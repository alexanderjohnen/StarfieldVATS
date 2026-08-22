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

		// All offsets below come from CommonLibSF headers carrying
		// static_assert(offsetof(...)) checks on the surrounding struct -
		// the same "verified by the library's own compile-time assertion"
		// confidence tier as GameOffsets::kAimPointChestZ's siblings, not
		// a fresh guess. Only the LAST hop (kWeaponDataAimPad30) is an
		// actual hypothesis - everything before it is load-bearing,
		// already-typed CommonLibSF struct layout.
		//
		// Path revised 2026-08-22: the original chain went through
		// Actor::currentProcess->middleHigh->lastBoundWeapon (a
		// MiddleHighProcessData field whose name plausibly suggested
		// "currently drawn weapon") and then TESObjectWEAP's *static form*
		// weaponData. In-game testing showed lastBoundWeapon is null on
		// every single shot despite currentProcess/middleHigh both
		// resolving fine - it's evidently not what its name suggests.
		// Replaced with the actor's live inventory list instead (Actor
		// inherits TESObjectREFR::inventoryList), walking to the equipped
		// weapon's *per-item instance data* (mods/attachments applied)
		// rather than the static template - arguably the more correct
		// object to read aim-assist config from anyway.
		constexpr std::size_t kInventoryList = 0xA0;         // RE::TESObjectREFR::inventoryList (BSGuarded<BGSInventoryList*, BSReadWriteLock> - read raw, no lock; see below)
		constexpr std::size_t kInventoryListData = 0x28;     // RE::BGSInventoryList::data (BSTArray<BGSInventoryItem>)
		constexpr std::size_t kArraySize = 0x00;             // RE::BSTArrayBase::_size
		constexpr std::size_t kArrayCapacity = 0x04;         // RE::BSTArrayBase::_capacity
		constexpr std::size_t kArrayData = 0x08;             // RE::BSTArray<T>::_data
		constexpr std::size_t kItemStride = 0x28;            // sizeof(RE::BGSInventoryItem)
		constexpr std::size_t kItemObject = 0x00;            // RE::BGSInventoryItem::object (TESBoundObject*)
		constexpr std::size_t kItemInstanceData = 0x08;      // RE::BGSInventoryItem::instanceData (BSTSmartPointer, _ptr at +0)
		constexpr std::size_t kItemFlags = 0x20;             // RE::BGSInventoryItem::flags
		constexpr std::uint32_t kSlotMask = 0x7;             // kSlotIndex1|2|3 - IsEquipped() check
		constexpr std::uint8_t  kFormTypeWEAP = 0x30;        // RE::FormType::kWEAP
		constexpr std::size_t kInstanceDataWeaponDataAim = 0x18;    // RE::TESObjectWEAPInstanceData::WeaponDataAim
		constexpr std::size_t kWeaponDataAimAimModel = 0x28;        // RE::WeaponDataAim::aimModel (known-good, cross-check field)
		constexpr std::size_t kWeaponDataAimPad30 = 0x30;           // RE::WeaponDataAim::pad30 - HYPOTHESIS: BGSAimAssistModel*
		constexpr std::size_t kBaseFormTData = 0x38;                // RE::BGSBaseFormT<T,...>::data (embedded, not a pointer)
		constexpr std::size_t kAimAssistBulletBendingConeAngle = 0x38;  // RE::AimAssistData::bulletBendingConeAngle
		constexpr std::size_t kAimAssistEnabled = 0x5C;                 // RE::AimAssistData::aimAssistEnabled
		constexpr std::uint8_t kFormTypeAMDL = 0x93;  // RE::FormType::kAMDL (BGSAimModel)
		constexpr std::uint8_t kFormTypeAAMD = 0x94;  // RE::FormType::kAAMD (BGSAimAssistModel)
	}

	void AimAssistProbe::ProbeEquippedWeapon(RE::Actor* a_actor)
	{
		if (!a_actor) {
			REX::INFO("[VATS] aimassist-probe: null actor");
			return;
		}

		// inventoryList is a BSGuarded<BGSInventoryList*, BSReadWriteLock>
		// - reading the raw pointer directly instead of going through
		// BSReadWriteLock::LockRead()/UnlockRead() deliberately: those are
		// REL::ID-backed engine calls, and BSReadWriteLock::LockRead()
		// specifically is already on this project's list of "mapped but
		// crashed anyway" calls (TESObjectCELL::ForEachReference, see
		// commonlibsf-unmapped-ids memory) - a plain unsynchronized read
		// degrades to a stale-but-still-valid-shaped pointer at worst,
		// SafeRead-guarded either way.
		std::uint64_t invList = 0;
		if (!Read(a_actor, kInventoryList, invList) || !invList) {
			REX::INFO("[VATS] aimassist-probe: no inventoryList");
			return;
		}

		std::uint32_t arraySize = 0;
		std::uint32_t arrayCapacity = 0;
		std::uint64_t arrayData = 0;
		if (!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArraySize, arraySize) ||
			!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArrayCapacity, arrayCapacity) ||
			!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArrayData, arrayData) ||
			arraySize == 0 || arrayCapacity < arraySize || !arrayData) {
			REX::INFO("[VATS] aimassist-probe: inventoryList=0x{:X} array read failed (size={} cap={} data=0x{:X})",
				invList, arraySize, arrayCapacity, arrayData);
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
			REX::INFO("[VATS] aimassist-probe: no equipped weapon found in inventory (size={})", arraySize);
			return;
		}

		std::uint64_t weaponDataAim = 0;
		if (!Read(reinterpret_cast<const void*>(instanceData), kInstanceDataWeaponDataAim, weaponDataAim) || !weaponDataAim) {
			REX::INFO("[VATS] aimassist-probe: weapon=0x{:X} instanceData=0x{:X} has no WeaponDataAim", weapon, instanceData);
			return;
		}

		std::uint64_t  aimModel = 0;
		const bool     aimModelRead = Read(reinterpret_cast<const void*>(weaponDataAim), kWeaponDataAimAimModel, aimModel);
		std::uint8_t   aimModelFormType = 0;
		bool           aimModelFormTypeRead = false;
		if (aimModelRead && aimModel) {
			aimModelFormTypeRead = Read(reinterpret_cast<const void*>(aimModel), GameOffsets::kFormType, aimModelFormType);
		}
		REX::INFO("[VATS] aimassist-probe: weapon=0x{:X} weaponDataAim=0x{:X} aimModel=0x{:X} formType=0x{:02X} (expect 0x{:02X} kAMDL) -> {}",
			weapon, weaponDataAim, aimModel, aimModelFormType, kFormTypeAMDL,
			(aimModelFormTypeRead && aimModelFormType == kFormTypeAMDL) ? "MATCH (chain up to here confirmed)" : "MISMATCH (chain is wrong before pad30, stop here)");

		std::uint64_t pad30 = 0;
		const bool    pad30Read = Read(reinterpret_cast<const void*>(weaponDataAim), kWeaponDataAimPad30, pad30);
		if (!pad30Read || !pad30) {
			REX::INFO("[VATS] aimassist-probe: pad30 read={} value=0x{:X} - null or unreadable", pad30Read, pad30);
			return;
		}

		std::uint8_t pad30FormType = 0;
		const bool   pad30FormTypeRead = Read(reinterpret_cast<const void*>(pad30), GameOffsets::kFormType, pad30FormType);
		const bool   pad30IsAimAssistModel = pad30FormTypeRead && pad30FormType == kFormTypeAAMD;
		REX::INFO("[VATS] aimassist-probe: pad30=0x{:X} formType=0x{:02X} (expect 0x{:02X} kAAMD) -> {}",
			pad30, pad30FormType, kFormTypeAAMD,
			pad30IsAimAssistModel ? "MATCH - pad30 IS BGSAimAssistModel" : "MISMATCH - hypothesis is WRONG");

		if (!pad30IsAimAssistModel) {
			return;
		}

		float      bulletBendingConeAngle = 0.0f;
		const bool bbcaRead = Read(reinterpret_cast<const void*>(pad30), kBaseFormTData + kAimAssistBulletBendingConeAngle, bulletBendingConeAngle);
		bool       aimAssistEnabled = false;
		const bool enabledRead = Read(reinterpret_cast<const void*>(pad30), kBaseFormTData + kAimAssistEnabled, aimAssistEnabled);
		REX::INFO("[VATS] aimassist-probe: bulletBendingConeAngle={:.3f} (read={}) aimAssistEnabled={} (read={})",
			bulletBendingConeAngle, bbcaRead, aimAssistEnabled, enabledRead);
	}
}
