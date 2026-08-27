#include "AimAssist.h"

#include "AimAssistProbe.h"
#include "GameOffsets.h"
#include "InputHookPump.h"
#include "ProjectileFlagProbe.h"
#include "ProjectileTracker.h"
#include "ProjectileTypeOverride.h"
#include "SafeMem.h"
#include "Settings.h"
#include "Targeting.h"
#include "UI/CameraProject.h"
#include "VATSController.h"
#include "WorldBoundProbe.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <thread>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef ERROR  // wingdi.h's ERROR macro clashes with REX::ERROR below

namespace VATS
{
	namespace
	{
		std::atomic<bool> s_buttonHeld{ false };

		// Identifies which physical button-down this SteeringLoop instance
		// belongs to. Incremented on every WM_LBUTTONDOWN (HookProc) and
		// also doubles as "the generation of the most recent press" when
		// just read - a loop's own button-still-held check compares against
		// this rather than trusting the shared s_buttonHeld flag alone, so
		// a newer click starting during an older hold's post-release grace
		// window can't fool the older loop into thinking its own button is
		// still down (see IsMyPressStillHeld below - found 2026-08-23 while
		// removing the single-steering-thread gate that used to make
		// overlapping holds impossible in the first place).
		std::atomic<std::uint64_t> s_pressGeneration{ 0 };

		[[nodiscard]] bool IsMyPressStillHeld(std::uint64_t a_myGeneration)
		{
			return s_buttonHeld.load(std::memory_order_relaxed) &&
			       s_pressGeneration.load(std::memory_order_relaxed) == a_myGeneration;
		}

		[[nodiscard]] bool GameWindowHasFocus()
		{
			const HWND foreground = ::GetForegroundWindow();
			if (!foreground) {
				return false;
			}
			DWORD pid = 0;
			::GetWindowThreadProcessId(foreground, &pid);
			return pid == ::GetCurrentProcessId();
		}

		// Minimal standalone version of Overlay.cpp's ResolveOnScreen —
		// dead check + position read + projection + world distance to the
		// player, no telemetry/throttling (that's a UI concern; this runs
		// on a background thread and needs to be cheap and quiet). Screen
		// position is still resolved and gates on-screen-ness (chance is 0
		// if the target isn't currently in view at all — same "roughly
		// facing the target" requirement FO4/76 VATS has via its target
		// list), but no longer feeds the chance formula itself — see
		// ComputeChancePercent.
		[[nodiscard]] bool ResolveTargetScreen(RE::Actor* a_actor, float& a_outDist)
		{
			std::uint32_t boolBits = 0;
			if (SafeRead(reinterpret_cast<const std::byte*>(a_actor) + GameOffsets::kBoolBits, &boolBits, sizeof(boolBits)) &&
				(boolBits & GameOffsets::kActorDeadBit) != 0) {
				return false;
			}

			RE::NiPoint3 pos{};
			if (!SafeRead(reinterpret_cast<const std::byte*>(a_actor) + GameOffsets::kLocation, &pos, sizeof(pos))) {
				return false;
			}
			const RE::NiPoint3 aimPos = WorldBoundProbe::GetAimPoint(a_actor, pos);

			float sx = 0.0f, sy = 0.0f;
			if (!UI::WorldToScreen(aimPos, sx, sy)) {
				return false;
			}
			if (sx < 0.0f || sx > 1.0f || sy < 0.0f || sy > 1.0f) {
				return false;  // in front of the camera but outside the FOV frustum
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return false;
			}
			const auto  pp = player->GetPosition();
			const float dx = aimPos.x - pp.x;
			const float dy = aimPos.y - pp.y;
			const float dz = aimPos.z - pp.z;
			a_outDist = std::sqrt(dx * dx + dy * dy + dz * dz);
			return true;
		}

