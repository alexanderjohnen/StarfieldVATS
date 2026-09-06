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
	// BOTH AXES since 2026-09-06. It began horizontal-only, because the
	// calibration probe measured yaw. Alexander tried it in third person
	// and the missing half was immediately obvious: the spring belongs
	// between the weapon and the target, and a target above or below is
	// just as far out of line as one to the side. Vertical uses its own
	// conversion factor (fMouseDegPerUnitY), defaulted to the measured
	// horizontal one but separate, since nothing has verified that
	// Starfield turns pitch and yaw at the same rate per mouse unit.
	//
	// The pull is applied along the actual error direction rather than per
	// axis, so it points AT the target from wherever the view is - the
	// spring stretched between weapon and target, not two independent
	// rubber bands.
	class AimSpring
	{
	public:
		// Current stretch, 0 (on target) to 1 (at the release angle). Read
		// once per frame by the HUD tether, which is the only thing that
		// tells the player how far out they are and how far they may go -
		// without it the release arrives as a surprise, which Alexander
		// reported as "too easy to break out of".
		[[nodiscard]] static float GetTension();

		static void Start();
		static void Stop();
	};
}
