#pragma once

namespace VATS
{
	// Read-only probe (2026-08-25) for RE::NiAVObject::worldBound - see
	// GameOffsets.h for the full offset chain and the reasoning. Goal:
	// find out whether this gives a real, pose-current, creature-agnostic
	// "center of the actor" - Alexander's requirement for a redirect aim
	// point that works standing, kneeling, prone, and on non-humanoid
	// skeletons (robots, alien creatures) without any bone-name lookup.
	//
	// DOES NOT WRITE ANYTHING and is not wired into any aim/redirect logic
	// yet - Overlay.cpp's aim point (feet + fixed chest-height offset) is
	// unchanged. This only logs, so the two can be compared side by side
	// while testing different poses in-game before anything depends on it.
	class WorldBoundProbe
	{
	public:
		// Logs the locked target's worldBound.center/radius next to its
		// ref-origin (feet) position and the CURRENT fixed-offset aim
		// point, throttled to "log on meaningful change" (a few cm) rather
		// than every frame - same pattern as HealthReader.cpp's diagnostic,
		// to avoid the log-volume-affecting-timing problem this project
		// already hit once. Call once per frame for the Locked target
		// (Overlay.cpp) - safe no-op if the chain doesn't resolve (dead
		// end reads all SafeRead-guarded, degrades to nothing rather than
		// a crash).
		static void LogIfChanged(RE::Actor* a_actor);
	};
}
