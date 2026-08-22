#pragma once

namespace VATS
{
	// Diagnostic-only (2026-08-22): scans the player's cell for a freshly-
	// spawned Projectile reference and logs what it finds. Doesn't modify
	// anything yet - the goal right now is just proving we can reliably
	// *locate* the projectile the player just fired, before ever risking
	// a write to its RE::Projectile::velocity/movementDirection fields
	// (see Projectile.h - both plain, directly-writable NiPoint3 fields,
	// no engine function call needed for the actual redirect once we have
	// a pointer).
	//
	// Originally planned to trigger off RE::WeaponFiredEvent, but its
	// Address Library ID is 0 (hard-crashed on load, see
	// commonlibsf-unmapped-ids memory) - and the natural fallback,
	// RE::BSAnimationGraphEvent (what Fallout 76 VATS - F4SE itself uses
	// via Papyrus's RegisterForAnimationEvent), is only forward-declared
	// in CommonLibSF for Starfield, no field layout available. Instead,
	// this is called directly from AimAssist.cpp's own mouse-hook click
	// detection (already proven reliable all session) shortly after a
	// real fire click - no native event needed at all.
	class ProjectileTracker
	{
	public:
		// Call shortly after detecting a real fire click. Scans the
		// player's cell for projectile-type references and logs each
		// one's formType/position/age/distanceMoved/velocity/
		// shooterHandle. Safe to call from any thread (SafeRead-guarded
		// throughout, no engine function calls).
		static void ProbeAfterFire();
	};
}
