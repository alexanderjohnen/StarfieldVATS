#include "HealthReader.h"

#include <cstdio>
#include <string>
#include <unordered_set>

namespace VATS
{
	namespace
	{
		constexpr std::uint32_t kUnresolved = 0xFFFFFFFF;
		std::atomic<std::uint32_t> s_healthIndex{ kUnresolved };

		// RE::ActorValue::GetSingleton()->health is a fixed ActorValueInfo*
		// for the whole process lifetime once resolved - cache the index
		// instead of re-walking the singleton and its ->index field on
		// every HUD frame.
		[[nodiscard]] bool ResolveHealthIndex(std::uint32_t& a_out)
		{
			const std::uint32_t cached = s_healthIndex.load(std::memory_order_relaxed);
			if (cached != kUnresolved) {
				a_out = cached;
				return true;
			}

			auto* avList = RE::ActorValue::GetSingleton();
			if (!avList || !avList->health) {
				REX::ERROR("[VATS] health: ActorValue singleton or ->health ActorValueInfo unavailable");
				return false;
			}
			const std::uint32_t index = avList->health->index;
			s_healthIndex.store(index, std::memory_order_relaxed);
			REX::INFO("[VATS] health: resolved health AV index={}", index);
			a_out = index;
			return true;
		}
	}

	bool GetActorHealth(RE::Actor* a_actor, HealthReading& a_out)
	{
		if (!a_actor) {
			return false;
		}

		std::uint32_t healthIndex = 0;
		if (!ResolveHealthIndex(healthIndex)) {
			return false;
		}

		float base = 0.0f;
		bool  foundBase = false;
		for (const auto& entry : a_actor->avStorage.baseValues) {
			if (entry.first == healthIndex) {
				base = entry.second;
				foundBase = true;
				break;
			}
		}
		if (!foundBase) {
			// Diagnostic (2026-08-23): no lookup hit at all in Alexander's
			// first test despite the index resolving cleanly - dump the
			// actual array contents once per actor so the next test's log
			// says whether healthIndex is just missing from a populated,
			// sane-looking array (a legitimately different value than 24)
			// or the array itself is empty/garbage (avStorage's offset on
			// Actor is wrong - see HealthReader.h's residual-risk comment).
			// Remove once GetActorHealth is confirmed working.
			static std::unordered_set<std::uint32_t> s_logged;
			const std::uint32_t                       formID = a_actor->GetFormID();
			if (s_logged.insert(formID).second) {
				const auto& arr = a_actor->avStorage.baseValues;
				std::string dump;
				char        pair[32];
				std::size_t n = 0;
				for (const auto& entry : arr) {
					if (n++ >= 12) {
						break;
					}
					std::snprintf(pair, sizeof(pair), "[%u]=%.1f ", entry.first, entry.second);
					dump += pair;
				}
				REX::WARN("[VATS] health: no baseValues entry for index={} on formID=0x{:08X}, size={}, first entries: {}",
					healthIndex, formID, arr.size(), dump);
			}
			return false;
		}

		// Modifiers entry is optional - an actor that's never taken/healed
		// damage and has no permanent/temporary health bonuses simply has no
		// entry at all, which is a legitimate "no modifiers" case (all three
		// stay 0), not a failure.
		float permanent = 0.0f, temporary = 0.0f, damage = 0.0f;
		for (const auto& entry : a_actor->avStorage.modifiers) {
			if (entry.first == healthIndex) {
				const auto& mods = entry.second.modifiers;
				permanent = mods[static_cast<std::size_t>(RE::ACTOR_VALUE_MODIFIER::kPermanent)];
				temporary = mods[static_cast<std::size_t>(RE::ACTOR_VALUE_MODIFIER::kTemporary)];
				damage = mods[static_cast<std::size_t>(RE::ACTOR_VALUE_MODIFIER::kDamage)];
				break;
			}
		}

		a_out.max = base + permanent + temporary;
		a_out.current = a_out.max + damage;  // damage modifier is stored negative
		return true;
	}

	namespace
	{
		std::atomic<std::uint32_t> s_legendaryRankIndex{ kUnresolved };

		[[nodiscard]] bool ResolveLegendaryRankIndex(std::uint32_t& a_out)
		{
			const std::uint32_t cached = s_legendaryRankIndex.load(std::memory_order_relaxed);
			if (cached != kUnresolved) {
				a_out = cached;
				return true;
			}

			auto* avList = RE::ActorValue::GetSingleton();
			if (!avList || !avList->legendaryRank) {
				return false;
			}
			const std::uint32_t index = avList->legendaryRank->index;
			s_legendaryRankIndex.store(index, std::memory_order_relaxed);
			REX::INFO("[VATS] health: resolved legendaryRank AV index={}", index);
			a_out = index;
			return true;
		}
	}

	std::uint32_t GetActorExtraHealthSegments(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return 0;
		}

		std::uint32_t rankIndex = 0;
		if (!ResolveLegendaryRankIndex(rankIndex)) {
			return 0;
		}

		for (const auto& entry : a_actor->avStorage.baseValues) {
			if (entry.first == rankIndex) {
				return entry.second > 0.0f ? static_cast<std::uint32_t>(entry.second + 0.5f) : 0;
			}
		}
		return 0;
	}
}
