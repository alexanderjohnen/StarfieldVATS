#include "CombatTargetOverride.h"

#include "GameOffsets.h"
#include "SafeMem.h"

#include <chrono>
#include <thread>

namespace VATS
{
	namespace
	{
		std::uint32_t s_originalValue = 0;

		constexpr auto kWriteInterval = std::chrono::milliseconds(5);
	}

	void CombatTargetOverride::ThreadProc(const std::stop_token& a_stop, std::uint32_t a_formID)
	{
		while (!a_stop.stop_requested()) {
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				auto* addr = reinterpret_cast<std::byte*>(player) + GameOffsets::kCurrentCombatTarget;
				(void)SafeWrite(addr, &a_formID, sizeof(a_formID));
			}
			std::this_thread::sleep_for(kWriteInterval);
		}
	}

	void CombatTargetOverride::Engage(RE::Actor* a_target)
	{
		if (!a_target || m_thread.joinable()) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		auto*         addr = reinterpret_cast<std::byte*>(player) + GameOffsets::kCurrentCombatTarget;
		std::uint32_t original = 0;
		if (!SafeRead(addr, &original, sizeof(original))) {
			return;
		}
		s_originalValue = original;

		const std::uint32_t formID = a_target->GetFormID();
		VATS_LOG("[VATS] combat-target override: engaged, forcing formID=0x{:08X} every {}ms (was 0x{:08X})",
			formID, kWriteInterval.count(), original);
		m_thread = std::jthread(&CombatTargetOverride::ThreadProc, formID);
	}

	void CombatTargetOverride::Disengage()
	{
		if (!m_thread.joinable()) {
			return;
		}
		m_thread.request_stop();
		m_thread.join();

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		auto*      addr = reinterpret_cast<std::byte*>(player) + GameOffsets::kCurrentCombatTarget;
		const bool wrote = SafeWrite(addr, &s_originalValue, sizeof(s_originalValue));
		VATS_LOG("[VATS] combat-target override: disengaged, restored 0x{:08X} (ok={})", s_originalValue, wrote);
	}
}
