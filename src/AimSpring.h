#pragma once

namespace VATS
{
	// The COARSE stage. Everything else in this mod is the fine stage.
	//
	// The finding this exists for (docs/FINDINGS.md): FO76, the VATS76 mod
	// for Fallout 4, and Starfield's own ship combat all work in two
	// stages - a coarse orientation, then a fine correction - and this
	// project only ever had the fine one. Nothing constrained where the
	// player looked, so the in-flight redirect had to be able to cover any
	// angle up to a full reversal, and THAT is where fLockedProjectileSpeed
	// = 80 m/s comes from. The slowdown is the price of a missing stage,
	// not a property of the mechanic.
	//
	// FO76 supplies the coarse stage by taking the camera over. That is
	// rejected here and has been since 2026-08-22: the weapon and camera
	// must never visibly snap. Starfield's ship combat shows the other
	// arrangement - the PLAYER supplies the coarse stage - and that is the
	// one this follows.
	//
	// A SPRING, NOT A MAGNET, and the direction matters. Resistance GROWS
	// with the angle: the further the view turns off the target, the
	// harder it pulls back. It never becomes infinite, and past
	// fSpringReleaseDeg the lock simply ENDS rather than the wall getting
	// harder.
	//
	// Claude proposed the opposite first - strong on target, fading with
	// angle, i.e. ordinary console aim assist - and Alexander rejected it
	// for a reason worth keeping: a tolerance zone whose only consequence
	// is "then it does not help" is a RULE the player has to learn, while
	// resistance is a SENSATION that needs no explanation. The fading
	// version is the more conventional design and will keep looking like
	// the obvious choice. It is not the one that was chosen.
	//
	// Ending the lock rather than hardening the wall is the same shape as
	// the ADS decision: when the player unmistakably signals something
	// else, give way instead of fighting. It also makes the backwards shot
	// impossible without ever needing an invisible rule for it - at that
	// angle the mode is already over.
	//
	// Mechanism: OS-level synthetic mouse movement (SendInput), the same
	// mechanism as the scanner-close keypress. This rests on an asymmetry
	// established three times over here - INJECTING input reaches this
	// game, SUPPRESSING it does not (four failed attempts at blocking ADS,
	// plus the unreliable back key). A spring only ever injects. No engine
	// calls, no engine writes; the only reads are the guarded ones the
	// rest of this project already uses.
	//
	// Horizontal only. The probe that calibrated this measured yaw, and
	// pitch on the player may well live somewhere else entirely in first
	// person - a question for its own probe rather than a guess inside
	// this one.
	class AimSpring
	{
	public:
		static void Start();
		static void Stop();
	};
}
