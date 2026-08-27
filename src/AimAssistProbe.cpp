#include "AimAssistProbe.h"

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

		// All offsets below are either CommonLibSF static_assert-backed
		// struct layout, or were cross-validated live in-game 2026-08-22
		// (see AimAssistProbe.h for the full confirmed chain and its
		// history - lastBoundWeapon was a dead end, pad30 was a dead end,
		// pad38 is the real BGSAimAssistModel*).
		constexpr std::size_t kInventoryList = 0xA0;                    // RE::TESObjectREFR::inventoryList (BSGuarded, read raw - see header)
		constexpr std::size_t kInventoryListData = 0x28;                // RE::BGSInventoryList::data
		constexpr std::size_t kArraySize = 0x00;                        // RE::BSTArrayBase::_size
		constexpr std::size_t kArrayCapacity = 0x04;                    // RE::BSTArrayBase::_capacity
		constexpr std::size_t kArrayData = 0x08;                        // RE::BSTArray<T>::_data
		constexpr std::size_t kItemStride = 0x28;                       // sizeof(RE::BGSInventoryItem)
		constexpr std::size_t kItemObject = 0x00;                       // RE::BGSInventoryItem::object
		constexpr std::size_t kItemInstanceData = 0x08;                 // RE::BGSInventoryItem::instanceData (_ptr at +0)
		constexpr std::size_t kItemFlags = 0x20;                        // RE::BGSInventoryItem::flags
		constexpr std::uint32_t kSlotMask = 0x7;                        // kSlotIndex1|2|3 - IsEquipped() check
		constexpr std::uint8_t  kFormTypeWEAP = 0x30;                   // RE::FormType::kWEAP
		constexpr std::size_t kInstanceDataWeaponDataAim = 0x18;        // RE::TESObjectWEAPInstanceData::WeaponDataAim
		constexpr std::size_t kWeaponDataAimAimModel = 0x28;            // RE::WeaponDataAim::aimModel (cross-check field)
		constexpr std::size_t kWeaponDataAimAimAssistModel = 0x38;      // RE::WeaponDataAim - confirmed live 2026-08-22 (NOT +0x30)
		constexpr std::size_t kBaseFormTData = 0x38;                    // RE::BGSBaseFormT<T,...>::data (embedded, not a pointer)
		constexpr std::size_t kAimAssistBulletBendingConeAngle = 0x38;  // RE::AimAssistData::bulletBendingConeAngle
		constexpr std::size_t kAimAssistEnabled = 0x5C;                 // RE::AimAssistData::aimAssistEnabled
		constexpr std::uint8_t kFormTypeAMDL = 0x93;                    // RE::FormType::kAMDL (BGSAimModel)
		constexpr std::uint8_t kFormTypeAAMD = 0x94;                    // RE::FormType::kAAMD (BGSAimAssistModel)
	}

	void AimAssistProbe::ForceAimAssist(RE::Actor* a_actor)
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

		std::uint64_t weapon = 0;
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
			weapon = object;
			instanceData = itemInstanceData;
			break;
		}

		if (!weapon || !instanceData) {
			return;
		}

		std::uint64_t weaponDataAim = 0;
		if (!Read(reinterpret_cast<const void*>(instanceData), kInstanceDataWeaponDataAim, weaponDataAim) || !weaponDataAim) {
			return;
		}

		// Cross-check: aimModel's formType should be kAMDL. If this ever
		// stops matching (e.g. a game update reshuffles WeaponDataAim),
		// the whole chain below is suspect - bail rather than write
		// somewhere unverified.
		std::uint64_t aimModel = 0;
		std::uint8_t  aimModelFormType = 0;
		if (!Read(reinterpret_cast<const void*>(weaponDataAim), kWeaponDataAimAimModel, aimModel) || !aimModel ||
			!Read(reinterpret_cast<const void*>(aimModel), GameOffsets::kFormType, aimModelFormType) ||
			aimModelFormType != kFormTypeAMDL) {
			VATS_WARN("[VATS] aimassist: cross-check field aimModel no longer matches kAMDL, chain may have shifted - not touching aim-assist");
			return;
		}

		std::uint64_t aimAssistModel = 0;
		std::uint8_t  aimAssistFormType = 0;
		if (!Read(reinterpret_cast<const void*>(weaponDataAim), kWeaponDataAimAimAssistModel, aimAssistModel) || !aimAssistModel ||
			!Read(reinterpret_cast<const void*>(aimAssistModel), GameOffsets::kFormType, aimAssistFormType) ||
			aimAssistFormType != kFormTypeAAMD) {
			VATS_LOG("[VATS] aimassist: no BGSAimAssistModel found at the expected offset (formType=0x{:02X}, expect 0x{:02X})",
				aimAssistFormType, kFormTypeAAMD);
			return;
		}

		bool wasEnabled = false;
		(void)Read(reinterpret_cast<const void*>(aimAssistModel), kBaseFormTData + kAimAssistEnabled, wasEnabled);

		constexpr bool kEnabled = true;
		const bool     wrote = Write(reinterpret_cast<void*>(aimAssistModel), kBaseFormTData + kAimAssistEnabled, kEnabled);

		float bulletBendingConeAngle = 0.0f;
		(void)Read(reinterpret_cast<const void*>(aimAssistModel), kBaseFormTData + kAimAssistBulletBendingConeAngle, bulletBendingConeAngle);

		VATS_LOG("[VATS] aimassist: model=0x{:X} wasEnabled={} wrote=true (ok={}) bulletBendingConeAngle={:.3f}",
			aimAssistModel, wasEnabled, wrote, bulletBendingConeAngle);
	}
}
