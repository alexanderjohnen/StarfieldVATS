#include "AimSpring.h"

#include "GameOffsets.h"
#include "Log.h"
#include "SafeMem.h"
#include "Settings.h"
#include "VATSController.h"
#include "WorldBoundProbe.h"

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

		// Published for the HUD tether. 0 = on target, 1 = at the release
		// angle. Plain atomic float: written here every tick, read once per
		// frame from the render thread, and a torn read would cost one
		// slightly wrong line width for one frame.
		std::atomic<float> g_tension{ 0.0f };

		constexpr float kRadToDeg = 57.29578f;

		// Is Starfield the window that will actually receive this input?
		//
		// SendInput goes to the OS, not to a process, so a pulse sent while
		// the game is in the background lands on whatever the player has in
		// front and shakes their real desktop cursor. Not hypothetical: the
		// calibration probe did exactly that on 2026-09-06 and Alexander
		// could not click anything until the game was killed. It also keeps
		// unfocused time out of the spring's own reasoning - an unfocused
		// game consumes no mouse input, so the view cannot move, and a
		// spring integrating against a view that cannot move would build up
		// a shove waiting to be delivered.
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

		[[nodiscard]] float Dot(const RE::NiPoint3& a, const RE::NiPoint3& b)
		{
			return a.x * b.x + a.y * b.y + a.z * b.z;
		}

		[[nodiscard]] bool Normalize(RE::NiPoint3& a_v)
		{
			const float len = std::sqrt(Dot(a_v, a_v));
			if (len < 1.0e-5f) {
				return false;
			}
			a_v.x /= len;
			a_v.y /= len;
			a_v.z /= len;
			return true;
		}

		struct AimError
		{
			float yawDeg{ 0.0f };    // + = target is right of the view
			float pitchDeg{ 0.0f };  // + = target is above the view
			float magnitudeDeg{ 0.0f };
		};

		// Angular error from the view axis to the target, split into the two
		// axes a mouse can move.
		//
		// ONE SOURCE FOR BOTH AXES AND BOTH SIGNS, which is the fix for the
		// jitter (2026-09-06, second attempt). The first version took the
		// magnitude from a vector angle and the sign from UI::WorldToScreen.
		// Those are two different computations of the same thing, and near
		// the boundary they disagree - so the sign flipped back and forth
		// between ticks while the magnitude stayed above the deadzone, and
		// the spring shoved left, right, left, right. Alexander felt it
		// immediately as wobble. Mixing a proven source for one half of a
		// quantity with a guessed source for the other half was the actual
		// mistake; the sign was only its most visible symptom.
		//
		// The camera basis is the one CameraProject uses and proves: row 1
		// is forward, row 0 is RIGHT and row 2 is UP - not by assumption
		// about Starfield's handedness, but because that projection places
		// the HUD marker on real targets, and it derives screen x directly
		// from dot(diff, row 0). Anything that lands the marker correctly
		// has the orientation right.
		//
		// Working in the camera's frame is also what makes third person
		// behave. The camera sits behind and beside the player there, so an
		// error measured against the PLAYER would disagree with what the
		// player sees on screen - and what they see is what they are aiming
		// with.
		[[nodiscard]] bool ComputeError(RE::Actor* a_target, AimError& a_out)
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			if (!camera || !a_target) {
				return false;
			}
			auto* cameraRoot = camera->cameraRoot.get();
			if (!cameraRoot) {
				return false;
			}

			const auto&        world = cameraRoot->world;
			const RE::NiPoint3 camPos = world.translate;

			RE::NiPoint3 fwd{ world.rotate.entry[1].x, world.rotate.entry[1].y, world.rotate.entry[1].z };
			RE::NiPoint3 right{ world.rotate.entry[0].x, world.rotate.entry[0].y, world.rotate.entry[0].z };
			RE::NiPoint3 up{ world.rotate.entry[2].x, world.rotate.entry[2].y, world.rotate.entry[2].z };
			if (!Normalize(fwd) || !Normalize(right) || !Normalize(up)) {
				return false;
			}

			// THE AIM POINT, not the actor's origin.
			//
			// The previous version read kLocation directly, on the reasoning
			// that a sideways-only spring does not care about height and
			// that GetAimPoint carries per-actor smoothing state shared with
			// the render thread and AimAssist. Both halves of that stopped
			// being true the moment the spring gained a vertical axis:
			// kLocation is an actor's origin, which is at their FEET, so the
			// spring pulled the view into the floor - Alexander screenshotted
			// it pointing under a seated NPC, and shots stopped landing
			// because they were being aimed at the furniture in front of the
			// feet rather than at the person.
			//
			// The decisive argument is not the height though, it is
			// agreement: the HUD tether is drawn to the MARKER, and the
			// marker is placed at the aim point. A spring pulling anywhere
			// else makes the indicator a liar - it would show a line to one
			// place while dragging the view to another. Whatever the marker
			// claims is where the pull has to go.
			//
			// The smoothing concern was over-cautious: that state is mutex
			// guarded and already has two callers by design, and a filter
			// shared by three consumers is a far smaller problem than a
			// spring and its own indicator disagreeing.
			RE::NiPoint3 feet{};
			if (!SafeRead(reinterpret_cast<const std::byte*>(a_target) + GameOffsets::kLocation, &feet, sizeof(feet))) {
				return false;
			}
			const RE::NiPoint3 pos = WorldBoundProbe::GetAimPoint(a_target, feet);

			RE::NiPoint3 diff{ pos.x - camPos.x, pos.y - camPos.y, pos.z - camPos.z };
			if (!Normalize(diff)) {
				return false;
			}

			const float f = Dot(diff, fwd);
			const float r = Dot(diff, right);
			const float u = Dot(diff, up);

			// atan2 rather than asin so that a target BEHIND the camera
			// reads as a large angle instead of folding back toward zero.
			// Behind is exactly where the spring should pull hardest, so
			// getting it wrong there would be getting it wrong where it
			// matters most.
			a_out.yawDeg = std::atan2(r, f) * kRadToDeg;
			a_out.pitchDeg = std::atan2(u, std::sqrt(f * f + r * r)) * kRadToDeg;
			a_out.magnitudeDeg = std::acos(std::clamp(f, -1.0f, 1.0f)) * kRadToDeg;
			return true;
		}

		void ThreadProc(std::stop_token a_stop)
		{
			using namespace std::chrono;

			// 60 Hz, back down from the 250 the previous attempt raised it
			// to. That change rested on a wrong diagnosis and made this
			// worse, not better.
			//
			// Fourth attempt at the jitter, and Alexander's description is
			// what finally identifies it: a CONSTANT right-left-right-left,
			// not an uneven stutter. Granularity produces uneven stepping. A
			// steady alternation is an OSCILLATION - something regularly
			// overshoots and corrects back.
			//
			// This is a control loop with latency. We read the error, inject
			// a correction, and that correction does not reach the camera
			// matrix for a frame or two. Until it does we keep computing
			// against the STALE error and keep injecting, so the total in
			// flight exceeds what was needed, the view sails past the
			// target, and the next tick repeats it in reverse. Raising the
			// tick rate packs more stale corrections into the same latency
			// window, which is exactly why 250 Hz was worse.
			//
			// It also explains why the first three attempts could not have
			// worked: all three looked for a wrong DIRECTION. The direction
			// was right. The GAIN was wrong.
			constexpr auto kTick = milliseconds(16);

			// Ceiling on any single pulse, so a frame hitch cannot deliver a
			// lurch in one packet.
			constexpr float kMaxStepDeg = 1.5f;

			// The actual fix: never inject more than this fraction of the
			// REMAINING error in one tick.
			//
			// With a step bounded by a fraction of the error, the loop
			// converges rather than rings even while corrections are still
			// in flight - after two ticks of latency at 25% each, only about
			// 44% of the original error has been injected, so overshoot is
			// impossible by construction rather than by choosing a gentle
			// enough speed. It also makes fSpringMaxDegPerSec safe to raise:
			// that setting now governs how fast the spring pulls when far
			// from the target, and this governs whether it can overshoot
			// when near it. Those were the same number before, which is why
			// making the spring strong enough to feel also made it ring.
			constexpr float kMaxErrorFraction = 0.25f;

			auto lastTick = steady_clock::now();
			auto beyondReleaseSince = steady_clock::time_point{};
			bool beyondRelease = false;

			// Fractional mouse units left over from the previous tick.
			// SendInput takes integers, and a gentle pull at 60Hz is often
			// well under one unit per tick - truncating every tick would
			// floor the spring to zero at exactly the low strengths it most
			// needs to feel smooth at.
			float carryX = 0.0f;
			float carryY = 0.0f;

			while (!a_stop.stop_requested()) {
				std::this_thread::sleep_for(kTick);

				const auto  now = steady_clock::now();
				const float dt = std::min(0.1f, duration<float>(now - lastTick).count());
				lastTick = now;

				const auto state = Controller::Get().GetOverlayState();
				if (state.mode != VATSMode::kLocked || !state.actor || !GameHasFocus()) {
					beyondRelease = false;
					carryX = carryY = 0.0f;
					g_tension.store(0.0f, std::memory_order_relaxed);
					continue;
				}

				const auto& settings = Settings::Get();
				if (!settings.aimSpringEnabled || settings.mouseDegPerUnit <= 0.0f) {
					g_tension.store(0.0f, std::memory_order_relaxed);
					continue;
				}

				AimError err{};
				if (!ComputeError(state.actor.get(), err)) {
					continue;
				}

				const float span = std::max(1.0f, settings.springReleaseDeg - settings.springDeadzoneDeg);
				const float tension = std::clamp((err.magnitudeDeg - settings.springDeadzoneDeg) / span, 0.0f, 1.0f);
				g_tension.store(tension, std::memory_order_relaxed);

				// Past the release angle the mode ends rather than the wall
				// getting harder - but only after it has stayed there for
				// iSpringReleaseGraceMs. Without the grace a glance sideways
				// mid-fight drops the lock, which reads as flakiness rather
				// than as a rule.
				if (err.magnitudeDeg >= settings.springReleaseDeg) {
					if (!beyondRelease) {
						beyondRelease = true;
						beyondReleaseSince = now;
					}
					else if (duration_cast<milliseconds>(now - beyondReleaseSince).count() >= settings.springReleaseGraceMs) {
						VATS_LOG("[spring] released - {:.1f} deg off target for {} ms", err.magnitudeDeg, settings.springReleaseGraceMs);
						Controller::Get().ForceOff("turned away from target");
						beyondRelease = false;
						carryX = carryY = 0.0f;
						g_tension.store(0.0f, std::memory_order_relaxed);
						continue;
					}
				}
				else {
					beyondRelease = false;
				}

				// Inside the deadzone the spring does nothing. Without it the
				// view is glued to the target and the player cannot make
				// their own small corrections - the exact "I lost control of
				// the camera" complaint that killed the first camera-steering
				// design in August.
				if (err.magnitudeDeg <= settings.springDeadzoneDeg) {
					carryX = carryY = 0.0f;
					continue;
				}

				// Restoring force, proportional to how far it is stretched -
				// which is what makes it a spring rather than a brake. A
				// brake only slows you leaving; this also carries you back
				// once you stop pulling, which is what Alexander asked for.
				//
				// Applied along the actual error direction rather than per
				// axis independently, so the pull points AT the target from
				// wherever the view is - the "spring stretched between the
				// weapon and the target" that motivates the whole feature.
				const float degPerSec = settings.springMaxDegPerSec * tension;
				const float step = std::min({ kMaxStepDeg,
					degPerSec * dt,
					err.magnitudeDeg * kMaxErrorFraction });
				const float scale = step / std::max(1.0e-3f, err.magnitudeDeg);

				const float wantYawDeg = err.yawDeg * scale;
				const float wantPitchDeg = err.pitchDeg * scale;

				const float wantX = wantYawDeg / settings.mouseDegPerUnit + carryX;
				// Negative: on a mouse, moving DOWN (+dy) looks down, so
				// looking UP at a target above the view needs a negative dy.
				const float wantY = -wantPitchDeg / settings.mouseDegPerUnitY + carryY;

				const int unitsX = static_cast<int>(wantX);
				const int unitsY = static_cast<int>(wantY);
				carryX = wantX - static_cast<float>(unitsX);
				carryY = wantY - static_cast<float>(unitsY);

				SendMouseMove(unitsX, unitsY);

				VATS_TRACE("[spring] err {:.1f} deg (yaw {:+.1f}, pitch {:+.1f}) | tension {:.2f} | {:+.2f} deg -> ({}, {})",
					err.magnitudeDeg, err.yawDeg, err.pitchDeg, tension, step, unitsX, unitsY);
			}
		}
	}

	float AimSpring::GetTension()
	{
		return g_tension.load(std::memory_order_relaxed);
	}

	void AimSpring::Start()
	{
		if (g_running.exchange(true)) {
			return;
		}
		// Started unconditionally and gated per tick rather than at startup,
		// so bAimSpringEnabled survives a settings reload without a restart.
		VATS_LOG("[spring] started (enabled={}, deadzone={} deg, release={} deg, max={} deg/s, {}/{} deg/unit)",
			Settings::Get().aimSpringEnabled, Settings::Get().springDeadzoneDeg,
			Settings::Get().springReleaseDeg, Settings::Get().springMaxDegPerSec,
			Settings::Get().mouseDegPerUnit, Settings::Get().mouseDegPerUnitY);
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
