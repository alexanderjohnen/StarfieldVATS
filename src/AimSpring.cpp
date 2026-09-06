#include "AimSpring.h"

#include "GameOffsets.h"
#include "Log.h"
#include "SafeMem.h"
#include "Settings.h"
#include "VATSController.h"
#include "UI/CameraProject.h"

#include "RE/A/Actor.h"
#include "RE/P/PlayerCamera.h"
#include "RE/P/PlayerCharacter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

// Windows.h defines max/min as macros, which eats every std::max and
// std::min in this file - the error it produces names the std call rather
// than the macro, so it reads like a broken standard library. This has to
// sit before the include, not next to it.
#define NOMINMAX
#include <Windows.h>

namespace VATS
{
	namespace
	{
		std::jthread      g_thread;
		std::atomic<bool> g_running{ false };

		constexpr float kPi = 3.14159265f;
		constexpr float kRadToDeg = 57.29578f;

		// Is Starfield the window that will actually receive this input?
		//
		// SendInput goes to the OS, not to a process, so a pulse sent while
		// the game is in the background lands on whatever the player has in
		// front and shakes their real desktop cursor. That is not a
		// hypothetical - the calibration probe did exactly this on
		// 2026-09-06 and Alexander could not click anything until the game
		// was killed. It also keeps unfocused time out of the spring's own
		// reasoning: an unfocused game consumes no mouse input, so the view
		// cannot move, and a spring that kept integrating against a view
		// that cannot move would build up a shove waiting to be delivered.
		[[nodiscard]] bool GameHasFocus()
		{
			const HWND fg = ::GetForegroundWindow();
			if (!fg) {
				return false;
			}
			DWORD pid = 0;
			::GetWindowThreadProcessId(fg, &pid);
			return pid == ::GetCurrentProcessId();
		}

		void SendMouseMove(int a_dx)
		{
			if (a_dx == 0) {
				return;
			}
			INPUT input{};
			input.type = INPUT_MOUSE;
			input.mi.dx = a_dx;
			input.mi.dy = 0;
			input.mi.dwFlags = MOUSEEVENTF_MOVE;
			::SendInput(1, &input, sizeof(INPUT));
		}

		// Signed horizontal angle from where the camera looks to where the
		// target is, in degrees. Positive means the target is to the RIGHT,
		// i.e. the direction a positive mouse dx turns the view.
		//
		// THE SIGN COMES FROM THE PROJECTION, NOT FROM THE CROSS PRODUCT,
		// and that is the fix for the first version of this file (2026-09-06).
		// That one took the sign from the 2D cross product of the forward
		// and to-target vectors, which silently assumes a handedness for
		// Starfield's world axes that was never verified here. It was
		// backwards: Alexander reported the camera being pushed AWAY from
		// the target, and the log agreed - every release fired at 83-91
		// degrees rather than creeping past the 55 degree threshold, which
		// is what being accelerated outwards looks like rather than being
		// resisted.
		//
		// UI::WorldToScreen is the proven alternative. It returns normalized
		// coordinates with the origin top-left, it has placed the HUD marker
		// on real targets since the first targeting test, and x < 0.5 versus
		// x > 0.5 is left versus right with no handedness assumption of our
		// own. The cross product is still computed and logged beside it, so
		// one run settles what the world axes actually do - a fact worth
		// having rather than routing around.
		//
		// The MAGNITUDE still comes from the vector angle, because the
		// projection cannot express a target behind the camera and that is
		// exactly where a spring needs its largest value.
		//
		// Horizontal only: both vectors are flattened onto the XY plane
		// before the comparison. A target above or below contributes
		// nothing, which is correct for a spring that only ever pushes
		// sideways - otherwise a target at the player's feet would read as
		// a large error and the spring would fight a direction it cannot
		// move in.
		[[nodiscard]] bool YawErrorToTarget(RE::Actor* a_target, float& a_outDeg)
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			if (!camera || !a_target) {
				return false;
			}
			auto* cameraRoot = camera->cameraRoot.get();
			if (!cameraRoot) {
				return false;
			}

