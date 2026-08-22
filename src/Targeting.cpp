#include "Targeting.h"

#include "GameOffsets.h"
#include "SafeMem.h"

namespace VATS
{
	namespace
	{
		// Offsets/constants live in GameOffsets.h — empirically verified,
		// see the long comment in Targeting.h for how each one was proven.
		constexpr auto kCellReferencesOffset = GameOffsets::kCellReferences;
		constexpr auto kFormTypeOff = GameOffsets::kFormType;
		constexpr auto kLocationOff = GameOffsets::kLocation;
		constexpr auto kBoolBitsOff = GameOffsets::kBoolBits;
		constexpr auto kFormTypeACHR = GameOffsets::kFormTypeACHR;
		constexpr auto kDeadBit = GameOffsets::kActorDeadBit;

		template <class T>
		[[nodiscard]] bool Read(const void* a_base, std::size_t a_off, T& a_out)
		{
			return SafeRead(static_cast<const std::byte*>(a_base) + a_off, &a_out, sizeof(T));
		}

		[[nodiscard]] bool TryReadCandidate(const void* a_entry, const void* a_player, RE::NiPoint3& a_posOut)
		{
			if (!a_entry || a_entry == a_player) {
				return false;
			}

			std::uint8_t formType = 0;
			if (!Read(a_entry, kFormTypeOff, formType) || formType != kFormTypeACHR) {
				return false;
			}

			std::uint32_t boolBits = 0;
			if (Read(a_entry, kBoolBitsOff, boolBits) && (boolBits & kDeadBit) != 0) {
				return false;  // dead
			}

			return Read(a_entry, kLocationOff, a_posOut);
		}
	}

	std::optional<TargetPick> FindNearestActorToCrosshair(float a_maxRange, float a_maxConeDeg)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* playerCamera = RE::PlayerCamera::GetSingleton();
		if (!player || !playerCamera) {
			return std::nullopt;
		}

		auto* cell = player->parentCell;
		auto* cameraRoot = playerCamera->cameraRoot.get();
		if (!cell || !cameraRoot) {
			return std::nullopt;
		}

		std::uint32_t size = 0;
		std::uint32_t capacity = 0;
		std::uint64_t data = 0;
		if (!Read(cell, kCellReferencesOffset, size) ||
			!Read(cell, kCellReferencesOffset + 4, capacity) ||
			!Read(cell, kCellReferencesOffset + 8, data) ||
			size == 0 || capacity < size || !data) {
			return std::nullopt;
		}

		const auto& world = cameraRoot->world;
		const RE::NiPoint3 camPos = world.translate;

		// Row 1 of the rotation matrix is the view direction — empirically
		// verified in-game 2026-08-22 (dot product ~0.99 against the known
		// direction to a dead-center target, in two independent tests). Row
		// 0 (which NiCamera::WorldToScreen's depth term is built from) is
		// NOT forward here — it has zero Z and behaves like a lateral axis.
		RE::NiPoint3 camFwd{ world.rotate.entry[1].x, world.rotate.entry[1].y, world.rotate.entry[1].z };
		const float  fwdLen = std::sqrt(camFwd.x * camFwd.x + camFwd.y * camFwd.y + camFwd.z * camFwd.z);
		if (fwdLen < 1.0e-4f) {
			return std::nullopt;
		}
		camFwd.x /= fwdLen;
		camFwd.y /= fwdLen;
		camFwd.z /= fwdLen;

		std::optional<TargetPick> best;
		float                     bestCosAngle = std::cos(a_maxConeDeg * 0.01745329f);
		// Sanity bound against a garbage `size` (wrong offset, corrupted
		// read) turning this into a near-unbounded loop — NOT meant as a
		// practical limit. Found and fixed 2026-08-22: the previous cap of
		// 4096 was silently truncating real cells (a station interior with
		// 6900+ references), always missing whatever landed past index
		// 4096 — which a moving/following actor (added to the cell's
		// reference list dynamically as they walk in, rather than placed by
		// the level designer near the start of the array) reliably does.
		// Confirmed via a companion NPC found instantly on the player's ship
		// (a small cell, well under the old cap) but never found in any
		// larger location, despite standing in plain sight the whole time.
		constexpr std::uint32_t   kScanCap = 32768;
		const std::uint32_t       scanCount = std::min<std::uint32_t>(size, kScanCap);

