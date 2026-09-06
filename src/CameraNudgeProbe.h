#pragma once

namespace VATS
{
	// TEMPORARY (2026-09-06). Answers one question before any "magnet"
	// design gets built: can this mod turn the player's view SMOOTHLY, and
	// how much turn does one unit of synthetic mouse movement buy?
	//
	// Why this is worth measuring rather than assuming. The project already
	// shipped a camera-steering AimAssist once, on SendInput/
	// MOUSEEVENTF_MOVE, and removed it on 2026-08-22 - but for DESIGN
	// reasons (Alexander: the weapon and camera must never visibly snap),
	// not because it failed. So the mechanism is likely sound and the open
	// question is a different one: that old code converted "degrees I want"
	// into raw mouse units through fMouseSensitivityScale, a fudge factor
	// its own comment admits was never derived. A magnet that has to apply
	// a gentle, angle-dependent pull needs the real conversion, or every
	// strength constant on top of it is guesswork stacked on guesswork.
	//
	// Note the asymmetry this rests on, which is established rather than
	// hoped for: INJECTING input works in this game (the scanner-close
	// keypress is built on it and is proven), while SUPPRESSING input does
	// not (four failed attempts at blocking ADS, and the back key is
	// unreliable for the same reason). A magnet only ever needs to inject.
	//
	// Writes nothing into engine memory. It sends OS-level mouse movement
	// and READS the resulting player angle back through the same guarded
	// path everything else here uses, which is what makes it cheap to run
	// and impossible to crash on.
	//
	// Off unless bProbeCameraNudge=1. Delete once the question is settled.
	class CameraNudgeProbe
	{
	public:
		// Starts the sampling thread if the setting is on. Called once at
		// plugin init; a no-op otherwise.
		static void Start();
		static void Stop();
	};
}
