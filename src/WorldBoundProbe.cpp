#include "WorldBoundProbe.h"

#include "GameOffsets.h"
#include "SafeMem.h"
#include "Settings.h"

#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace VATS
{
	namespace
	{
		template <class T>
		[[nodiscard]] bool Read(const void* a_base, std::size_t a_off, T& a_out)
		{
			return SafeRead(static_cast<const std::byte*>(a_base) + a_off, &a_out, sizeof(T));
		}

		// Shared by LogIfChanged and GetAimPoint - the offset-chain walk
		// itself, no interpretation/clamping. See GameOffsets.h for the
		// chain and its unconfirmed-offset caveats.
		[[nodiscard]] bool TryReadWorldBound(RE::Actor* a_actor, RE::NiBound& a_out)
		{
			if (!a_actor) {
				return false;
			}
			std::uint64_t loadedData = 0;
			if (!Read(a_actor, GameOffsets::kActorLoadedData, loadedData) || !loadedData) {
				return false;
			}
			std::uint64_t data3D = 0;
			if (!Read(reinterpret_cast<const void*>(loadedData), GameOffsets::kLoadedRefData3D, data3D) || !data3D) {
				return false;
			}
			if (!Read(reinterpret_cast<const void*>(data3D), GameOffsets::kNiAVObjectWorldBound, a_out)) {
				return false;
			}
			// Plausibility check (2026-08-25): the chain can resolve to
			// non-null pointers that don't actually point at a live,
			// properly-initialised NiAVObject (e.g. a brief window during
			// spawn/despawn - the same class of "reads succeed, data is
			// garbage" risk this project hit with recycled projectile
			// pointers earlier today). A real worldBound radius has
			// consistently measured ~0.9-1.2 in testing; reject anything
			// wildly outside a generous band rather than trust a read that
			// merely didn't fault.
			if (!(a_out.radius > 0.05f && a_out.radius < 10.0f)) {
				return false;
			}
			return true;
		}

		// Logged again once the center has moved at least this far since
		// the last log line for this actor - filters per-frame animation
		// jitter (breathing, weapon sway) while still catching a real pose
		// change (standing<->crouching is typically tens of centimetres).
		constexpr float kLogMoveThreshold = 0.05f;

		// GetAimPoint's safety floor (2026-08-25): testing showed a
		// transient ragdoll-collapse frame where the raw center read
		// slightly BELOW the actor's own feet - physically plausible for a
		// falling body for one frame, but not something to ever aim a
		// redirected round at (risks aiming into/under the floor). Clamps
		// z only, never x/y, so the real horizontal body-center position
		// (which can differ meaningfully from the feet position during a
		// fall - also observed in testing) is preserved.
		constexpr float kMinAimPointAboveFeet = 0.1f;

		[[nodiscard]] float Distance(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
		{
			const float dx = a_a.x - a_b.x;
			const float dy = a_a.y - a_b.y;
			const float dz = a_a.z - a_b.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}
	}

	RE::NiPoint3 WorldBoundProbe::GetAimPoint(RE::Actor* a_actor, const RE::NiPoint3& a_feet)
	{
		RE::NiPoint3 fallback = a_feet;
		fallback.z += GameOffsets::kAimPointChestZ;

		RE::NiBound bound{};
		if (!TryReadWorldBound(a_actor, bound)) {
			return fallback;
		}

		RE::NiPoint3 out = bound.center;

		// Bias upward from the bounding sphere's centre toward the chest.
		// The centre is the GEOMETRIC middle of the whole body, which for a
		// standing humanoid is hip height, not chest - visible in-game as a
		// target box sitting over the target's thighs (screenshot,
		// 2026-08-25), and as redirected rounds converging lower than the
		// player aimed.
		//
		// Scaled by how high the centre already sits above the actor's own
		// feet, rather than by a fixed distance or by the sphere's radius.
		// That keeps it pose-aware for free: a standing figure with its
		// centre ~0.9m up aims at ~1.35m (chest), while a crouching or
		// prone one has a much lower centre and so gets a proportionally
		// smaller lift instead of being aimed at above its own body. A
		// fixed offset would have broken exactly the prone case this was
		// partly meant to help.
		// One-shot per actor. The height factor has now been guessed at
		// twice (1.5 then 1.25) and read too high both times, which means
		// the model behind it - "the bounding sphere's centre sits at about
		// hip height" - is not actually established. An old BoneProbe line
		// even suggests the centre is only ~0.10 above the feet, which
		// cannot be squared with a box that renders at chest height. Rather
		// than pick a third number, log what the inputs really are so the
		// factor can be derived from measurement.
		{
			static std::unordered_set<std::uint32_t> s_logged;
			if (s_logged.insert(a_actor->GetFormID()).second) {
				REX::INFO("[VATS] aimpoint: formID=0x{:08X} feetZ={:.3f} centreZ={:.3f} centreAboveFeet={:.3f} radius={:.3f} factor={:.2f} -> aimZ={:.3f} (aimAboveFeet={:.3f})",
					a_actor->GetFormID(), a_feet.z, out.z, out.z - a_feet.z, bound.radius,
					Settings::Get().aimPointHeightFactor,
					a_feet.z + (out.z - a_feet.z) * Settings::Get().aimPointHeightFactor,
					(out.z - a_feet.z) * Settings::Get().aimPointHeightFactor);
			}
		}

		const float centreAboveFeet = out.z - a_feet.z;
		if (centreAboveFeet > 0.0f) {
			out.z = a_feet.z + centreAboveFeet * Settings::Get().aimPointHeightFactor;
			// Never aim above the top of the actor's own bounding sphere -
			// a bad factor should degrade to "aims high on the body", never
			// to "aims over its head".
			const float maxZ = bound.center.z + bound.radius;
			if (out.z > maxZ) {
				out.z = maxZ;
			}
		}

		const float minZ = a_feet.z + kMinAimPointAboveFeet;
		if (out.z < minZ) {
			out.z = minZ;
		}
		return out;
	}

	void WorldBoundProbe::LogIfChanged(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}

		RE::NiBound bound{};
		if (!TryReadWorldBound(a_actor, bound)) {
			return;
		}

		RE::NiPoint3 feet{};
		if (!Read(a_actor, GameOffsets::kLocation, feet)) {
			return;
		}
		RE::NiPoint3 oldAimPoint = feet;
		oldAimPoint.z += GameOffsets::kAimPointChestZ;

		static std::unordered_map<std::uint32_t, RE::NiPoint3> s_lastLogged;
		const std::uint32_t                                    formID = a_actor->GetFormID();
		const auto                                             it = s_lastLogged.find(formID);
		if (it != s_lastLogged.end() && Distance(it->second, bound.center) < kLogMoveThreshold) {
			return;
		}
		s_lastLogged[formID] = bound.center;

		REX::INFO("[VATS] worldBound: formID=0x{:08X} feet=({:.2f},{:.2f},{:.2f}) oldAimPoint=({:.2f},{:.2f},{:.2f}) center=({:.2f},{:.2f},{:.2f}) radius={:.2f} centerAboveFeet={:.2f}",
			formID, feet.x, feet.y, feet.z,
			oldAimPoint.x, oldAimPoint.y, oldAimPoint.z,
			bound.center.x, bound.center.y, bound.center.z, bound.radius,
			bound.center.z - feet.z);
	}
}
