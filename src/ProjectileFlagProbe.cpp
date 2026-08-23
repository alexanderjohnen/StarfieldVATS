#include "ProjectileFlagProbe.h"

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

		// Same equipped-weapon resolution as AimAssistProbe.cpp (see that
		// file for how each offset here was confirmed live 2026-08-22) -
		// duplicated rather than shared, matching this project's existing
		// per-file style (ProjectileTracker.cpp also keeps its own local
		// Read/Write helpers rather than a shared header).
		constexpr std::size_t  kInventoryList = 0xA0;
		constexpr std::size_t  kInventoryListData = 0x28;
		constexpr std::size_t  kArraySize = 0x00;
		constexpr std::size_t  kArrayCapacity = 0x04;
		constexpr std::size_t  kArrayData = 0x08;
		constexpr std::size_t  kItemStride = 0x28;
		constexpr std::size_t  kItemObject = 0x00;
		constexpr std::size_t  kItemInstanceData = 0x08;
		constexpr std::size_t  kItemFlags = 0x20;
		constexpr std::uint32_t kSlotMask = 0x7;
		constexpr std::uint8_t  kFormTypeWEAP = 0x30;

		// New for this probe (2026-08-23) - see ProjectileFlagProbe.h for
		// the full chain and the WeaponAmmoData::ammo offset discrepancy.
		constexpr std::size_t  kInstanceDataWeaponAmmoData = 0x20;  // RE::TESObjectWEAPInstanceData::WeaponAmmoData
		constexpr std::size_t  kWeaponAmmoDataAmmo = 0x18;          // RE::WeaponAmmoData::ammo - static_assert-backed, NOT the header's stale "0x0020" inline comment
		// FormTypes.h's inline comments are hex without a "0x" prefix, not
		// decimal - misread once as decimal (0x1F instead of 0x31), which
		// made every cross-check fail even though the pointer chain was
		// actually resolving correctly. kAMMO's real value is 0x31.
		constexpr std::uint8_t  kFormTypeAMMO = 0x31;               // RE::FormType::kAMMO
		constexpr std::size_t  kAmmoDataProjectile = 0x1F8;         // RE::TESAmmo::data.projectile (AMMO_DATA is at 0x1F8, projectile is its first member)
		constexpr std::uint8_t  kFormTypePROJ = 0x3A;               // RE::FormType::kPROJ (58)
		constexpr std::size_t  kProjectileData = 0x128;             // RE::BGSProjectile::data
		constexpr std::size_t  kProjectileDataFlags = 0x48;         // RE::BGSProjectileData::flags (within data)
		constexpr std::size_t  kProjectileDataGravity = 0x4C;
		constexpr std::size_t  kProjectileDataSpeed = 0x50;
		constexpr std::size_t  kProjectileDataRange = 0x54;
		constexpr std::uint32_t kFlagHitScan = 1u << 0;
	}

	void ProjectileFlagProbe::LogCurrentWeaponProjectileFlags(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}

		std::uint64_t invList = 0;
		if (!Read(a_actor, kInventoryList, invList) || !invList) {
			return;
		}

		std::uint32_t arraySize = 0;
		std::uint32_t arrayCapacity = 0;
		std::uint64_t arrayData = 0;
		if (!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArraySize, arraySize) ||
			!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArrayCapacity, arrayCapacity) ||
			!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArrayData, arrayData) ||
			arraySize == 0 || arrayCapacity < arraySize || !arrayData) {
			return;
		}

		std::uint64_t instanceData = 0;
		for (std::uint32_t i = 0; i < arraySize; ++i) {
			const std::uint64_t itemBase = arrayData + static_cast<std::uint64_t>(i) * kItemStride;

			std::uint64_t object = 0;
			if (!Read(reinterpret_cast<const void*>(itemBase), kItemObject, object) || !object) {
				continue;
			}
			std::uint8_t formType = 0;
			if (!Read(reinterpret_cast<const void*>(object), GameOffsets::kFormType, formType) || formType != kFormTypeWEAP) {
				continue;
			}
			std::uint32_t flags = 0;
			if (!Read(reinterpret_cast<const void*>(itemBase), kItemFlags, flags) || (flags & kSlotMask) == 0) {
				continue;  // not equipped
			}
			std::uint64_t itemInstanceData = 0;
			if (!Read(reinterpret_cast<const void*>(itemBase), kItemInstanceData, itemInstanceData) || !itemInstanceData) {
				continue;
			}
			instanceData = itemInstanceData;
			break;
		}

		if (!instanceData) {
			return;
		}

		std::uint64_t weaponAmmoData = 0;
		if (!Read(reinterpret_cast<const void*>(instanceData), kInstanceDataWeaponAmmoData, weaponAmmoData) || !weaponAmmoData) {
			REX::INFO("[VATS] projflag: no WeaponAmmoData for equipped weapon (unarmed/melee?)");
			return;
		}

		std::uint64_t ammo = 0;
		if (!Read(reinterpret_cast<const void*>(weaponAmmoData), kWeaponAmmoDataAmmo, ammo) || !ammo) {
			REX::INFO("[VATS] projflag: WeaponAmmoData=0x{:X} but ammo pointer is null", weaponAmmoData);
			return;
		}

		std::uint8_t ammoFormType = 0;
		if (!Read(reinterpret_cast<const void*>(ammo), GameOffsets::kFormType, ammoFormType) || ammoFormType != kFormTypeAMMO) {
			REX::WARN("[VATS] projflag: cross-check failed - ammo=0x{:X} formType=0x{:02X}, expected kAMMO=0x{:02X} - chain may have shifted",
				ammo, ammoFormType, kFormTypeAMMO);
			return;
		}

		std::uint64_t projectile = 0;
		if (!Read(reinterpret_cast<const void*>(ammo), kAmmoDataProjectile, projectile) || !projectile) {
			REX::INFO("[VATS] projflag: ammo=0x{:X} has no projectile pointer", ammo);
			return;
		}

		std::uint8_t projFormType = 0;
		if (!Read(reinterpret_cast<const void*>(projectile), GameOffsets::kFormType, projFormType) || projFormType != kFormTypePROJ) {
			REX::WARN("[VATS] projflag: cross-check failed - projectile=0x{:X} formType=0x{:02X}, expected kPROJ=0x{:02X} - chain may have shifted",
				projectile, projFormType, kFormTypePROJ);
			return;
		}

		std::uint32_t rawFlags = 0;
		float         gravity = 0.0f, speed = 0.0f, range = 0.0f;
		const bool    flagsRead = Read(reinterpret_cast<const void*>(projectile), kProjectileData + kProjectileDataFlags, rawFlags);
		(void)Read(reinterpret_cast<const void*>(projectile), kProjectileData + kProjectileDataGravity, gravity);
		(void)Read(reinterpret_cast<const void*>(projectile), kProjectileData + kProjectileDataSpeed, speed);
		(void)Read(reinterpret_cast<const void*>(projectile), kProjectileData + kProjectileDataRange, range);

		REX::INFO("[VATS] projflag: ammo=0x{:X} projectile=0x{:X} flags=0x{:08X} (read={}) hitScan={} gravity={:.2f} speed={:.1f} range={:.1f}",
			ammo, projectile, rawFlags, flagsRead, flagsRead && (rawFlags & kFlagHitScan) != 0, gravity, speed, range);
	}
}
