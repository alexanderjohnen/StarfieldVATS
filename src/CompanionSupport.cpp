#include "CompanionSupport.h"

#include "ActorValueProbe.h"
#include "GameOffsets.h"
#include "HealthReader.h"
#include "SafeMem.h"
#include "Settings.h"
#include "VATSController.h"

namespace VATS
{
	void CompanionSupport::RequestAction()
	{
		const auto* tasks = SFSE::GetTaskInterface();
		if (!tasks) {
			VATS_ERROR("[support] task interface unavailable, cannot act");
			return;
		}
		tasks->AddTask([]() {
			auto  state = Controller::Get().GetOverlayState();
			if (state.mode != VATSMode::kSupport || !state.actor) {
				VATS_LOG("[support] task ran but session was gone (mode ok={}, actor ok={})",
					state.mode == VATSMode::kSupport, static_cast<bool>(state.actor));
				return;
			}
			HealActor(state.actor.get());
		});
	}

	void CompanionSupport::LogPlayerInventory()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		// Offsets taken from the CommonLibSF headers rather than probed,
		// which is unusual for this project - justified here because these
		// particular types carry static_asserts on their own size
		// (BGSInventoryItem == 0x28, Stack == 0x10), so the layout is at
		// least internally consistent rather than a bare guess. Every read is
		// still SafeRead-guarded, so a wrong offset degrades to "no items
		// listed" instead of a crash.
		RE::BGSInventoryList* list = nullptr;
		if (!SafeRead(reinterpret_cast<const std::byte*>(player) + offsetof(RE::TESObjectREFR, inventoryList),
				&list, sizeof(list)) ||
			!list) {
			VATS_WARN("[inv] no inventory list on the player");
			return;
		}

		constexpr std::size_t kDataOff = offsetof(RE::BGSInventoryList, data);
		std::uint32_t         count = 0;
		std::uint64_t         items = 0;
		if (!SafeRead(reinterpret_cast<const std::byte*>(list) + kDataOff, &count, sizeof(count)) ||
			!SafeRead(reinterpret_cast<const std::byte*>(list) + kDataOff + 8, &items, sizeof(items)) ||
			!items) {
			VATS_WARN("[inv] inventory array unreadable (count={})", count);
			return;
		}

		constexpr std::uint32_t kCap = 250;
		const std::uint32_t     scan = std::min(count, kCap);
		VATS_LOG("[inv] player inventory: {} entries (listing {})", count, scan);

		for (std::uint32_t i = 0; i < scan; ++i) {
			const auto* entry = reinterpret_cast<const std::byte*>(items) + 
				static_cast<std::size_t>(i) * sizeof(RE::BGSInventoryItem);

			std::uint64_t object = 0;
			if (!SafeRead(entry + offsetof(RE::BGSInventoryItem, object), &object, sizeof(object)) || !object) {
				continue;
			}

			std::uint8_t  formType = 0;
			std::uint32_t formID = 0;
			(void)SafeRead(reinterpret_cast<const std::byte*>(object) + GameOffsets::kFormType, &formType, sizeof(formType));
			(void)SafeRead(reinterpret_cast<const std::byte*>(object) + offsetof(RE::TESForm, formID), &formID, sizeof(formID));

			// Stack count: the stacks array header, same {size, capacity,
			// data} shape every other array in this project uses.
			std::uint32_t stackCount = 0;
			(void)SafeRead(entry + offsetof(RE::BGSInventoryItem, stacks), &stackCount, sizeof(stackCount));

			VATS_LOG("[inv] formID=0x{:08X} formType={} stacks={}", formID, formType, stackCount);
		}
	}

	void CompanionSupport::HealActor(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}
		const std::uint32_t formID = a_actor->GetFormID();

		// Read before and after through the path that is already trusted.
		// This is what turns the press into a measurement: if before and
		// after are identical, RestoreActorValue did not do what its name
		// says on this build, and that is worth knowing immediately rather
		// than inferring from a health bar.
		HealthReading before{};
		const bool    haveBefore = GetActorHealth(a_actor, before);

		const float amount = Settings::Get().supportHealAmount;
		if (!TryRestoreHealth(a_actor, amount)) {
			VATS_WARN("[support] formID=0x{:08X}: RestoreActorValue not reachable (no verified ActorValueOwner)", formID);
			return;
		}

		HealthReading after{};
		const bool    haveAfter = GetActorHealth(a_actor, after);

		VATS_LOG("[support] healed formID=0x{:08X} by {:.0f}: {:.2f} -> {:.2f} (max {:.0f}, read ok={}/{})",
			formID, amount,
			haveBefore ? before.current : -1.0f,
			haveAfter ? after.current : -1.0f,
			haveAfter ? after.max : -1.0f,
			haveBefore, haveAfter);
	}
}
