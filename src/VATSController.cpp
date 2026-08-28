#include "VATSController.h"

#include "CombatHudVisibility.h"
#include "CombatTargetOverride.h"
#include "CompanionSupport.h"
#include "CrosshairVisibility.h"
#include "DamageNumbersVisibility.h"
#include "EngineInputLayer.h"
#include "Settings.h"
#include "Targeting.h"
#include "VatsResource.h"

#include <chrono>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef ERROR  // wingdi.h's ERROR macro clashes with REX::ERROR below

namespace VATS
{
	namespace
	{
		// Simulate a real press+release of a_vk at the OS level. Starfield
		// (single-player, no anti-cheat) can't tell this apart from an
		// actual key press, so it goes through the normal input pipeline
		// and triggers whatever native handler/animation/sound the game
		// itself would run - unlike UIMessageQueue::AddMessage(kHide),
		// which just yanks the menu away with no transition. No delay
		// between down/up was tried first and silently missed - likely too
		// fast for whatever poll-driven edge detection the game uses
		// internally to ever observe the "down" state. Blocks the calling
		// thread for a_holdMs; callers already run this off the game thread
		// (see CloseScannerIfOpen below).
		void SendKeyPress(std::uint32_t a_vk, std::chrono::milliseconds a_holdMs)
		{
			INPUT down{};
			down.type = INPUT_KEYBOARD;
			down.ki.wVk = static_cast<WORD>(a_vk);
			down.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(a_vk, MAPVK_VK_TO_VSC));
			::SendInput(1, &down, sizeof(INPUT));

			std::this_thread::sleep_for(a_holdMs);