		// Compact funnel telemetry — kept permanently (not just for one-off
		// diagnostics) because reverse-engineered offsets/axes in this
		// codebase have broken silently more than once. One line per scan,
		// cheap, always answers "how far did candidates get" without a
		// special diagnostic build.
		std::uint32_t nActorsSeen = 0;
		std::uint32_t nOutOfRange = 0;
		std::uint32_t nOutsideCone = 0;
		float         closestMissAngleDeg = -1.0f;  // smallest angle among actors that failed the cone check

		for (std::uint32_t i = 0; i < scanCount; ++i) {
			std::uint64_t entry = 0;
			if (!Read(reinterpret_cast<const void*>(data), 8ull * i, entry) || !entry) {
				continue;
			}

			RE::NiPoint3 pos{};
			if (!TryReadCandidate(reinterpret_cast<const void*>(entry), player, pos)) {
				continue;
			}
			++nActorsSeen;

			const float dx = pos.x - camPos.x;
			const float dy = pos.y - camPos.y;
			const float dz = pos.z - camPos.z;
			const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (dist > a_maxRange || dist < 1.0e-4f) {
				++nOutOfRange;
				continue;
			}

			const float cosAngle = (dx * camFwd.x + dy * camFwd.y + dz * camFwd.z) / dist;
			const float angleDeg = std::acos(std::clamp(cosAngle, -1.0f, 1.0f)) * 57.29578f;
			if (cosAngle <= bestCosAngle) {
				++nOutsideCone;
				if (closestMissAngleDeg < 0.0f || angleDeg < closestMissAngleDeg) {
					closestMissAngleDeg = angleDeg;
				}
				continue;
			}

			bestCosAngle = cosAngle;
			best = TargetPick{ RE::NiPointer<RE::Actor>(reinterpret_cast<RE::Actor*>(entry)), dist, angleDeg };
		}

		if (scanCount < size) {
			REX::WARN("[targeting] cellRefs={} exceeds scan cap {} — {} entries were NOT scanned this call",
				size, kScanCap, size - scanCount);
		}
		REX::INFO("[targeting] cellRefs={} scanned={} actorsSeen={} outOfRange={} outsideCone={} closestMissAngle={:.1f}deg camFwd=({:.3f},{:.3f},{:.3f}) -> {}",
			size, scanCount, nActorsSeen, nOutOfRange, nOutsideCone, closestMissAngleDeg, camFwd.x, camFwd.y, camFwd.z, best ? "FOUND" : "none");

		return best;
	}

	RE::NiPointer<RE::Actor> GetCrosshairActivationTarget()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return nullptr;
		}

		RE::TESObjectREFR* target = nullptr;
		if (!Read(player, GameOffsets::kPlayerCommandTarget, target) || !target) {
			return nullptr;
		}

		std::uint8_t formType = 0;
		if (!Read(target, kFormTypeOff, formType) || formType != kFormTypeACHR) {
			return nullptr;  // not an actor (e.g. a container/terminal under the crosshair)
		}

		std::uint32_t boolBits = 0;
		if (Read(target, kBoolBitsOff, boolBits) && (boolBits & kDeadBit) != 0) {
			return nullptr;  // dead
		}

		return RE::NiPointer<RE::Actor>(reinterpret_cast<RE::Actor*>(target));
	}

	bool HasDetectionLOS(RE::Actor* a_source, RE::Actor* a_target)
	{
		if (!a_source || !a_target) {
			return false;
		}
		// Cast/call shape copied as-is from Cassiopeia's HasDetectionLOS
		// (see Targeting.h's comment) rather than reasoned out from
		// scratch — the leading (0, 0) args are dummy/unknown-purpose
		// slots in their working implementation, kept verbatim rather
		// than guessed at.
		REL::Relocation<std::uintptr_t> funcPtr{ REL::ID(170456) };
		const auto func = reinterpret_cast<bool (*)(std::uintptr_t, std::uint32_t, RE::Actor*, RE::Actor*)>(funcPtr.address());
		return func(0, 0, a_source, a_target);
	}
}