			const auto& world = cameraRoot->world;
			const RE::NiPoint3 camPos = world.translate;

			// Row 1 is the view direction here, empirically verified
			// 2026-08-22 - see the long note in Targeting.cpp. Row 0 is NOT
			// forward, it behaves like a lateral axis.
			float fx = world.rotate.entry[1].x;
			float fy = world.rotate.entry[1].y;
			const float fLen = std::sqrt(fx * fx + fy * fy);
			if (fLen < 1.0e-4f) {
				return false;  // looking straight up or down - no yaw to speak of
			}
			fx /= fLen;
			fy /= fLen;

			// The actor's own origin, deliberately NOT GetAimPoint.
			//
			// The aim point exists to lift the target vertically onto the
			// chest, which is worth nothing to a spring that only pushes
			// sideways. And it carries per-actor smoothing state, already
			// shared between the render thread and AimAssist's steering
			// thread; adding a third caller at 60Hz would perturb a filter
			// two other consumers depend on, to obtain a horizontal
			// direction that the raw position gives just as well.
			RE::NiPoint3 pos{};
			if (!SafeRead(reinterpret_cast<const std::byte*>(a_target) + GameOffsets::kLocation, &pos, sizeof(pos))) {
				return false;
			}

			float tx = pos.x - camPos.x;
			float ty = pos.y - camPos.y;
			const float tLen = std::sqrt(tx * tx + ty * ty);
			if (tLen < 1.0e-3f) {
				return false;  // standing inside the target - no meaningful direction
			}
			tx /= tLen;
			ty /= tLen;

			// Magnitude from the vectors: atan2 of the 2D cross and dot
			// products stays well-behaved near 180 degrees, where an acos
			// would lose all its precision - and 180 degrees is precisely
			// where a spring is doing its most important work.
			const float dot = fx * tx + fy * ty;
			const float cross = fx * ty - fy * tx;
			const float magnitude = std::abs(std::atan2(cross, dot) * kRadToDeg);

			// Sign from the projection, which is proven on real targets.
			float screenX = 0.0f;
			float screenY = 0.0f;
			if (UI::WorldToScreen(pos, screenX, screenY)) {
				a_outDeg = (screenX >= 0.5f) ? magnitude : -magnitude;
			}
			else {
				// Behind the camera: the projection has nothing to say, and
				// which way to turn is genuinely arbitrary there - both ways
				// are equally far. Keep the cross-product sign so the pull
				// stays consistent instead of flapping while the target sits
				// behind the player.
				a_outDeg = (cross >= 0.0f) ? magnitude : -magnitude;
			}

			VATS_TRACE("[spring] err {:+.2f} deg | screenX {:.3f} | crossSign {} | agree={}",
				a_outDeg, screenX, cross >= 0.0f ? "+" : "-",
				((screenX >= 0.5f) == (cross >= 0.0f)) ? "yes" : "NO");
			return true;
		}