		// Runs for the whole duration the fire button stays physically
		// held, not just once - a single button-down doesn't map to a
		// single shot (automatic weapons fire repeatedly for as long as
		// it's held, driven entirely inside the game, with no further
		// mouse events for us to see). Rolls hit/miss ONCE per hold
		// session rather than per bullet - we have no reliable way to
		// learn a weapon's actual fire rate, so re-rolling every shot in a
		// burst isn't practical yet; every shot in one held-trigger burst
		// currently shares the same hit/miss outcome. Known v1
		// simplification.
		//
		// Replaces the original SendMouseMove-based camera-steering design
		// (removed 2026-08-22, see the file-level comment history / git
		// log) - per Alexander, the weapon/camera should never visibly
		// snap onto the target at all. Instead this loop repeatedly asks
		// ProjectileTracker to find and redirect any freshly-fired player
		// round in-flight, every tick for the duration of the hold, so a
		// sustained burst gets each of its rounds bent independently as
		// they spawn. The real click is never touched; Starfield's own
		// fire-rate timer and ballistics/damage/hit-reaction pipeline
		// still do everything except the round's own trajectory.
		// Engage() runs as the very first thing on this thread (2026-08-25,
		// revert of a same-day attempt to run it synchronously inside
		// HookProc's WH_MOUSE_LL callback instead - that broke shot
		// redirect entirely by stalling the system-wide input path, see
		// HookProc's comment). Still meaningfully earlier than the
		// original position (used to run after ForceAimAssist,
		// ProjectileFlagProbe, ResolveTargetScreen, ComputeChancePercent,
		// and the roll), just without touching the low-level hook at all.
		// Every exit path below Disengage()s it, since it's now
		// unconditional at function entry regardless of what the rest of
		// this function ends up doing.
		void SteeringLoop(std::uint64_t a_myGeneration)
		{
			const auto state = Controller::Get().GetOverlayState();
			if (state.mode != VATSMode::kLocked || !state.actor) {
				return;
			}

			// Forces Starfield's own native bullet-bending aim assist on
			// for the equipped weapon, once per shot (see AimAssistProbe.h
			// for the full confirmed pointer chain and why aimAssistEnabled
			// reads false by default - suspected gamepad-only gating).
			// Test: does this alone make hitscan shots actually curve
			// toward the locked target, or is there a separate device
			// check elsewhere that a data flag flip can't reach?
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				AimAssistProbe::ForceAimAssist(player);

				// Read-only probe (2026-08-23, Alexander's idea): can a
				// weapon's hitscan-vs-projectile behavior be flipped at
				// runtime, so ProjectileTracker's already-working redirect
				// applies to every weapon instead of needing to solve the
				// hitscan raycast-redirect problem separately? Logs the
				// equipped weapon's ammo/projectile flags only - no write
				// yet. See ProjectileFlagProbe.h for the full chain.
				//
				// Skipped entirely below iLogLevel=3 (2026-08-27), not just
				// silenced: this walks weapon -> WeaponAmmoData -> ammo ->
				// projectile with a dozen guarded reads and builds two hex
				// dump strings, on EVERY trigger pull, purely to produce log
				// lines. Nothing downstream reads its result.
				if (Log::Verbose()) {
					ProjectileFlagProbe::LogCurrentWeaponProjectileFlags(player);
				}
			}

			// Chance (and therefore the hit/miss roll) is based on distance
			// + LOS at the moment the trigger is pulled - not evaluated
			// per shot within a held burst, same "one roll per hold"
			// simplification as before.
			float initialDist = 0.0f;
			if (!ResolveTargetScreen(state.actor.get(), initialDist)) {
				VATS_TRACE("[VATS] aim-assist: target not resolvable/in view at hold start, no assist this hold");
				// Still record a (guaranteed-miss) result - the real trigger
				// was pulled and a real round left the gun, so the HUD
				// should say something rather than nothing. Found
				// 2026-08-23: silence here was indistinguishable from "this
				// click was never even seen at all" from Alexander's
				// perspective (see also the s_steering removal below).
				Controller::Get().RecordShotResult(false);
				return;
			}