			INPUT up{};
			up.type = INPUT_KEYBOARD;
			up.ki.wVk = static_cast<WORD>(a_vk);
			up.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(a_vk, MAPVK_VK_TO_VSC));
			up.ki.dwFlags = KEYEVENTF_KEYUP;
			::SendInput(1, &up, sizeof(INPUT));
		}

		constexpr int                       kMaxScannerCloseAttempts = 3;
		constexpr std::chrono::milliseconds kScannerCloseHoldMs{ 60 };
		constexpr std::chrono::milliseconds kScannerCloseSettleMs{ 250 };

		void CloseScannerIfOpen(int a_attempt);

		// SendInput + the blocking sleeps happen here, on a disposable
		// background thread - never IsMenuOpen. IsMenuOpen is a real engine
		// function call (RE::UI::GetSingleton()->IsMenuOpen), and every
		// other call site in this project runs it either from the render
		// thread (Overlay.cpp) or from the SFSE task thread (Advance()
		// itself) - both proven safe. Calling it from a throwaway
		// std::thread crashed the game on 2026-08-22 (crash coincided
		// exactly with this code path in the log, right after EngineInputLayer
		// had already been ruled out) - so the check is always bounced back
		// onto the task thread via AddTask before touching RE::UI, even
		// though that means round-tripping threads for every attempt.
		void ScannerCloseAttemptWorker(int a_attempt, std::uint32_t a_vk)
		{
			SendKeyPress(a_vk, kScannerCloseHoldMs);
			std::this_thread::sleep_for(kScannerCloseSettleMs);

			auto* tasks = SFSE::GetTaskInterface();
			if (!tasks) {
				return;
			}
			tasks->AddTask([a_attempt]() {
				auto* ui = RE::UI::GetSingleton();
				if (!ui || !ui->IsMenuOpen("MonocleMenu")) {
					VATS_LOG("[VATS] scanner closed after {} attempt(s)", a_attempt);
					return;
				}
				if (a_attempt >= kMaxScannerCloseAttempts) {
					VATS_LOG("[VATS] scanner still open after {} attempts, giving up", kMaxScannerCloseAttempts);
					return;
				}
				CloseScannerIfOpen(a_attempt + 1);
			});
		}

		// The scanner toggle key doesn't always fully close MonocleMenu in
		// one press - if a scan-action submenu is open (e.g. pressing E on
		// an NPC opens Techrunner's Scan/Mark/etc. list), a single press
		// only backs out of that submenu into the normal scanner view,
		// same as if Alexander had pressed it himself; the scanner itself
		// stays open. Found 2026-08-22. We have no reliable way to detect
		// which of these sub-states MonocleMenu is in, so instead of
		// guessing how many presses are needed, press once, check whether
		// MonocleMenu is still open, and press again if so - up to
		// kMaxScannerCloseAttempts, in case of deeper nesting. Capped
		// rather than looped forever so a menu that Q genuinely can't
		// close (something unanticipated) doesn't leave us spamming input
		// indefinitely. Called on the task thread (from Advance()); always
		// runs the blocking key-send off-thread so it never stalls the
		// game thread.
		//
		// EngineInputLayer::SetBlocked(true) is applied at the END of this
		// sequence (inside ScannerCloseAttemptWorker's terminal branches),
		// NOT before it starts. Found 2026-08-22: calling it up front (at
		// the same moment as the lock itself) made every single simulated
		// Q-press attempt fail - 0/N successes across every lock session
		// once EngineInputLayer was re-enabled, where it had been reliable
		// before. USER_EVENT_FLAG::TabMenuMaybe apparently gates more than
		// just the Tab/DataMenu action - plausibly any single-key menu-
		// toggle, including the scanner's own Q-toggle. Rather than give
		// up the nicer keypress-based close (Alexander's clear preference,
		// worked reliably for a long time before this), the fix is
		// ordering: let the real keypress-driven close finish first, only
		// then engage the block. The ~1 second window this delays
		// TabMenuMaybe's protection by is not a practical risk - nobody
		// hits Tab in the instant right after locking.
		void CloseScannerIfOpen(int a_attempt)
		{
			const std::uint32_t vk = Settings::Get().scannerToggleKeyVK;
			VATS_LOG("[VATS] scanner-close attempt {}/{}, vk=0x{:X}", a_attempt, kMaxScannerCloseAttempts, vk);
			std::thread(ScannerCloseAttemptWorker, a_attempt, vk).detach();
		}

		// Shared by both things a press can start - a combat lock and a
		// support session. Extracted 2026-08-28 rather than copied: the
		// keypress path below carries hard-won ordering (see
		// CloseScannerIfOpen above) that must not exist in two places.
		void CloseScannerPerSetting()
		{
			switch (Settings::Get().scannerCloseMode) {
			case 1:
				VATS_LOG("[VATS] closing scanner via simulated key press");
				CloseScannerIfOpen(1);
				break;
			case 2:
				VATS_LOG("[VATS] closing scanner via UIMessageQueue kHide");
				if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
					queue->AddMessage("MonocleMenu", RE::UI_MESSAGE_TYPE::kHide);
				}
				break;
			default:
				VATS_LOG("[VATS] leaving scanner open (iScannerCloseMode=0)");
				break;
			}
		}
	}

	Controller& Controller::Get()
	{
		static Controller instance;
		return instance;
	}

	Controller::OverlayState Controller::GetOverlayState()
	{
		OverlayState state;
		state.mode = m_mode.load(std::memory_order_relaxed);
		{
			const std::scoped_lock lock(m_targetLock);
			state.actor = m_target;
		}
		return state;
	}

	void Controller::ForceOff(const char* a_reason)
	{
		if (m_mode.load(std::memory_order_relaxed) == VATSMode::kOff) {
			return;
		}
		{
			const std::scoped_lock lock(m_targetLock);
			m_target.reset();
		}
		m_mode.store(VATSMode::kOff, std::memory_order_relaxed);
		// EngineInputLayer::SetBlocked(true) is no longer called anywhere
		// (see the Advance() lock branch comment - disabled again
		// 2026-08-22, broad collateral blocking with no confirmed
		// benefit). This SetBlocked(false) is harmless/idempotent either
		// way (re-enabling flags that are presumably already enabled),
		// left in place in case the feature comes back.
		EngineInputLayer::SetBlocked(false);
		EngineInputLayer::SetAdsBlocked(false);
		CrosshairVisibility::Restore();
		DamageNumbersVisibility::Restore();
		CombatTargetOverride::Disengage();
		CombatHudVisibility::Restore();
		{
			const std::scoped_lock overrideLock(m_projectileOverrideLock);
			ProjectileTypeOverride::Disengage(m_projectileOverride);
			m_projectileOverride = {};
			m_projectileOverrideTarget = 0;
		}
		// No console->Log() here (unlike Advance()) - this runs from the
		// render thread via Overlay::Draw(), and ConsoleLog has only ever
		// been touched from the game thread so far in this project. File
		// logging only until that's been verified safe cross-thread.
		VATS_LOG("[VATS] OFF (forced: {})", a_reason ? a_reason : "unspecified");
	}

	void Controller::RecordShotResult(bool a_hit)
	{
		m_shotHit.store(a_hit, std::memory_order_relaxed);
		m_shotTimestamp.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
	}

	void Controller::SyncProjectileOverride()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const std::uint64_t current = ProjectileTypeOverride::ResolveEquippedProjectile(player);

		const std::scoped_lock overrideLock(m_projectileOverrideLock);
		if (current == m_projectileOverrideTarget) {
			return;  // still the same weapon (or still nothing equipped) - nothing to do
		}
		ProjectileTypeOverride::Disengage(m_projectileOverride);
		m_projectileOverride = ProjectileTypeOverride::Engage(player);
		m_projectileOverrideTarget = current;
	}

	Controller::ShotResult Controller::GetLastShotResult()
	{
		ShotResult          result;
		const std::int64_t  ticks = m_shotTimestamp.load(std::memory_order_relaxed);
		if (ticks == 0) {
			return result;
		}
		result.valid = true;
		result.hit = m_shotHit.load(std::memory_order_relaxed);
		result.time = std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(ticks));
		return result;
	}

	void Controller::RequestAdvance()
	{
		const auto* tasks = SFSE::GetTaskInterface();
		if (!tasks) {
			VATS_ERROR("task interface unavailable, cannot advance VATS state");
			return;
		}

		// Press-time marker, compared against the "VATS ..." line logged
		// once Advance() actually runs on the game thread — see
		// starfield-vats-ui-hook memory for why this exists (ruled out SFSE
		// task-queue delay as a suspect during the OFF/ON investigation).
		VATS_LOG("[hotkey] advance requested (queued to game thread)");

		tasks->AddTask([]() {
			Controller::Get().Advance();
		});
	}

	void Controller::RequestCancel()
	{
		if (m_mode.load(std::memory_order_relaxed) == VATSMode::kOff) {
			return;  // nothing open, and a hold outside VATS must stay silent
		}
		const auto* tasks = SFSE::GetTaskInterface();
		if (!tasks) {
			VATS_ERROR("task interface unavailable, cannot cancel");
			return;
		}
		VATS_LOG("[hotkey] cancel requested (key held)");
		tasks->AddTask([]() {
			Controller::Get().ForceOff("VATS key held");
		});
	}

	bool Controller::TryAdvanceToNextTarget()
	{
		const auto& settings = Settings::Get();
		if (!settings.autoAdvanceOnKill) {
			return false;
		}
		if (m_mode.load(std::memory_order_relaxed) != VATSMode::kLocked) {
			return false;
		}

		RE::Actor* dying = nullptr;
		{
			const std::scoped_lock lock(m_targetLock);
			dying = m_target.get();
		}

		// Preferred source: the game's own crosshair activation target
		// (Alexander's suggestion, 2026-08-25, after the cone scan hopped
		// to an enemy in the next room). The cone scan has no line-of-sight
		// test at all - this project has never had a usable raycast, and
		// the one engine LOS function it tried is disabled as a confirmed
		// crash cause - so it happily picks targets through walls.
		//
		// commandTarget solves that without any raycasting of our own: it
		// is what drives the vanilla "hold E to interact" prompt, and the
		// game will not offer that through a wall, so it is occlusion-
		// correct by construction. Same reasoning that made it the source
		// for ordinary target acquisition in the first place. The trade is
		// range - it only reaches normal interaction distance - so the
		// advance simply does not fire for a distant next enemy, which is
		// the safer failure: no hop is better than a hop through a wall.
		RE::NiPointer<RE::Actor> next = GetCrosshairActivationTarget();
		float                    reportedDistance = -1.0f;
		float                    reportedAngle = -1.0f;

		if (next && next.get() == dying) {
			next = nullptr;  // still looking at the body that just dropped
		}

		if (!next && !settings.autoAdvanceRequireCrosshair) {
			// Cone scan, now restricted to actors actually fighting the
			// player. Requiring the crosshair target alone turned out to be
			// unusable in practice (Alexander, 2026-08-25: "den Target
			// wechsel kriege ich echt gar nicht hin") - it only reaches
			// interaction distance, which in a real firefight the next
			// enemy rarely is.
			//
			// The engagement filter is the stand-in for the line-of-sight
			// test this project does not have. Not real occlusion, but an
			// actor shooting at the player is rarely behind a wall, and one
			// in the next room who has not noticed them is excluded - which
			// was the actual complaint about the unrestricted scan.
			//
			// requireAlive matters here specifically: the corpse just
			// created is the thing nearest the crosshair, and the scan's
			// own dead filter cannot see that it is dead (see Targeting.h).
			// Excluding it by pointer is not enough either - any other body
			// lying nearby would be picked instead.
			if (auto pick = FindNearestActorToCrosshair(
					settings.autoAdvanceRangeMeters,
					static_cast<float>(settings.autoAdvanceConeDeg),
					dying,
					/*a_requireAlive*/ true,
					/*a_requireEngagedWithPlayer*/ settings.autoAdvanceRequireEngaged,
					/*a_requireOnScreen*/ true)) {
				next = pick->actor;
				reportedDistance = pick->worldDistance;
				reportedAngle = pick->angleDeg;
			}
		}

		if (!next) {
			return false;
		}

		{
			const std::scoped_lock lock(m_targetLock);
			m_target = next;
		}
		VATS_LOG("[VATS] auto-advance: new target formID=0x{:08X} (source={}, dist={:.1f}m, angle={:.1f}deg)",
			next->GetFormID(), reportedDistance < 0.0f ? "crosshair" : "cone-scan", reportedDistance, reportedAngle);
		return true;
	}

	bool Controller::TryAdvanceOrHold()
	{
		const auto& settings = Settings::Get();
		if (!settings.autoAdvanceOnKill) {
			return false;
		}

		if (TryAdvanceToNextTarget()) {
			const std::scoped_lock lock(m_advanceLock);
			m_advancePending = false;
			return true;
		}

		const auto now = std::chrono::steady_clock::now();
		const std::scoped_lock lock(m_advanceLock);
		if (!m_advancePending) {
			m_advancePending = true;
			m_advanceDeadline = now + std::chrono::milliseconds(settings.autoAdvanceGraceMs);
			return true;
		}
		if (now < m_advanceDeadline) {
			return true;  // still waiting for the player to pick the next one
		}

		m_advancePending = false;
		return false;
	}

	void Controller::Advance()
	{
		const VATSMode current = m_mode.load(std::memory_order_relaxed);
		auto*          console = RE::ConsoleLog::GetSingleton();

		if (current == VATSMode::kSupport) {
			// Already in a support session: a TAP performs the action. Leaving
			// is a HOLD of the same key (RequestCancel), which is what lets one
			// binding do both without swallowing anything the game wants.
			RE::NiPointer<RE::Actor> friendTarget;
			{
				const std::scoped_lock lock(m_targetLock);
				friendTarget = m_target;
			}
			if (!friendTarget) {
				ForceOff("support target vanished");
				return;
			}
			CompanionSupport::HealActor(friendTarget.get());
			return;
		}

		if (current == VATSMode::kOff) {
			// Off -> Locked requires the hand scanner to actually be open —
			// otherwise the hotkey could lock onto something anywhere, with
			// no highlight UI ever shown first (the "TARGETING (N)" hint
			// only draws while scanning). This used to be a two-press
			// Off -> Aiming -> Locked cycle so Alexander could see the
			// highlight before committing, but once activation required the
			// scanner to already be open, the scanner's own highlight plus
			// the hint already cover that — so this now locks directly onto
			// whatever's under the crosshair on a single press. Reads
			// GetCrosshairActivationTarget() fresh here rather than reusing
			// a render-thread-cached pick, since there's no longer an
			// Aiming period for Overlay::Draw() to have populated one in.
			// No cone-scan fallback if nothing's there — that used to exist
			// and silently reintroduced through-wall locking (occlusion-
			// blind), see starfield-vats-ui-hook memory, 2026-08-22.
			auto* ui = RE::UI::GetSingleton();
			if (!ui || !ui->IsMenuOpen("MonocleMenu")) {
				VATS_LOG("[VATS] ignored (scanner not open)");
				return;
			}

			RE::NiPointer<RE::Actor> lockTarget = GetCrosshairActivationTarget();
			if (!lockTarget) {
				// Nothing hostile there - but the same pick may well have
				// landed on one of the player own people, which
				// GetCrosshairActivationTarget throws away by design. That is
				// the support case, and it takes over the same press.
				if (auto teammate = GetCrosshairTeammate()) {
					const std::uint32_t friendID = teammate->GetFormID();
					{
						const std::scoped_lock lock(m_targetLock);
						m_target = teammate;
					}
					m_mode.store(VATSMode::kSupport, std::memory_order_relaxed);
					VATS_LOG("[VATS] SUPPORT | companion formID=0x{:08X}", friendID);
					CompanionSupport::LogPlayerInventory();
					if (console) {
						console->Log("[VATS] SUPPORT | companion formID=0x{:08X}", friendID);
					}
					CloseScannerPerSetting();
					return;
				}
				VATS_LOG("[VATS] ignored (nothing under crosshair)");
				return;
			}

			const std::uint32_t formID = lockTarget->GetFormID();
			{
				const std::scoped_lock lock(m_targetLock);
				m_target = lockTarget;
			}
			m_mode.store(VATSMode::kLocked, std::memory_order_relaxed);
			VATS_LOG("[VATS] LOCKED | target formID=0x{:08X}", formID);
			if (console) {
				console->Log("[VATS] LOCKED | target formID=0x{:08X}", formID);
			}

			// Re-reads the player's full health/oxygen so perk, gear and
			// food changes since the last lock are picked up, and starts
			// damage accounting fresh so a target that was already hurt
			// before being locked isn't billed retroactively.
			VatsResource::Get().OnLockStart();
			{
				const std::scoped_lock advanceLock(m_advanceLock);
				m_advancePending = false;
			}

			// EXPERIMENTAL (2026-08-24, Alexander's idea) - see
			// CombatTargetOverride.h. Engaged unconditionally (no settings
			// flag yet, first real in-game test) rather than behind
			// hideCrosshairWhileLocked - this is unrelated to that flag's
			// actual purpose.
			CombatTargetOverride::Engage(lockTarget.get());

			// Flip the equipped weapon hitscan->real-projectile (and apply
			// the reduced projectile speed) for the WHOLE lock, not per
			// trigger-hold - see VATSController.h for why. Done here, at
			// lock time, so it is finished long before the player can pull
			// the trigger; SteeringLoop no longer engages anything itself.
			// SyncProjectileOverride() (not a direct Engage() call) so this
			// shares the exact same "only Disengage+Engage on an actual
			// weapon change" logic as the per-frame re-check below - a
			// weapon switched WHILE already Locked needs the identical
			// handling, see that function's comment.
			SyncProjectileOverride();

			if (Settings::Get().hideCrosshairWhileLocked) {
				CrosshairVisibility::Hide();
				// DamageNumbersVisibility (2026-08-24) - toggles Starfield's
				// own Interface > "Show Damage Numbers" setting instead of
				// fighting Scaleform for the individual popups (which are
				// unreachable anyway, see CombatHudVisibility.h) - same
				// safe one-shot mechanism as CrosshairVisibility above.
				DamageNumbersVisibility::Hide();
				// CombatHudVisibility (2026-08-24, rewritten) - hides the
				// native hit marker/kill marker/crit banner. Back to a
				// one-shot call here, same as Crosshair/DamageNumbers above
				// - the JPEXS decompile confirmed HitKillIndicator (the
				// container for these three) is a persistent object, not
				// spawned per-hit like the damage-number popups, so a
				// one-shot find-and-hide is the right shape after all. See
				// CombatHudVisibility.h for the full history.
				CombatHudVisibility::HideActive();
			}
			// EngineInputLayer::SetAdsBlocked(true) disabled again
			// 2026-08-23: confirmed via screenshot that USER_EVENT_FLAG::
			// Fighting is far broader than its name suggests - it holstered
			// the weapon entirely on lock, not just blocked ADS. Same
			// "collateral damage" pattern as TabMenuMaybe/POVSwitch above.
			// The SetAdsBlocked(false) calls in ForceOff()/the Locked->Off
			// branch stay - harmless/idempotent either way, left in place
			// in case this comes back with a narrower flag.

			// Close the hand scanner on lock so the weapon comes back up
			// immediately, mirroring vanilla scan-then-act flows. Mechanism
			// is INI-selectable (iScannerCloseMode, see Settings.h) -
			// simulated keypress (mode 1, default) vs. UIMessageQueue
			// kHide (mode 2) vs. leave it open (mode 0).
			//
			// EngineInputLayer::SetBlocked(true) disabled again 2026-08-22.
			// Re-enabling it (POVSwitch/TabMenuMaybe/WheelZoom) was meant
			// to suppress Tab/DataMenu and mouse-wheel POV switching at
			// the engine's own input-mapping level. In practice, per
			// Alexander's report, it blocked far more than intended
			// (favorites menu and other unrelated single-key menu
			// shortcuts all went dead while Locked) without even fixing
			// the thing it was for (Tab still doesn't end the lock -
			// BackKeyInterceptor's own diagnostic logging shows zero
			// "back key seen" events despite the hook installing
			// successfully, a separate, still-open problem). Net
			// negative: real collateral damage, no confirmed benefit.
			// TabMenuMaybe is evidently a much broader flag than its name
			// suggests - don't re-enable without first confirming exactly
			// what it does and does not gate.
			CloseScannerPerSetting();
			return;
		}

		// current == Locked. A TAP does nothing here as of 2026-08-28.
		//
		// It used to end the lock, and that job moved to HOLDING the key -
		// the same gesture that leaves a support session, so the model is
		// now identical in both: tap acts, hold leaves. ForceOff does the
		// entire teardown that used to live here, so nothing was lost in
		// the move.
		//
		// The tap is deliberately left unbound rather than given a
		// placeholder. Alexander's idea for it is switching to the next
		// enemy, which this class can very nearly already do
		// (TryAdvanceToNextTarget, written for auto-advance-on-kill) - but
		// that feature is parked on the missing line-of-sight test, since
		// without one it happily picks targets through walls. Wiring the
		// tap to it now would ship that same bug under a new button.
		//
		// His other idea for it is cycling body parts, which would need the
		// body-part system back - deliberately dropped 2026-08-26 for want of
		// a usable per-part anchor (the bone walk never found one, see
		// docs/FINDINGS.md). Either way the button is the easy half; both
		// candidates are blocked on something else entirely.
		VATS_LOG("[VATS] tap while locked - nothing bound to it yet (hold to exit)");
	}
}
