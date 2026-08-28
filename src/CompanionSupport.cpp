#include "CompanionSupport.h"

#include "ActorValueProbe.h"
#include "GameOffsets.h"
#include "HealthReader.h"
#include "SafeMem.h"
#include "AidItems.h"
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

	namespace
	{
		// Walks the player inventory and hands every readable entry to
		// a_visit as (boundObject, formID). Pure reads, SafeRead-guarded
		// throughout, so a wrong offset yields no entries rather than a
		// crash. Confirmed working 2026-08-28: 65 entries, all seven known
		// aid item form IDs present. See docs/FINDINGS.md.
		template <class F>
		void ForEachInventoryItem(RE::PlayerCharacter* a_player, F&& a_visit)
		{
			RE::BGSInventoryList* list = nullptr;
			if (!SafeRead(reinterpret_cast<const std::byte*>(a_player) + offsetof(RE::TESObjectREFR, inventoryList),
					&list, sizeof(list)) ||
				!list) {
				return;
			}

			constexpr std::size_t kDataOff = offsetof(RE::BGSInventoryList, data);
			std::uint32_t         count = 0;
			std::uint64_t         items = 0;
			if (!SafeRead(reinterpret_cast<const std::byte*>(list) + kDataOff, &count, sizeof(count)) ||
				!SafeRead(reinterpret_cast<const std::byte*>(list) + kDataOff + 8, &items, sizeof(items)) ||
				!items) {
				return;
			}

			constexpr std::uint32_t kCap = 250;
			const std::uint32_t     scan = std::min(count, kCap);
			for (std::uint32_t i = 0; i < scan; ++i) {
				const auto* entry = reinterpret_cast<const std::byte*>(items) +
					static_cast<std::size_t>(i) * sizeof(RE::BGSInventoryItem);

				std::uint64_t object = 0;
				if (!SafeRead(entry + offsetof(RE::BGSInventoryItem, object), &object, sizeof(object)) || !object) {
					continue;
				}
				std::uint32_t formID = 0;
				if (!SafeRead(reinterpret_cast<const std::byte*>(object) + offsetof(RE::TESForm, formID), &formID, sizeof(formID))) {
					continue;
				}
				a_visit(reinterpret_cast<RE::TESBoundObject*>(object), formID);
			}
		}
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

	namespace
	{
		// Finds the WEAKEST heal item the player is carrying, so the good
		// ones survive for when they are needed. Returns the bound object to
		// spend and the table entry describing it.
		[[nodiscard]] const AidItem* FindWeakestHealItem(RE::PlayerCharacter* a_player, RE::TESBoundObject*& a_outObject)
		{
			const AidItem* best = nullptr;
			a_outObject = nullptr;
			ForEachInventoryItem(a_player, [&](RE::TESBoundObject* a_object, std::uint32_t a_formID) {
				const auto* entry = FindAidItem(a_formID);
				if (!entry || entry->healPercent <= 0.0f) {
					return;
				}
				if (!best || entry->healPercent < best->healPercent) {
					best = entry;
					a_outObject = a_object;
				}
			});
			return best;
		}

		// The first engine call in this project that hands the engine a
		// STRUCT we built rather than reading bytes out of one. That is a
		// step up in risk from everything before it: a wrong field offset in
		// a read degrades to nonsense, here it corrupts a call.
		//
		// What makes it acceptable: RemoveItem is a plain virtual (vtable
		// slot 08B), so no Address Library is involved, and CommonLibSF types
		// RemoveItemRequest field by field WITH a static_assert on its total
		// size (0x40). The layout is therefore at least internally
		// consistent rather than a guess - which is more than could be said
		// for the offsets that burned this project before.
		//
		// Verified by counting: the caller logs how many of the item are in
		// the inventory before and after, so a call that silently does
		// nothing is visible immediately rather than inferred.
		void SpendItem(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object)
		{
			RE::RemoveItemRequest request{};
			request.object = a_object;
			request.count = 1;
			request.reason = RE::ITEM_TRANSFER_REASON::kNone;

			std::uint32_t outHandle = 0;
			a_player->RemoveItem(outHandle, request);
		}

		// How many entries in the inventory carry this form ID. Entries, not
		// units - the per-stack count has not been verified yet - but that is
		// enough to tell "the call removed something" from "the call did
		// nothing".
		[[nodiscard]] int CountEntries(RE::PlayerCharacter* a_player, std::uint32_t a_formID)
		{
			int n = 0;
			ForEachInventoryItem(a_player, [&](RE::TESBoundObject*, std::uint32_t a_id) {
				if (a_id == a_formID) {
					++n;
				}
				});
			return n;
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
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		RE::TESBoundObject* object = nullptr;
		const AidItem*      item = FindWeakestHealItem(player, object);
		if (!item || !object) {
			VATS_LOG("[support] no heal item in inventory - nothing spent, nothing healed");
			return;
		}

		HealthReading before{};
		const bool    haveBefore = GetActorHealth(a_actor, before);
		if (!haveBefore || !(before.max > 0.0f)) {
			VATS_WARN("[support] formID=0x{:08X}: cannot read max health, refusing to heal blind", formID);
			return;
		}

		// Percentage of MAX health, so a heal stays worth pressing as
		// companions level - see AidItems.h.
		const float amount = before.max * (item->healPercent / 100.0f);

		const int countBefore = CountEntries(player, item->formID);
		if (!TryRestoreHealth(a_actor, amount)) {
			VATS_WARN("[support] formID=0x{:08X}: RestoreActorValue not reachable - item NOT spent", formID);
			return;
		}

		// Spent only after the heal actually went through, so a failure
		// cannot cost the player an item for nothing.
		SpendItem(player, object);
		const int countAfter = CountEntries(player, item->formID);

		HealthReading after{};
		const bool    haveAfter = GetActorHealth(a_actor, after);

		VATS_LOG("[support] healed formID=0x{:08X} using {} (0x{:08X}): {:.0f} percent of {:.0f} = {:.0f} | health {:.2f} -> {:.2f} | item entries {} -> {}",
			formID, item->name, item->formID, item->healPercent, before.max, amount,
			before.current,
			haveAfter ? after.current : -1.0f,
			countBefore, countAfter);
	}
}