			// Chance/roll removed 2026-08-25, Alexander's explicit call:
			// always redirect toward the target once Locked and resolvable
			// on screen - no distance falloff, no RNG. An actual wall in
			// the way still blocks the shot on its own, no separate LOS/
			// obstruction check needed for that - this redirects a real,
			// physically simulated projectile now (ProjectileTypeOverride/
			// ProjectileTracker), not a raycast, so real collision already
			// does that job. HasDetectionLOS/fullChanceRangeMeters/
			// maxEffectiveRangeMeters are unused by this function now but
			// left in Settings/Targeting.h - Overlay.cpp's HUD readout and
			// other callers still use HasDetectionLOS independently.
			constexpr bool hit = true;
			VATS_TRACE("[VATS] aim-assist: hold started, chance=100% (fixed) -> HIT");
			Controller::Get().RecordShotResult(hit);

			// Fast-poll right after the click, falling back to a slower
			// cadence afterward. Found 2026-08-22: at the close range
			// Alexander's screenshots showed, a fired round can travel to
			// its target and resolve well inside 20ms - the original
			// fixed 20ms tick interval meant the very first scan often
			// ran *after* the round had already hit or expired, so it was
			// never seen as a candidate at all (zero "[VATS] projectile
			// redirect:" log lines despite dozens of correctly-rolled
			// HITs). 2ms for the first 150ms after the click covers close-
			// range shots without keeping a sustained automatic-weapon
			// hold spinning that tightly for its whole duration.
			constexpr auto kFastPollInterval = std::chrono::milliseconds(2);
			constexpr auto kFastPollWindow = std::chrono::milliseconds(150);
			constexpr auto kSlowPollInterval = std::chrono::milliseconds(20);
			constexpr auto kMaxHoldDuration = std::chrono::seconds(10);  // safety net if a button-up is ever missed

			// Found 2026-08-23: two holds out of 43 in a test session ended
			// with ZERO "projectile candidate" lines at all - not a missed
			// redirect, the scan never saw any fresh round in the cell
			// reference array during the whole hold. Both were unusually
			// short (~85-90ms, a quick semi-auto tap). Starfield weapons
			// have a real attack-delay between trigger-down and the shot
			// actually firing (e.g. the Shingen mod's own WFIR data showed
			// "Attack Delay (Seconds): 0.058824") - a fast enough tap can
			// release the mouse button before the round has even spawned,
			// and the scan loop used to stop instantly on button-up. Keep
			// scanning for a short grace period after release instead, so
			// a shot whose spawn lagged behind the click still gets a
			// chance to be found and redirected (and the type override
			// stays engaged long enough to matter for it, too).
			constexpr auto kPostReleaseGrace = std::chrono::milliseconds(250);

			// The hitscan->real-projectile flip is NOT this function's job
			// any more (2026-08-25): Controller engages it once when the
			// lock starts and releases it when the lock ends, so it is
			// already active long before the trigger is ever pulled. See
			// VATSController.h for why - doing it per hold meant every
			// single trigger pull raced the round leaving the barrel.
			std::unordered_map<std::uint64_t, ProjectileTracker::TrackedState> tracked;
			const auto                        start = std::chrono::steady_clock::now();
			std::optional<std::chrono::steady_clock::time_point> releasedAt;
			while (Controller::Get().GetMode() == VATSMode::kLocked) {
				const auto elapsed = std::chrono::steady_clock::now() - start;
				if (elapsed > kMaxHoldDuration) {
					VATS_WARN("[VATS] aim-assist: hold exceeded safety timeout, stopping redirect scan");
					break;
				}

				if (IsMyPressStillHeld(a_myGeneration)) {
					releasedAt.reset();
				} else {
					if (!releasedAt) {
						releasedAt = std::chrono::steady_clock::now();
					} else if (std::chrono::steady_clock::now() - *releasedAt > kPostReleaseGrace) {
						break;
					}
				}

				const auto currentState = Controller::Get().GetOverlayState();
				if (!currentState.actor) {
					// Target died/left view mid-burst - nothing left to
					// redirect toward, stop scanning for the rest of this
					// hold.
					break;
				}
				ProjectileTracker::RedirectFreshProjectiles(currentState.actor.get(), hit, tracked);

				std::this_thread::sleep_for(elapsed < kFastPollWindow ? kFastPollInterval : kSlowPollInterval);
			}
			VATS_TRACE("[VATS] aim-assist: hold ended");
		}

