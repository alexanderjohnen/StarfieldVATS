#include "ProjectileTracker.h"

#include "GameOffsets.h"
#include "SafeMem.h"

namespace VATS
{
	namespace
	{
		template <class T>
		[[nodiscard]] bool Read(const void* a_base, std::size_t a_off, T& a_out)
		{
			return SafeRead(static_cast<const std::byte*>(a_base) + a_off, &a_out, sizeof(T));
		}

		// Live projectile *reference* form types - NOT RE::FormType::kPROJ,
		// which is the base BGSProjectile record (the ammo-type template),
		// not a world instance. A flying projectile in the world is one of
		// these concrete subtypes, a contiguous range in FormTypes.h.
		constexpr std::uint8_t kFormTypeProjectileMin = 0x4C;  // kPMIS
		constexpr std::uint8_t kFormTypeProjectileMax = 0x54;  // kPEMI
	}

	void ProjectileTracker::ProbeAfterFire()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		auto* cell = player->parentCell;
		if (!cell) {
			REX::INFO("[VATS] projectile-probe: no parentCell");
			return;
		}

		std::uint32_t size = 0;
		std::uint32_t capacity = 0;
		std::uint64_t data = 0;
		if (!Read(cell, GameOffsets::kCellReferences, size) ||
			!Read(cell, GameOffsets::kCellReferences + 4, capacity) ||
			!Read(cell, GameOffsets::kCellReferences + 8, data) ||
			size == 0 || capacity < size || !data) {
			REX::INFO("[VATS] projectile-probe: cell reference array read failed");
			return;
		}

		const RE::NiPoint3 playerPos = player->GetPosition();
		std::uint32_t      found = 0;

		for (std::uint32_t i = 0; i < size; ++i) {
			std::uint64_t entry = 0;
			if (!Read(reinterpret_cast<const void*>(data), 8ull * i, entry) || !entry) {
				continue;
			}

			std::uint8_t formType = 0;
			if (!Read(reinterpret_cast<const void*>(entry), GameOffsets::kFormType, formType) ||
				formType < kFormTypeProjectileMin || formType > kFormTypeProjectileMax) {
				continue;
			}

			RE::NiPoint3  pos{};
			float         age = -1.0f;
			float         distanceMoved = -1.0f;
			RE::NiPoint3  velocity{};
			std::uint32_t shooterHandle = 0;
			(void)Read(reinterpret_cast<const void*>(entry), GameOffsets::kLocation, pos);
			(void)Read(reinterpret_cast<const void*>(entry), 0x220, age);            // RE::Projectile::age
			(void)Read(reinterpret_cast<const void*>(entry), 0x234, distanceMoved);  // RE::Projectile::distanceMoved
			(void)Read(reinterpret_cast<const void*>(entry), 0x164, velocity);       // RE::Projectile::velocity
			(void)Read(reinterpret_cast<const void*>(entry), 0x180, shooterHandle);  // RE::Projectile::shooterHandle

			const float dx = pos.x - playerPos.x;
			const float dy = pos.y - playerPos.y;
			const float dz = pos.z - playerPos.z;
			const float distFromPlayer = std::sqrt(dx * dx + dy * dy + dz * dz);

			++found;
			REX::INFO("[VATS] projectile-probe: formType=0x{:02X} distFromPlayer={:.2f} age={:.3f} distanceMoved={:.2f} velocity=({:.1f},{:.1f},{:.1f}) shooterHandle=0x{:08X}",
				formType, distFromPlayer, age, distanceMoved, velocity.x, velocity.y, velocity.z, shooterHandle);
		}

		if (found == 0) {
			REX::INFO("[VATS] projectile-probe: no projectile-type reference found in cell (cellRefs={})", size);
		}
	}
}