		void ThreadProc(std::stop_token a_stop)
		{
			using namespace std::chrono;

			// 60 Hz. Fast enough that the pull reads as continuous
			// resistance rather than a series of shoves, and the work per
			// tick is two guarded reads plus arithmetic.
			constexpr auto kTick = milliseconds(16);

			auto lastTick = steady_clock::now();
			auto beyondReleaseSince = steady_clock::time_point{};
			bool beyondRelease = false;

			// Fractional mouse units left over from the previous tick.
			// SendInput takes integers, and at 60 Hz a gentle pull is often
			// well under one unit per tick - truncating every tick would
			// silently floor the whole spring to zero at exactly the low
			// strengths it is supposed to feel smooth at.
			float carry = 0.0f;

			while (!a_stop.stop_requested()) {
				std::this_thread::sleep_for(kTick);

				const auto  now = steady_clock::now();
				const float dt = std::min(0.1f, duration<float>(now - lastTick).count());
				lastTick = now;

				const auto state = Controller::Get().GetOverlayState();
				if (state.mode != VATSMode::kLocked || !state.actor || !GameHasFocus()) {
					beyondRelease = false;
					carry = 0.0f;
					continue;
				}

				const auto& settings = Settings::Get();
				if (!settings.aimSpringEnabled || settings.mouseDegPerUnit <= 0.0f) {
					continue;
				}

				float errDeg = 0.0f;
				if (!YawErrorToTarget(state.actor.get(), errDeg)) {
					continue;
				}
				const float absErr = std::abs(errDeg);

				// Past the release angle the mode ends rather than the wall
				// getting harder - but only after it has stayed past it for
				// iSpringReleaseGraceMs. Without the grace a glance sideways
				// during a fight would drop the lock, and the player would
				// read that as the mod being flaky rather than as a rule.
				if (absErr >= settings.springReleaseDeg) {
					if (!beyondRelease) {
						beyondRelease = true;
						beyondReleaseSince = now;
					}
					else if (duration_cast<milliseconds>(now - beyondReleaseSince).count() >= settings.springReleaseGraceMs) {
						VATS_LOG("[spring] released - view {:.1f} deg off target for {} ms", absErr, settings.springReleaseGraceMs);
						Controller::Get().ForceOff("turned away from target");
						beyondRelease = false;
						carry = 0.0f;
					}
					// Keep pulling at full strength while the grace runs.
					// Letting go here would make the last moments before a
					// release feel loose, which is the opposite of what the
					// release is meant to communicate.
				}
				else {
					beyondRelease = false;
				}

				// Inside the deadzone the spring does nothing at all. This
				// is not a tuning nicety: without it the view would be
				// glued to the target and the player could not make their
				// own small corrections, which is the exact "I lost control
				// of the camera" complaint that killed the first
				// camera-steering design.
				if (absErr <= settings.springDeadzoneDeg) {
					carry = 0.0f;
					continue;
				}

				// Linear from the deadzone up to the release angle, then
				// capped. Linear on purpose: the shape is going to be tuned
				// by feel, and a curve with a second constant in it would
				// make "it feels wrong" ambiguous between the two.
				const float span = std::max(1.0f, settings.springReleaseDeg - settings.springDeadzoneDeg);
				const float t = std::clamp((absErr - settings.springDeadzoneDeg) / span, 0.0f, 1.0f);
				const float degPerSec = settings.springMaxDegPerSec * t;

				// Toward the target, hence the sign of the error.
				const float wantDeg = degPerSec * dt * (errDeg > 0.0f ? 1.0f : -1.0f);
				const float wantUnits = wantDeg / settings.mouseDegPerUnit + carry;
				const int   units = static_cast<int>(wantUnits);
				carry = wantUnits - static_cast<float>(units);

				SendMouseMove(units);

				VATS_TRACE("[spring] err {:+.2f} deg | strength {:.2f} | {:+.3f} deg -> {} units (carry {:+.2f})",
					errDeg, t, wantDeg, units, carry);
			}
		}
	}

	void AimSpring::Start()
	{
		if (g_running.exchange(true)) {
			return;
		}
		// Started unconditionally and gated per tick instead of at startup,
		// so bAimSpringEnabled can be flipped by a settings reload without
		// a restart. The thread costs one wakeup per frame while idle.
		VATS_LOG("[spring] started (enabled={}, deadzone={} deg, release={} deg, max={} deg/s, {} deg/unit)",
			Settings::Get().aimSpringEnabled, Settings::Get().springDeadzoneDeg,
			Settings::Get().springReleaseDeg, Settings::Get().springMaxDegPerSec,
			Settings::Get().mouseDegPerUnit);
		g_thread = std::jthread(ThreadProc);
	}

	void AimSpring::Stop()
	{
		if (!g_running.exchange(false)) {
			return;
		}
		if (g_thread.joinable()) {
			g_thread.request_stop();
			g_thread.join();
		}
	}
}