		LRESULT CALLBACK HookProc(int a_code, WPARAM a_wParam, LPARAM a_lParam)
		{
			if (a_code == HC_ACTION) {
				if (a_wParam == WM_LBUTTONDOWN || a_wParam == WM_LBUTTONUP) {
					const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(a_lParam);
					// Ignore our own injected mouse-move events reaching
					// here is a non-issue (we never inject clicks anymore,
					// only moves), but keep the check for any future
					// synthetic click use - injected clicks should never
					// restart/stop a steering session.
					const bool injected = (info->flags & LLMHF_INJECTED) != 0;
					if (!injected) {
						if (a_wParam == WM_LBUTTONDOWN) {
							s_buttonHeld.store(true);
							const std::uint64_t myGeneration = s_pressGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
							// Every real press gets its own SteeringLoop now -
							// no single-instance gate (removed 2026-08-23).
							// ProjectileTypeOverride is already reference-
							// counted specifically to support overlapping
							// holds, so the only thing the old gate
							// accomplished was silently dropping a fast
							// follow-up click that arrived before the
							// previous loop's next ~2-20ms poll tick noticed
							// the release - no roll, no HUD feedback, no
							// redirect attempt at all for that click.
							if (GameWindowHasFocus() && Controller::Get().GetMode() == VATSMode::kLocked) {
								// REVERTED 2026-08-25: Engage() briefly lived
								// here, called synchronously in this hook
								// before spawning the thread. Broke shot
								// redirect entirely (Alexander: "nichts trifft
								// mehr") - a WH_MOUSE_LL hook runs synchronously
								// in the SYSTEM-WIDE input delivery path, not a
								// normal background thread; ResolveEquippedProjectile's
								// inventory-array walk (multiple SafeReads in a
								// loop) is real work, not the O(1) atomic/
								// focus checks this hook already did, and
								// stalling here delays the real click reaching
								// the game at all, corrupting the whole
								// redirect timing chain, not just the
								// semi-auto race it was meant to fix. Engage()
								// now happens back on SteeringLoop's own
								// thread instead (as the very first thing
								// there, still earlier than before this whole
								// investigation started) - see SteeringLoop.
								std::thread([myGeneration]() {
									SteeringLoop(myGeneration);
								}).detach();
							}
						} else {
							s_buttonHeld.store(false);
						}
					}
					// Never swallowed - the real click always reaches
					// Starfield. We only ever influence aim direction
					// alongside it, never the firing itself.
				}
				// Mouse-wheel body-part cycling removed 2026-08-22 along
				// with the body-part system itself (see VATSController.h) -
				// the wheel is no longer touched at all, POV/perspective
				// switch works normally while Locked again.
			}
			return ::CallNextHookEx(nullptr, a_code, a_wParam, a_lParam);
		}
	}

	void AimAssist::ThreadProc(const std::stop_token& a_stop)
	{
		VATS_LOG("aim-assist started");

		RunLowLevelHookPump(
			a_stop, LowLevelHookKind::kMouse,
			reinterpret_cast<LowLevelHookProc>(&HookProc), "aim-assist",
			nullptr,
			[]() {
				// The hook is removed whenever the game loses focus, so a
				// button-up that happens while alt-tabbed is never seen.
				// Clearing the held flag here is what stops that stranding
				// a SteeringLoop on a button it thinks is still down until
				// its 10s safety timeout fires.
				s_buttonHeld.store(false, std::memory_order_relaxed);
			});
	}

	void AimAssist::Start()
	{
		if (m_thread.joinable()) {
			return;
		}
		m_thread = std::jthread(&AimAssist::ThreadProc);
	}

	void AimAssist::Stop()
	{
		if (m_thread.joinable()) {
			m_thread.request_stop();
			m_thread.join();
		}
	}
}
