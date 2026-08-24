#include "CombatTargetOverride.h"

#include "GameOffsets.h"
#include "SafeMem.h"

namespace VATS
{
	namespace
	{
		std::uint32_t s_originalValue = 0;
		bool          s_engaged = false;
	}

	void CombatTargetOverride::Engage(RE::Actor* a_target)
	{
		if (!a_target) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		auto* addr = reinterpret_cast<std::byte*>(player) + GameOffsets::kCurrentCombatTarget;
		std::uint32_t original = 0;
		if (!SafeRead(addr, &original, sizeof(original))) {
			return;
		}

		const std::uint32_t formID = a_target->GetFormID();
		if (!SafeWrite(addr, &formID, sizeof(formID))) {
			return;
		}

		s_originalValue = original;
		s_engaged = true;
		REX::INFO("[VATS] combat-target override: engaged, wrote formID=0x{:08X} (was 0x{:08X})", formID, original);
	}

	void CombatTargetOverride::Refresh(RE::Actor* a_target)
	{
		if (!s_engaged || !a_target) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		auto*        addr = reinterpret_cast<std::byte*>(player) + GameOffsets::kCurrentCombatTarget;
		const std::uint32_t formID = a_target->GetFormID();
		(void)SafeWrite(addr, &formID, sizeof(formID));
	}

	void CombatTargetOverride::Disengage()
	{
		if (!s_engaged) {
			return;
		}
		s_engaged = false;

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		auto* addr = reinterpret_cast<std::byte*>(player) + GameOffsets::kCurrentCombatTarget;
		const bool wrote = SafeWrite(addr, &s_originalValue, sizeof(s_originalValue));
		REX::INFO("[VATS] combat-target override: disengaged, restored 0x{:08X} (ok={})", s_originalValue, wrote);
	}
}
