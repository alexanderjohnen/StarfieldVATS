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
		constexpr std::size_t kActorCurrentProcess = 0x228;         // RE::Actor::currentProcess
		constexpr std::size_t kAIProcessMiddleHigh = 0x08;          // RE::AIProcess::middleHigh
		constexpr std::size_t kMiddleHighLastBoundWeapon = 0x450;   // RE::MiddleHighProcessData::lastBoundWeapon
		constexpr std::size_t kWeaponWeaponData = 0x248;            // RE::TESObjectWEAP::weaponData (BSTSmartPointer, _ptr at +0)
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

		std::uint64_t currentProcess = 0;
		if (!Read(a_actor, kActorCurrentProcess, currentProcess) || !currentProcess) {
			REX::INFO("[VATS] aimassist-probe: no currentProcess");
			return;
		}

		std::uint64_t middleHigh = 0;
		if (!Read(reinterpret_cast<const void*>(currentProcess), kAIProcessMiddleHigh, middleHigh) || !middleHigh) {
			REX::INFO("[VATS] aimassist-probe: no middleHigh");
			return;
		}

		std::uint64_t weapon = 0;
		if (!Read(reinterpret_cast<const void*>(middleHigh), kMiddleHighLastBoundWeapon, weapon) || !weapon) {
			REX::INFO("[VATS] aimassist-probe: no lastBoundWeapon (not wielding a weapon?)");
			return;
		}

		std::uint64_t instanceData = 0;
		if (!Read(reinterpret_cast<const void*>(weapon), kWeaponWeaponData, instanceData) || !instanceData) {
			REX::INFO("[VATS] aimassist-probe: weapon=0x{:X} has no weaponData", weapon);
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
