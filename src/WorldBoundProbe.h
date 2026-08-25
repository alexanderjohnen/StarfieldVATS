#pragma once

namespace VATS
{
	// Provides RE::NiAVObject::worldBound as a pose- and creature-type-
	// agnostic "center of the actor" - see GameOffsets.h for the full
	// offset chain and the reasoning. Confirmed in-game 2026-08-25: the
	// center tracks a real death/collapse animation smoothly (dropping
	// from roughly torso height while standing to near-ground while
	// prone), while the old fixed chest-height-above-feet offset stays
	// constant regardless of pose.
	class WorldBoundProbe
	{
	public:
		// Returns the best available aim point for a_actor: worldBound's
		// center if the chain resolves and passes a basic sanity check
		// (see the .cpp), clamped so it never sits below a small floor
		// above a_feet.z (guards against a transient ragdoll-collapse
		// frame observed briefly reading below the actor's own feet).
		// Falls back to the old fixed offset (a_feet.z +
		// GameOffsets::kAimPointChestZ) if the chain doesn't resolve at
		// all - this function always returns something usable, never
		// fails. a_feet is the caller's already-read ref-origin position
		// (GameOffsets::kLocation), passed in rather than re-read here
		// since every call site already has it.
		[[nodiscard]] static RE::NiPoint3 GetAimPoint(RE::Actor* a_actor, const RE::NiPoint3& a_feet);

		// The bounding sphere's radius, i.e. roughly how big this actor is
		// in world units. Used to size the HUD target box so it frames the
		// target instead of staying a fixed number of pixels: the box used
		// to keep its size at any distance, which made it look like it grew
		// as the target shrank away (Alexander, 2026-08-25). Returns false
		// if the chain doesn't resolve, in which case the caller keeps its
		// fixed fallback size.
		[[nodiscard]] static bool GetBoundRadius(RE::Actor* a_actor, float& a_out);

		// Logs the locked target's worldBound.center/radius next to its
		// ref-origin (feet) position and the fixed-offset fallback,
		// throttled to "log on meaningful change" (a few cm) rather than
		// every frame - same pattern as HealthReader.cpp's diagnostic, to
		// avoid the log-volume-affecting-timing problem this project
		// already hit once. Call once per frame for the Locked target
		// (Overlay.cpp) - safe no-op if the chain doesn't resolve.
		static void LogIfChanged(RE::Actor* a_actor);
	};
}
