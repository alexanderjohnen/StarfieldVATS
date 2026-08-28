#include "CompanionSupport.h"

#include "ActorValueProbe.h"
#include "GameOffsets.h"
#include "HealthReader.h"
#include "SafeMem.h"
#include "AidItems.h"
#include "CompanionShield.h"
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
		// Visits each entry with (rawEntryBytes, boundObject, formID). The raw
		// pointer is passed through because the per-stack unit count lives
		// inside the entry and the callers that need it should not have to
		// walk the list a second time to reach it.
		template <class F>
		void ForEachInventoryItemEntry(RE::PlayerCharacter* a_player, F&& a_visit)
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

			// Generous, because this bound is a guard against a corrupt size
			// field - NOT a scan limit. It was 250 until 2026-08-29, which
			// quietly broke the item finder: Alexander inventory holds 417
			// entries, so two fifths of it were invisible and every buff item
			// happened to sit past the cut. Healing worked only because Med
			// Pack landed inside it. A cap meant for logging must never limit
			// a search.
			constexpr std::uint32_t kSanityCap = 8192;
			const std::uint32_t     scan = std::min(count, kSanityCap);
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
				a_visit(entry, reinterpret_cast<RE::TESBoundObject*>(object), formID);
			}
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
			ForEachInventoryItemEntry(a_player, [&](const std::byte*, RE::TESBoundObject* a_object, std::uint32_t a_formID) {
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

		// How many UNITS of this form ID the player carries, summed across
		// every stack of every matching entry.
		//
		// This counted ENTRIES until 2026-08-29, which made the first real
		// test worthless: with several Med Packs in one stack, removing one
		// leaves the entry standing, so "1 -> 1" meant both "nothing was
		// removed" and "one of five was removed" and could not tell them
		// apart. The comment there even claimed entries were enough to
		// distinguish the two. They are not.
		[[nodiscard]] int CountUnits(RE::PlayerCharacter* a_player, std::uint32_t a_formID)
		{
			int total = 0;
			ForEachInventoryItemEntry(a_player, [&](const std::byte* a_entry, RE::TESBoundObject*, std::uint32_t a_id) {
				if (a_id != a_formID) {
					return;
				}

				// stacks is a BSTArray<Stack>, same {size, capacity, data}
				// shape as every other array here; Stack carries the count.
				constexpr std::size_t kStacksOff = offsetof(RE::BGSInventoryItem, stacks);
				std::uint32_t         stackCount = 0;
				std::uint64_t         stackData = 0;
				if (!SafeRead(a_entry + kStacksOff, &stackCount, sizeof(stackCount)) ||
					!SafeRead(a_entry + kStacksOff + 8, &stackData, sizeof(stackData)) ||
					!stackData) {
					return;
				}
				for (std::uint32_t s = 0; s < std::min<std::uint32_t>(stackCount, 32); ++s) {
					std::uint32_t n = 0;
					const auto* stack = reinterpret_cast<const std::byte*>(stackData) +
						static_cast<std::size_t>(s) * sizeof(RE::BGSInventoryItem::Stack);
					if (SafeRead(stack + offsetof(RE::BGSInventoryItem::Stack, count), &n, sizeof(n))) {
						total += static_cast<int>(n);
					}
				}
				});
			return total;
		}
	}

	void CompanionSupport::ShieldActor(RE::Actor* a_actor)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		// Weakest first, same rule as healing: the long items keep for when
		// they are needed, and it also happens to waste the least against
		// the cap - a 300s item spent with 250s already banked throws most
		// of itself away.
		const AidItem*      item = nullptr;
		RE::TESBoundObject* object = nullptr;
		ForEachInventoryItemEntry(player, [&](const std::byte*, RE::TESBoundObject* a_object, std::uint32_t a_formID) {
			const auto* entry = FindAidItem(a_formID);
			if (!entry || entry->shieldSeconds <= 0.0f) {
				return;
			}
			if (!item || entry->shieldSeconds < item->shieldSeconds) {
				item = entry;
				object = a_object;
			}
			});

		if (!item || !object) {
			VATS_LOG("[shield] no buff item in inventory - nothing spent");
			return;
		}

		const auto before = CompanionShield::Get().GetState();
		if (before.remaining >= Settings::Get().shieldMaxSeconds - 0.5f) {
			VATS_LOG("[shield] already full ({:.0f}s) - not spending {}", before.remaining, item->name);
			return;
		}

		const int countBefore = CountUnits(player, item->formID);
		CompanionShield::Get().Add(a_actor, item->shieldSeconds);
		SpendItem(player, object);
		const int countAfter = CountUnits(player, item->formID);

		const auto after = CompanionShield::Get().GetState();
		VATS_LOG("[shield] formID=0x{:08X} +{:.0f}s from {}: {:.0f}s -> {:.0f}s of {:.0f}s | item units {} -> {}",
			a_actor->GetFormID(), item->shieldSeconds, item->name,
			before.remaining, after.remaining, after.capacity,
			countBefore, countAfter);
	}

	void CompanionSupport::ProbeDamageResist(RE::Actor* a_actor)
	{
		auto* avList = RE::ActorValue::GetSingleton();
		if (!avList || !avList->damageResist) {
			VATS_WARN("[dr] ActorValue singleton has no damageResist");
			return;
		}
		const auto& av = *avList->damageResist;
		const std::uint32_t formID = a_actor->GetFormID();

		constexpr float kProbeAmount = 500.0f;

		float valueBefore = -1.0f, modBefore = -1.0f;
		(void)TryGetActorValue(a_actor, av, valueBefore);
		(void)TryGetTemporaryModifier(a_actor, av, modBefore);

		if (!TryModTemporary(a_actor, av, kProbeAmount)) {
			VATS_WARN("[dr] formID=0x{:08X}: no verified ActorValueOwner, nothing applied", formID);
			return;
		}

		float valueAfter = -1.0f, modAfter = -1.0f;
		(void)TryGetActorValue(a_actor, av, valueAfter);
		(void)TryGetTemporaryModifier(a_actor, av, modAfter);

		// Put it straight back. This is a measurement, not a buff - leaving
		// +500 damage resistance on a companion because a probe ran would be
		// a gameplay change nobody asked for, and there is no timer to take
		// it off again yet.
		(void)TryModTemporary(a_actor, av, -kProbeAmount);

		float valueReverted = -1.0f, modReverted = -1.0f;
		(void)TryGetActorValue(a_actor, av, valueReverted);
		(void)TryGetTemporaryModifier(a_actor, av, modReverted);

		// Four numbers, and between them they settle it:
		//   value moves      -> writing damage resistance works, build the bar
		//   only mod moves   -> the modifier is stored but the value is
		//                       derived from armour and perks, so it never
		//                       reaches the damage calculation
		//   neither moves    -> the write does not land at all
		VATS_LOG("[dr] formID=0x{:08X} probe {:+.0f}: value {:.1f} -> {:.1f} -> {:.1f} | tempMod {:.1f} -> {:.1f} -> {:.1f}",
			formID, kProbeAmount,
			valueBefore, valueAfter, valueReverted,
			modBefore, modAfter, modReverted);
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
		// Full health: there is nothing to heal, so the tap does the other
		// thing. This is the shape the finished feature wants anyway -
		// Alexander design was that a healthy companion turns the prompt into
		// a buff - it just runs a measurement there for now instead of a
		// shield that may or may not do anything.
		{
			HealthReading hp{};
			if (GetActorHealth(a_actor, hp) && hp.max > 0.0f && hp.current >= hp.max - 0.5f) {
				ShieldActor(a_actor);
				return;
			}
		}

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

		const int countBefore = CountUnits(player, item->formID);
		if (!TryRestoreHealth(a_actor, amount)) {
			VATS_WARN("[support] formID=0x{:08X}: RestoreActorValue not reachable - item NOT spent", formID);
			return;
		}

		// Spent only after the heal actually went through, so a failure
		// cannot cost the player an item for nothing.
		SpendItem(player, object);
		const int countAfter = CountUnits(player, item->formID);

		HealthReading after{};
		const bool    haveAfter = GetActorHealth(a_actor, after);

		VATS_LOG("[support] healed formID=0x{:08X} using {} (0x{:08X}): {:.0f} percent of {:.0f} = {:.0f} | health {:.2f} -> {:.2f} | item units {} -> {}",
			formID, item->name, item->formID, item->healPercent, before.max, amount,
			before.current,
			haveAfter ? after.current : -1.0f,
			countBefore, countAfter);
	}
	void CompanionSupport::LogPlayerInventory()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		int total = 0;
		int aid = 0;
		ForEachInventoryItemEntry(player, [&](const std::byte*, RE::TESBoundObject* a_object, std::uint32_t a_formID) {
			++total;

			std::uint8_t formType = 0;
			(void)SafeRead(reinterpret_cast<const std::byte*>(a_object) + GameOffsets::kFormType, &formType, sizeof(formType));
			if (formType != GameOffsets::kFormTypeALCH) {
				return;
			}
			++aid;

			// Only aid items now, not the whole inventory. Listing all 417
			// entries buried the interesting six and hit a logging cap that
			// then quietly became a SEARCH cap - see
			// ForEachInventoryItemEntry. Aid items are the only kind this
			// feature can spend, so they are the only kind worth printing.
			const auto* known = FindAidItem(a_formID);
			VATS_LOG("[inv] aid formID=0x{:08X} units={} -> {}", a_formID,
				CountUnits(player, a_formID),
				known ? known->name : "NOT IN TABLE");
			});

		VATS_LOG("[inv] {} entries scanned, {} aid items", total, aid);
	}
}
