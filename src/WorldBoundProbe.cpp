#include "WorldBoundProbe.h"

#include "GameOffsets.h"
#include "SafeMem.h"
#include "Settings.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <chrono>
#include <mutex>
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

		// --- Aim-point smoothing ---
		//
		// The bounding sphere BREATHES with the animation, and on some
		// creatures it does far more than breathe. Measured 2026-08-26 on
		// a flying alien (0xFF098288, 5938 samples): its radius swings
		// 3.09..4.50 and the height of its centre above its own root
		// swings 0.14..2.18 - a two-metre range, which at the 1.5x factor
		// would throw the aim point around by over three metres in time
		// with the wingbeat. Alexander saw it immediately: "die Box bewegt
		// sich mit dem Flügelschlagen mit".
		//
		// For comparison, a human guard's centre swings 0.09 and the
		// ground-huggers about 0.32. So this is not a flying-creature
		// special case, it is the same effect at every scale, and it is
		// there in every model built on the bounding sphere. Smoothing is
		// therefore not a patch over the current model - it is a missing
		// piece that all three models needed.
		//
		// Smoothed in OFFSET-FROM-ROOT space, not in world space. The
		// actor's ref origin already tracks its real movement exactly, so
		// only the animation-driven part is filtered: a sprinting target
		// stays pinned with zero lag while its wings stop mattering.
		// Smoothing world position instead would make the box trail behind
		// anything that moves.
		constexpr std::size_t kSmoothSlots = 8;

		struct SmoothSlot
		{
			std::uint32_t                         formID{ 0 };
			RE::NiPoint3                          offset{};
			std::chrono::steady_clock::time_point last{};
		};

		// GetAimPoint is called from the render thread (Overlay) AND from
		// AimAssist's steering thread, so this state needs a lock. An
		// unguarded map shared across those two threads is precisely the
		// shape of the heap corruption this project already spent a day
		// on. A fixed array rather than a map for the same reason: no
		// allocation, no rehash, nothing to corrupt.
		std::mutex                            g_smoothLock;
		std::array<SmoothSlot, kSmoothSlots>  g_smooth{};

		[[nodiscard]] RE::NiPoint3 SmoothOffset(std::uint32_t a_formID, const RE::NiPoint3& a_raw, float a_tau)
		{
			if (!(a_tau > 0.0f)) {
				return a_raw;
			}

			const auto             now = std::chrono::steady_clock::now();
			const std::scoped_lock lock(g_smoothLock);

			SmoothSlot* slot = nullptr;
			for (auto& candidate : g_smooth) {
				if (candidate.formID == a_formID) {
					slot = &candidate;
					break;
				}
			}
			if (!slot) {
				// Reuse the slot untouched for longest - with 8 slots and
				// one locked target at a time this effectively never
				// evicts anything live.
				slot = &g_smooth[0];
				for (auto& candidate : g_smooth) {
					if (candidate.formID == 0 || candidate.last < slot->last) {
						slot = &candidate;
					}
				}
				*slot = SmoothSlot{ a_formID, a_raw, now };
				return a_raw;
			}

			const float dt = std::chrono::duration<float>(now - slot->last).count();
			slot->last = now;

			// Out of sight long enough that the old value means nothing -
			// snap rather than gliding in from wherever the target used to
			// be. Also covers a negative dt from a clock oddity.
			constexpr float kStaleSeconds = 1.5f;
			if (!(dt > 0.0f) || dt > kStaleSeconds) {
				slot->offset = a_raw;
				return a_raw;
			}

			const float alpha = 1.0f - std::exp(-dt / a_tau);
			slot->offset.x += (a_raw.x - slot->offset.x) * alpha;
			slot->offset.y += (a_raw.y - slot->offset.y) * alpha;
			slot->offset.z += (a_raw.z - slot->offset.z) * alpha;
			return slot->offset;
		}

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

		// Aim at a fixed fraction above the bounding sphere's CENTRE
		// HEIGHT, and use nothing else. The radius is no longer part of
		// this calculation at all.
		//
		// Third model, and the first one chosen with non-humanoids in the
		// sample - which is what changed the answer. Measured 2026-08-26:
		//
		//   creature          radius  centreAboveFeet  centre/radius
		//   human pirate      1.03    0.84             0.81
		//   mantid (spidery)  2.96    1.40             0.47
		//   hopper (low)      2.16    0.30             0.14
		//
		// The radius measures a creature's LONGEST EXTENT, not its height.
		// A sprawling ground-hugger reads a radius of 2.16 with its body
		// 30cm off the ground, so the feet+radius model wanted to aim
		// 2.22m up - several body lengths above it. Every non-humanoid
		// measured hit the pose cap, all four of them, which means the
		// cap rather than the model was producing the aim point for an
		// entire class of target. When the safety net carries the load,
		// the net is the model and the model is noise. So the net became
		// the model.
		//
		// Centre height degrades gracefully across all of them because it
		// is a height rather than a size. At 1.5x: 1.26m on a human (~70%
		// of body height, i.e. the chest - and higher than the 64-68% the
		// previous model measured on screenshots), 0.46m on the hopper,
		// 2.10m on the mantid. It also absorbs pose for free, since a
		// crouching enemy's centre drops and the aim point drops with it.
		//
		// This was the original 2026-08-25 model, dropped then because it
		// "amplified the spread between characters". With creatures in the
		// sample that objection reads differently: most of that spread is
		// real - a crouching enemy's chest IS lower, a taller pirate's
		// chest IS higher - and 3cm of spread between two pirates is a
		// cheap price for not aiming two metres over an alien.
		const float centreAboveFeet = out.z - a_feet.z;
		const float aimZ = a_feet.z + settings.aimPointCentreFactor * std::max(0.0f, centreAboveFeet);

		// Floor for a collapsed body, whose centre can read at or below
		// its own feet for a frame - measured on a dead mantid at
		// centreAboveFeet = -0.008. Never aim into the floor.
		const float minZ = a_feet.z + kMinAimPointAboveFeet;
		out.z = std::max(aimZ, minZ);

		// Low-pass the whole offset from the actor's root, x/y included:
		// the sprawling ground-huggers put their sphere centre 0.035 of
		// screen width off their own root and it wanders there too.
		const RE::NiPoint3 rawOffset{ out.x - a_feet.x, out.y - a_feet.y, out.z - a_feet.z };
		const RE::NiPoint3 smoothed = SmoothOffset(a_actor->GetFormID(), rawOffset,
			settings.aimPointSmoothingSeconds);
		out.x = a_feet.x + smoothed.x;
		out.y = a_feet.y + smoothed.y;
		out.z = std::max(a_feet.z + smoothed.z, minZ);

		// One-shot per actor. This is the measurement the model above was
		// chosen from; keep logging it so a fourth pirate or a creature can
		// be checked against the same three numbers.
		{
			static std::unordered_set<std::uint32_t> s_logged;
			if (s_logged.insert(a_actor->GetFormID()).second) {
				REX::INFO("[VATS] aimpoint: formID=0x{:08X} feetZ={:.3f} centreZ={:.3f} centreAboveFeet={:.3f} radius={:.3f} centre/radius={:.3f} -> aimAboveFeet={:.3f}",
					a_actor->GetFormID(), a_feet.z, bound.center.z, centreAboveFeet, bound.radius,
					bound.radius > 0.0f ? centreAboveFeet / bound.radius : 0.0f,
					out.z - a_feet.z);
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
