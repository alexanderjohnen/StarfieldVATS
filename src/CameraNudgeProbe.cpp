#include "CameraNudgeProbe.h"

#include "GameOffsets.h"
#include "Log.h"
#include "SafeMem.h"
#include "Settings.h"
#include "VATSController.h"

#include "RE/P/PlayerCharacter.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

#include <Windows.h>

namespace VATS
{
	namespace
	{
		std::jthread      g_thread;
		std::atomic<bool> g_running{ false };

		// The player's facing angle. data.angle sits 0x0C BEFORE
		// data.location in OBJ_REFR (angle at 00, location at 0C), and
		// kLocation is an offset this project has already confirmed
		// empirically - the cone scan reads positions through it and picks
		// the right actors. So this is derived from a verified number
		// rather than taken on the header's word, which for this codebase
		// is the difference that matters.
		//
		// .z is the yaw (heading). Only yaw is sampled: a magnet is a
		// horizontal idea, and pitch on the player may well live somewhere
		// else entirely in first person - a question worth its own probe if
		// it ever matters, not worth guessing at inside this one.
		constexpr std::size_t kAngleZ = GameOffsets::kLocation - 0x0C + 0x08;

		[[nodiscard]] bool ReadPlayerYaw(float& a_out)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return false;
			}
			return SafeRead(reinterpret_cast<const std::byte*>(player) + kAngleZ, &a_out, sizeof(a_out));
		}

		// Is Starfield the window that will actually receive this input?
		//
		// SendInput goes to the OS, not to a process - so a pulse sent while
		// the game is in the background lands on whatever the player has in
		// front instead, and their real desktop cursor twitches. Alexander
		// hit exactly that on 2026-09-06 ("meine Maus hat nen Tremor"), and
		// the log shows the other half of it: 166 samples reading
		// 2.3008 -> 2.3008, the view not moving at all because Starfield was
		// not consuming input.
		//
		// So this guard does two jobs. It keeps the mod out of the player's
		// desktop, and it keeps unfocused frames out of the measurement -
		// they are not evidence that a nudge failed, only that nobody was
		// listening. Anything built on this later needs it just as much.
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

		// OS-level synthetic mouse movement - the same mechanism as the
		// scanner-close keypress, which is proven to reach this game. No
		// engine call, no engine write.
		void SendMouseMove(int a_dx)
		{
			INPUT input{};
			input.type = INPUT_MOUSE;
			input.mi.dx = a_dx;
			input.mi.dy = 0;
			input.mi.dwFlags = MOUSEEVENTF_MOVE;
			::SendInput(1, &input, sizeof(INPUT));
		}

		// Smallest signed difference between two yaw readings, in radians,
		// wrapped into [-pi, pi]. Without this a sweep across the 0/2pi
		// seam would read as a full turn in the wrong direction and poison
		// the average this probe exists to produce.
		[[nodiscard]] float YawDelta(float a_from, float a_to)
		{
			constexpr float kPi = 3.14159265f;
			float           d = a_to - a_from;
			while (d > kPi) {
				d -= 2.0f * kPi;
			}
			while (d < -kPi) {
				d += 2.0f * kPi;
			}
			return d;
		}

		void ThreadProc(std::stop_token a_stop)
		{
			using namespace std::chrono;

			// One pulse, then a gap to read the result. The gap matters:
			// the engine consumes mouse input on its own schedule, so
			// sampling immediately would measure our own timing rather than
			// the game's response.
			constexpr auto kPulseInterval = milliseconds(100);

			// Deliberately a FIXED pulse rather than a pull toward the
			// target. This probe is a ruler, not a feature: a constant,
			// known input is what makes the output a calibration figure.
			// Steering toward something would mix the conversion factor
			// with an error term and measure neither cleanly.
			constexpr int kPulseUnits = 40;

			int   samples = 0;
			float sumDegPerUnit = 0.0f;

			while (!a_stop.stop_requested()) {
				if (Controller::Get().GetMode() != VATSMode::kLocked || !GameHasFocus()) {
					// Idle cheaply while there is nothing to measure, and
					// forget the running average - a session's worth of
					// samples from different locks tells us less than a
					// clean run does.
					samples = 0;
					sumDegPerUnit = 0.0f;
					std::this_thread::sleep_for(milliseconds(250));
					continue;
				}

				float before = 0.0f;
				if (!ReadPlayerYaw(before)) {
					VATS_WARN("[nudge] player yaw not readable - probe cannot measure");
					std::this_thread::sleep_for(seconds(1));
					continue;
				}

				SendMouseMove(kPulseUnits);
				std::this_thread::sleep_for(kPulseInterval);

				float after = 0.0f;
				if (!ReadPlayerYaw(after)) {
					continue;
				}

				const float deltaRad = YawDelta(before, after);
				const float deltaDeg = deltaRad * 57.29578f;
				const float degPerUnit = deltaDeg / static_cast<float>(kPulseUnits);

				++samples;
				sumDegPerUnit += degPerUnit;

				// Four numbers per line, and between them they answer the
				// whole question:
				//   deltaDeg near zero      -> injected movement does not
				//                              reach the camera at all, and
				//                              the magnet idea is closed
				//   deltaDeg steady         -> smooth and proportional, so a
				//                              gentle pull is buildable and
				//                              degPerUnit is its scale
				//   deltaDeg wildly varying -> something else is fighting
				//                              us, or the read is not really
				//                              the view angle
				VATS_LOG("[nudge] pulse={} units | yaw {:.4f} -> {:.4f} rad | turned {:+.3f} deg | {:.5f} deg/unit | avg {:.5f} over {} samples",
					kPulseUnits, before, after, deltaDeg, degPerUnit,
					sumDegPerUnit / static_cast<float>(samples), samples);

				// Undo it. This is a measurement, not a feature - leaving
				// the player's view rotated because a probe ran would be a
				// gameplay change nobody asked for, and an accumulating
				// drift would also make every later sample start from a
				// different place.
				SendMouseMove(-kPulseUnits);
				std::this_thread::sleep_for(kPulseInterval);
			}
		}
	}

	void CameraNudgeProbe::Start()
	{
		if (!Settings::Get().probeCameraNudge || g_running.exchange(true)) {
			return;
		}
		VATS_LOG("[nudge] camera-nudge probe ARMED - pulses only while Locked");
		g_thread = std::jthread(ThreadProc);
	}

	void CameraNudgeProbe::Stop()
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
