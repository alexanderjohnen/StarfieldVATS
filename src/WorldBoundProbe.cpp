#include "WorldBoundProbe.h"

#include "GameOffsets.h"
#include "SafeMem.h"

#include <cmath>
#include <unordered_map>

namespace VATS
{
	namespace
	{
		template <class T>
		[[nodiscard]] bool Read(const void* a_base, std::size_t a_off, T& a_out)
		{
			return SafeRead(static_cast<const std::byte*>(a_base) + a_off, &a_out, sizeof(T));
		}

		// Logged again once the center has moved at least this far since
		// the last log line for this actor - filters per-frame animation
		// jitter (breathing, weapon sway) while still catching a real pose
		// change (standing<->crouching is typically tens of centimetres).
		constexpr float kLogMoveThreshold = 0.05f;

		[[nodiscard]] float Distance(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
		{
			const float dx = a_a.x - a_b.x;
			const float dy = a_a.y - a_b.y;
			const float dz = a_a.z - a_b.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}
	}

	void WorldBoundProbe::LogIfChanged(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}

		std::uint64_t loadedData = 0;
		if (!Read(a_actor, GameOffsets::kActorLoadedData, loadedData) || !loadedData) {
			return;
		}
		std::uint64_t data3D = 0;
		if (!Read(reinterpret_cast<const void*>(loadedData), GameOffsets::kLoadedRefData3D, data3D) || !data3D) {
			return;
		}
		RE::NiBound bound{};
		if (!Read(reinterpret_cast<const void*>(data3D), GameOffsets::kNiAVObjectWorldBound, bound)) {
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
