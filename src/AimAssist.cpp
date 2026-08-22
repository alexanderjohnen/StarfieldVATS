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
		// dead check + position read + projection, no telemetry/throttling
		// (that's a UI concern; this runs on a background thread and needs
		// to be cheap and quiet).
		[[nodiscard]] bool ResolveTargetScreen(RE::Actor* a_actor, float& a_outSx, float& a_outSy)
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
			a_outSx = sx;
			a_outSy = sy;
			return true;
		}

		// Hit-chance cone (2026-08-22, Alexander's idea): 100% dead center
		// on the crosshair, falling off linearly to 0% at
		// Settings::assistRadius. a_sx/a_sy are the target's aim-point
		// screen position (0..1, from ResolveTargetScreen) — distance is
		// measured from true screen center (0.5, 0.5), i.e. the crosshair.
		// ANDed with a real occlusion check (HasDetectionLOS, see
		// Targeting.h) — being aimed-at doesn't matter if a wall is
		// actually in the way; this is the live "is target visible" signal
		// the earlier GetActorKnowledge attempt was meant to provide,
		// found instead in Cassiopeia Papyrus Extender's source 2026-08-22.
		[[nodiscard]] float ComputeChancePercent(RE::Actor* a_target, float a_sx, float a_sy)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !HasDetectionLOS(player, a_target)) {
				return 0.0f;
			}

			const float dx = a_sx - 0.5f;
			const float dy = a_sy - 0.5f;
			const float screenDist = std::sqrt(dx * dx + dy * dy);
			const float radius = Settings::Get().assistRadius;
			if (radius <= 0.0f) {
				return 0.0f;
			}
			const float t = std::clamp(1.0f - screenDist / radius, 0.0f, 1.0f);
			return t * static_cast<float>(Settings::Get().centerHitChancePercent);
		}

		void SendMouseMove(int a_dx, int a_dy)
		{
			if (a_dx == 0 && a_dy == 0) {
				return;
			}
			INPUT input{};
			input.type = INPUT_MOUSE;
			input.mi.dx = a_dx;
			input.mi.dy = a_dy;
			input.mi.dwFlags = MOUSEEVENTF_MOVE;
			::SendInput(1, &input, sizeof(INPUT));
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
		// Steers in small proportional steps toward (hit) or away from
		// (miss) the target's *current* screen position every tick, rather
		// than jumping there once - stays correct as the target moves
		// (Locked tracks regardless of camera direction) and reads as
		// assistive pull rather than a jarring teleport-snap. The real
		// click is never touched; Starfield's own fire-rate timer decides
		// when shots actually happen, we only ever influence where the
		// camera points meanwhile.
		void SteeringLoop()
		{
			const auto state = Controller::Get().GetOverlayState();
			if (state.mode != VATSMode::kLocked || !state.actor) {
				return;
			}

			// Chance (and therefore the hit/miss roll) is based on how
			// close the player's actual aim already is to the target at
			// the moment the trigger is pulled - not evaluated per shot
			// within a held burst, same "one roll per hold" simplification
			// as before, just with a real input feeding the roll now
			// instead of a flat constant.
			float initialSx = 0.0f, initialSy = 0.0f;
			if (!ResolveTargetScreen(state.actor.get(), initialSx, initialSy)) {
				REX::INFO("[VATS] aim-assist: target not resolvable/in view at hold start, chance is 0, no assist this hold");
				return;
			}
			const float chancePercent = ComputeChancePercent(state.actor.get(), initialSx, initialSy);

			// Outside the assist cone entirely (chance == 0): do nothing
			// at all, don't touch the mouse for the rest of this hold.
			// Found 2026-08-22 that without this, a shot taken while
			// aiming nowhere near the target still steered the crosshair
			// most of the way to the target's screen position (plus the
			// miss offset) regardless of chance being 0 - the chance
			// number was right, but the steering below never actually
			// looked at it, so a "you get no help" situation still
			// produced a big, jarring snap. This is the fix: chance 0
			// means real aim decides, full stop, same as target being
			// off-screen entirely.
			if (chancePercent <= 0.0f) {
				REX::INFO("[VATS] aim-assist: outside assist radius, chance is 0, firing unassisted");
				return;
			}

			static thread_local std::mt19937     rng{ std::random_device{}() };
			std::uniform_real_distribution<float> dist(0.0f, 1.0f);
			const float                           roll = dist(rng);
			const float                           chance = chancePercent / 100.0f;
			const bool                             hit = roll < chance;
			REX::INFO("[VATS] aim-assist: hold started, chance={:.0f}%, roll={:.2f} -> {}", chancePercent, roll, hit ? "HIT" : "MISS");

			// Diagnostic (2026-08-22): probe for the just-fired projectile
			// a beat after the real click, once it's had time to actually
			// spawn/register in the cell. Not wired into anything yet -
			// see ProjectileTracker.h for why.
			std::thread([]() {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				ProjectileTracker::ProbeAfterFire();
			}).detach();

			const int   screenW = ::GetSystemMetrics(SM_CXSCREEN);
			const int   screenH = ::GetSystemMetrics(SM_CYSCREEN);
			const float scale = Settings::Get().mouseSensitivityScale;

			constexpr auto  kTickInterval = std::chrono::milliseconds(20);
			constexpr float kConvergePerTick = 0.4f;  // fraction of remaining offset closed each tick
			constexpr auto  kMaxHoldDuration = std::chrono::seconds(10);  // safety net if a button-up is ever missed

			const auto start = std::chrono::steady_clock::now();
			while (s_buttonHeld.load() && Controller::Get().GetMode() == VATSMode::kLocked) {
				if (std::chrono::steady_clock::now() - start > kMaxHoldDuration) {
					REX::WARN("[VATS] aim-assist: hold exceeded safety timeout, stopping steering");
					break;
				}

				const auto currentState = Controller::Get().GetOverlayState();
				float      sx = 0.0f, sy = 0.0f;
				if (!currentState.actor || !ResolveTargetScreen(currentState.actor.get(), sx, sy)) {
					// Target died/left view mid-burst - nothing sensible
					// left to steer toward, stop nudging for the rest of
					// this hold.
					break;
				}

				// Miss offset scales with assistRadius rather than a fixed
				// 0.08 - a hold that only barely qualified for the cone
				// (small radius, or aiming near its edge) shouldn't still
				// get pushed by a comparatively huge fixed amount.
				float aimSx = sx;
				float aimSy = sy;
				if (!hit) {
					const float missOffset = Settings::Get().assistRadius * 0.6f;
					aimSx = std::clamp(sx + missOffset, 0.0f, 1.0f);
					aimSy = std::clamp(sy + missOffset, 0.0f, 1.0f);
				}

				const float dxFrac = (aimSx - 0.5f) * kConvergePerTick;
				const float dyFrac = (aimSy - 0.5f) * kConvergePerTick;
				const int   dx = static_cast<int>(dxFrac * screenW * scale);
				const int   dy = static_cast<int>(dyFrac * screenH * scale);
				SendMouseMove(dx, dy);

				std::this_thread::sleep_for(kTickInterval);
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
