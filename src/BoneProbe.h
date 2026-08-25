#pragma once

namespace VATS
{
	// Read-only probe (2026-08-25) for a named skeleton bone as a
	// candidate "real center" - see WorldBoundProbe.h/GameOffsets.h for
	// why the bounding-sphere approach turned out insufficient (skewed low
	// by a wide/crouching pose, screenshot-confirmed by Alexander: the
	// target box sat at hip height on a lunging enemy). A specific bone
	// (Bethesda skeletons commonly carry a "COM"/center-of-mass joint, or
	// a spine/chest bone, used internally by ragdoll physics) is a single
	// point in the animated skeleton rather than a geometric envelope over
	// everything - it should move with the pose the same way worldBound
	// does, without being pulled around by spread limbs or held weapon
	// geometry.
	//
	// DOES NOT WRITE ANYTHING and is not wired into any aim/redirect logic
	// - purely diagnostic, same status worldBound was in before its own
	// probe confirmed (and then, in this specific pose, disproved) it.
	class BoneProbe
	{
	public:
		// Walks a_actor's skeleton (from its data3D root, bounded depth/
		// node count) looking for any node whose name contains one of a
		// short candidate list ("COM", "Spine", "Chest", "Torso",
		// "Pelvis", "Hips", "Root") - deliberately loose substring
		// matching rather than exact names, since Starfield's actual
		// naming convention for these joints is unconfirmed and may not
		// match Skyrim/FO4's bracketed style ("NPC COM [COM ]"). Logs
		// every match found (name + world position, relative to feet and
		// to WorldBoundProbe's center) once per second per locked target,
		// so multiple candidate bones can be compared side by side across
		// different poses before picking one. Also logs a one-line
		// "no candidates matched" fallback note if the walk completed but
		// found nothing, so a truly bone-less/differently-named skeleton
		// (robot, alien) is distinguishable from the chain simply failing
		// to resolve at all.
		static void LogIfChanged(RE::Actor* a_actor);
	};
}
