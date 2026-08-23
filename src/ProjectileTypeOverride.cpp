#include "ProjectileTypeOverride.h"

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

		template <class T>
		[[nodiscard]] bool Write(void* a_base, std::size_t a_off, const T& a_val)
		{
			return SafeWrite(static_cast<std::byte*>(a_base) + a_off, &a_val, sizeof(T));
		}

		// Same equipped-weapon resolution as AimAssistProbe.cpp/
		// ProjectileFlagProbe.cpp - duplicated rather than shared, matching
		// this project's existing per-file style.
		constexpr std::size_t   kInventoryList = 0xA0;
		constexpr std::size_t   kInventoryListData = 0x28;
		constexpr std::size_t   kArraySize = 0x00;
		constexpr std::size_t   kArrayCapacity = 0x04;
		constexpr std::size_t   kArrayData = 0x08;
		constexpr std::size_t   kItemStride = 0x28;
		constexpr std::size_t   kItemObject = 0x00;
		constexpr std::size_t   kItemInstanceData = 0x08;
		constexpr std::size_t   kItemFlags = 0x20;
		constexpr std::uint32_t kSlotMask = 0x7;
		constexpr std::uint8_t  kFormTypeWEAP = 0x30;
		constexpr std::size_t   kInstanceDataWeaponAmmoData = 0x20;
		constexpr std::size_t   kWeaponAmmoDataProjectile = 0x20;  // sweep-confirmed 2026-08-23, see ProjectileFlagProbe
		constexpr std::uint8_t  kFormTypePROJ = 0x3A;

		// See ProjectileFlagProbe.cpp for how these were found/confirmed
		// (raw-memory decoding, cross-checked against xEdit's own values
		// for a real mod weapon).
		constexpr std::size_t kProjectileData = 0x130;
		constexpr std::size_t kProjectileDataType = 0x84;

		// The value seen on both confirmed-real-projectile weapons tested
		// (rocket launcher, Shingen homing mod). Every tested hitscan
		// weapon read 0x02 instead.
		constexpr std::uint8_t kRealProjectileTypeValue = 0x00;

		[[nodiscard]] std::uint64_t ResolveEquippedProjectile(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return 0;
			}

			std::uint64_t invList = 0;
			if (!Read(a_actor, kInventoryList, invList) || !invList) {
				return 0;
			}

			std::uint32_t arraySize = 0;
			std::uint32_t arrayCapacity = 0;
			std::uint64_t arrayData = 0;
			if (!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArraySize, arraySize) ||
				!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArrayCapacity, arrayCapacity) ||
				!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArrayData, arrayData) ||
				arraySize == 0 || arrayCapacity < arraySize || !arrayData) {
				return 0;
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
				return 0;
			}

			std::uint64_t weaponAmmoData = 0;
			if (!Read(reinterpret_cast<const void*>(instanceData), kInstanceDataWeaponAmmoData, weaponAmmoData) || !weaponAmmoData) {
				return 0;
			}

			std::uint64_t projectile = 0;
			if (!Read(reinterpret_cast<const void*>(weaponAmmoData), kWeaponAmmoDataProjectile, projectile) || !projectile) {
				return 0;
			}

			std::uint8_t formType = 0;
			if (!Read(reinterpret_cast<const void*>(projectile), GameOffsets::kFormType, formType) || formType != kFormTypePROJ) {
				return 0;
			}

			return projectile;
		}
	}

	ProjectileTypeOverride::Token ProjectileTypeOverride::Engage(RE::Actor* a_actor)
	{
		Token token;

		const std::uint64_t projectile = ResolveEquippedProjectile(a_actor);
		if (!projectile) {
			return token;
		}

		std::uint8_t currentType = 0;
		if (!Read(reinterpret_cast<const void*>(projectile), kProjectileData + kProjectileDataType, currentType)) {
			return token;
		}

		if (currentType == kRealProjectileTypeValue) {
			// Already a real projectile (rocket/grenade/Shingen-like) -
			// ProjectileTracker already handles these, nothing to do.
			return token;
		}

		if (!Write(reinterpret_cast<void*>(projectile), kProjectileData + kProjectileDataType, kRealProjectileTypeValue)) {
			return token;
		}

		token.projectile = projectile;
		token.originalType = currentType;
		token.active = true;
		REX::INFO("[VATS] projtype: engaged, projectile=0x{:X} type 0x{:02X} -> 0x{:02X}", projectile, currentType, kRealProjectileTypeValue);
		return token;
	}

	void ProjectileTypeOverride::Disengage(const Token& a_token)
	{
		if (!a_token.active || !a_token.projectile) {
			return;
		}
		const bool wrote = Write(reinterpret_cast<void*>(a_token.projectile), kProjectileData + kProjectileDataType, a_token.originalType);
		REX::INFO("[VATS] projtype: disengaged, projectile=0x{:X} type -> 0x{:02X} (ok={})", a_token.projectile, a_token.originalType, wrote);
	}
}
