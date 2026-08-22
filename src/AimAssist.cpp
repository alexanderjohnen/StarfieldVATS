#include "AimAssist.h"

#include "GameOffsets.h"
#include "ProjectileTracker.h"
#include "SafeMem.h"
#include "Settings.h"
#include "Targeting.h"
#include "UI/CameraProject.h"
#include "VATSController.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>
#include <unordered_set>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef ERROR  // wingdi.h's ERROR macro clashes with REX::ERROR below

namespace VATS
{
	namespace
	{
		HHOOK             s_hook = nullptr;
		std::atomic<bool> s_buttonHeld{ false };
		std::atomic<bool> s_steering{ false };

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

		// Single aim point (chest height) — the Suit/Helmet/Pack body-part
		// system was removed 2026-08-22, see GameOffsets::kAimPointChestZ.
		[[nodiscard]] RE::NiPoint3 ResolveAimWorldPos(const RE::NiPoint3& a_pos)
		{
			RE::NiPoint3 out = a_pos;
			out.z += GameOffsets::kAimPointChestZ;
			return out;
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
			const RE::NiPoint3 aimPos = ResolveAimWorldPos(pos);

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

		// Distance-based chance (2026-08-22 redesign, replaces an earlier
		// screen-space "must aim precisely at the crosshair" cone) —
		// Alexander's reference: FO4/76 VATS shows a high chance number
		// even while the reticle points nowhere near the target (see
		// starfield-vats-mod-design memory); the whole point of VATS is
		// not requiring aim precision, only a lock and being in range/LOS
		// — doubly true here since the round gets redirected in-flight
		// regardless of exact aim (ProjectileTracker), so tying the
		// chance itself to crosshair proximity fought the mechanic rather
		// than complementing it. centerHitChancePercent applies at/under
		// Settings::fullChanceRangeMeters, falling linearly to 0% by
		// Settings::maxEffectiveRangeMeters. ANDed with a real occlusion
		// check (HasDetectionLOS, see Targeting.h) — being close doesn't
		// matter if a wall is actually in the way.
		[[nodiscard]] float ComputeChancePercent(RE::Actor* a_target, float a_distance)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !HasDetectionLOS(player, a_target)) {
				return 0.0f;
			}

			const auto& settings = Settings::Get();
			const float full = settings.fullChanceRangeMeters;
			const float maxR = settings.maxEffectiveRangeMeters;
			const float t = maxR > full ?
				std::clamp(1.0f - (a_distance - full) / (maxR - full), 0.0f, 1.0f) :
				(a_distance <= full ? 1.0f : 0.0f);
			return t * static_cast<float>(settings.centerHitChancePercent);
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
		void SteeringLoop()
		{
			const auto state = Controller::Get().GetOverlayState();
			if (state.mode != VATSMode::kLocked || !state.actor) {
				return;
			}

			// Chance (and therefore the hit/miss roll) is based on distance
			// + LOS at the moment the trigger is pulled - not evaluated
			// per shot within a held burst, same "one roll per hold"
			// simplification as before.
			float initialDist = 0.0f;
			if (!ResolveTargetScreen(state.actor.get(), initialDist)) {
				REX::INFO("[VATS] aim-assist: target not resolvable/in view at hold start, chance is 0, no assist this hold");
				return;
			}
			const float chancePercent = ComputeChancePercent(state.actor.get(), initialDist);

			// Zero chance (out of effective range or no LOS): don't
			// redirect anything for the rest of this hold - real aim
			// decides, full stop, same as the target being off-screen
			// entirely.
			if (chancePercent <= 0.0f) {
				REX::INFO("[VATS] aim-assist: zero chance (out of range or no LOS), firing unassisted");
				return;
			}

			static thread_local std::mt19937     rng{ std::random_device{}() };
			std::uniform_real_distribution<float> dist(0.0f, 1.0f);
			const float                           roll = dist(rng);
			const float                           chance = chancePercent / 100.0f;
			const bool                             hit = roll < chance;
			REX::INFO("[VATS] aim-assist: hold started, chance={:.0f}%, roll={:.2f} -> {}", chancePercent, roll, hit ? "HIT" : "MISS");

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

			std::unordered_set<std::uint64_t> handled;
			const auto                        start = std::chrono::steady_clock::now();
			while (s_buttonHeld.load() && Controller::Get().GetMode() == VATSMode::kLocked) {
				const auto elapsed = std::chrono::steady_clock::now() - start;
				if (elapsed > kMaxHoldDuration) {
					REX::WARN("[VATS] aim-assist: hold exceeded safety timeout, stopping redirect scan");
					break;
				}

				const auto currentState = Controller::Get().GetOverlayState();
				if (!currentState.actor) {
					// Target died/left view mid-burst - nothing left to
					// redirect toward, stop scanning for the rest of this
					// hold.
					break;
				}
				ProjectileTracker::RedirectFreshProjectiles(currentState.actor.get(), hit, handled);

				std::this_thread::sleep_for(elapsed < kFastPollWindow ? kFastPollInterval : kSlowPollInterval);
			}
			REX::INFO("[VATS] aim-assist: hold ended");
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
							if (!s_steering.load() &&
								GameWindowHasFocus() &&
								Controller::Get().GetMode() == VATSMode::kLocked) {
								s_steering.store(true);
								std::thread([]() {
									SteeringLoop();
									s_steering.store(false);
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
		s_hook = ::SetWindowsHookExW(WH_MOUSE_LL, HookProc, nullptr, 0);
		if (!s_hook) {
			REX::ERROR("failed to install aim-assist mouse hook, GetLastError={}", ::GetLastError());
			return;
		}
		REX::INFO("aim-assist started");

		while (!a_stop.stop_requested()) {
			MSG msg;
			while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
				::TranslateMessage(&msg);
				::DispatchMessageW(&msg);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		::UnhookWindowsHookEx(s_hook);
		s_hook = nullptr;
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
