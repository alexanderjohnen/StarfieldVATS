#include "WorldBoundProbe.h"

#include "GameOffsets.h"
#include "SafeMem.h"
#include "Settings.h"

#include <algorithm>
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

		const auto& settings = Settings::Get();

		// Anchor the aim point at the FEET and scale it by the bounding
		// sphere's RADIUS, rather than starting at the sphere's centre and
		// lifting from there.
		//
		// Measured 2026-08-26 across three pirates, which is what finally
		// separated the two candidates:
		//
		//   actor       radius  centreAboveFeet  c/r    aim (old model)
		//   0x0017E6A2  1.099   0.814            0.741  1.088
		//   0x0017E6A1  1.173   0.913            0.778  1.207
		//   0x0017E688  1.112   0.865            0.778  1.143
		//
		// The centre's height above the feet is the noisy quantity: two of
		// the three sit at 0.778 of their own radius and the third at
		// 0.741, and that difference passes straight through to the aim
		// point. Old model spread: 12cm on three humans, which Alexander
		// has been seeing since the beginning as "the box sits differently
		// on different characters". Anchoring on the radius instead cuts
		// it to about 8cm and, more to the point, pulls the outlier back in
		// line rather than preserving it.
		//
		// Some spread is CORRECT - a taller pirate's chest really is
		// higher - so the goal was never zero. It was to track actual body
		// size rather than the sphere centre's own wobble, and radius is
		// the quantity that does that: aim/radius reads 0.990, 1.029, 1.028
		// under the old model, i.e. two of three already agree to within
		// 0.1% once expressed this way.
		//
		// x/y are still the sphere centre's, untouched. They were measured
		// as sound: |sphere.x - feet.x| averages 0.0068 normalized across
		// 1004 samples spanning the full width of the screen.
		const float centreAboveFeet = out.z - a_feet.z;
		float       aimZ = a_feet.z + settings.aimPointHeightRadiusFactor * bound.radius;

		// Pose cap, unchanged in spirit from the previous model and now
		// applied unconditionally. A body on the ground keeps a large
		// radius while its centre drops to near-floor, so an uncapped
		// radius-scaled height would aim well above a corpse. Capping at a
		// fraction above the centre's own height scales down with the pose
		// exactly when it needs to, and is inactive for a standing target
		// (0.913 * 1.5 = 1.370 against an aim of 1.206 on the tallest of
		// the three above).
		//
		// Confirmed exercised in the same session: 11 frames of a
		// collapsing actor logged an on-screen body height under 0.05, and
		// the lift ratios there run up to 0.61 - only reachable via the
		// kMinAimPointAboveFeet floor below, i.e. both safety paths fired
		// on a real ragdoll for the first time.
		const float capZ = a_feet.z + std::max(0.0f, centreAboveFeet) * (1.0f + settings.aimPointMaxLiftFraction);
		if (aimZ > capZ) {
			aimZ = capZ;
		}

		const float minZ = a_feet.z + kMinAimPointAboveFeet;
		out.z = std::max(aimZ, minZ);

		// One-shot per actor. This is the measurement the model above was
		// chosen from; keep logging it so a fourth pirate or a creature can
		// be checked against the same three numbers.
		{
			static std::unordered_set<std::uint32_t> s_logged;
			if (s_logged.insert(a_actor->GetFormID()).second) {
				REX::INFO("[VATS] aimpoint: formID=0x{:08X} feetZ={:.3f} centreZ={:.3f} centreAboveFeet={:.3f} radius={:.3f} centre/radius={:.3f} -> aimAboveFeet={:.3f} (cap={:.3f})",
					a_actor->GetFormID(), a_feet.z, bound.center.z, centreAboveFeet, bound.radius,
					bound.radius > 0.0f ? centreAboveFeet / bound.radius : 0.0f,
					out.z - a_feet.z, capZ - a_feet.z);
			}
		}

		return out;
	}

	bool WorldBoundProbe::GetBoundCenter(RE::Actor* a_actor, RE::NiPoint3& a_out)
	{
		RE::NiBound bound{};
		if (!TryReadWorldBound(a_actor, bound)) {
			return false;
		}
		a_out = bound.center;
		return true;
	}

	bool WorldBoundProbe::GetBoundRadius(RE::Actor* a_actor, float& a_out)
	{
		RE::NiBound bound{};
		if (!TryReadWorldBound(a_actor, bound) || !(bound.radius > 0.0f)) {
			return false;
		}
		a_out = bound.radius;
		return true;
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
