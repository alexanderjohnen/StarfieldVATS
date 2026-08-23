#include "HealthReader.h"

#include "SafeMem.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace VATS
{
	namespace
	{
		constexpr std::size_t kBaseValueStride = 16;  // ActorValueInfo* (8) + float (4), 8-byte aligned
		constexpr std::size_t kModifierStride = 24;   // ActorValueInfo* (8) + Modifiers (12), 8-byte aligned

		// avStorage.baseValues/modifiers are declared by CommonLibSF's
		// header as BSTArray<BSTTuple<uint32_t, T>> (key = a 4-byte AV
		// index), but empirical inspection (2026-08-23, cross-checked
		// against Alexander's `getav health`=480 on a locked target) showed
		// that reading with that assumed 8-byte stride produces a
		// corrupted-looking sequence: "key" values that cluster tightly
		// with a constant high 16 bits (the signature of the low 32 bits of
		// consecutive pointers into one contiguous table, not a small
		// integer index), paired with "values" that are almost always ~0.0
		// (consistent with reading the high 32 bits of those same 8-byte
		// pointers as if they were a float). The array's own {size,
		// capacity, data} header at avStorage's claimed offset (0x260) read
		// identically via two independent methods (the typed accessor and a
		// raw dump), so that part of the header is trusted - the bug is
		// specifically the assumed element type/stride. Real key type is
		// almost certainly ActorValueInfo* (8 bytes) instead. Reads raw via
		// SafeRead rather than the header's mistyped BSTTuple, matching this
		// project's established "don't trust an unverified layout without a
		// guard" policy (SafeMem.h) - if this hypothesis is also wrong, a
		// miss degrades to "no entry found", never a crash.
		template <class T, class ArrayT>
		[[nodiscard]] bool FindByAvPointer(const ArrayT& a_array, const RE::ActorValueInfo* a_key, std::size_t a_stride, T& a_out)
		{
			const auto*          dataPtr = reinterpret_cast<const std::byte*>(a_array.data());
			const std::uint32_t  count = a_array.size();
			for (std::uint32_t i = 0; i < count; ++i) {
				const std::byte*           entry = dataPtr + static_cast<std::size_t>(i) * a_stride;
				const RE::ActorValueInfo*  key = nullptr;
				if (!SafeRead(entry, &key, sizeof(key))) {
					continue;
				}
				if (key == a_key) {
					return SafeRead(entry + sizeof(key), &a_out, sizeof(a_out));
				}
			}
			return false;
		}

		// Fallback diagnostic if FindByAvPointer's new hypothesis is also
		// wrong: dumps raw floats starting at the array's own data pointer
		// (the heap-allocated element buffer itself, NOT the Actor object -
		// an earlier version of this dump wrongly scanned around the Actor
		// object's own memory, which could never have found a real entry's
		// value regardless of stride, since elements live on the heap the
		// data pointer points to). A known-good value (Alexander's
		// `getav health`) can then be located by inspection - same
		// technique ProjectileTracker.cpp used to correct RE::Projectile's
		// offsets.
		void DumpRawFloatWindow(const void* a_dataPtr, std::size_t a_byteCount)
		{
			const auto* base = reinterpret_cast<const std::byte*>(a_dataPtr);
			std::string line;
			char        cell[40];
			int         perLine = 0;
			for (std::size_t off = 0; off < a_byteCount; off += 4) {
				std::uint32_t raw = 0;
				if (!SafeRead(base + off, &raw, sizeof(raw))) {
					continue;
				}
				float f = 0.0f;
				std::memcpy(&f, &raw, sizeof(f));
				std::snprintf(cell, sizeof(cell), "%03zX:%08X(%.2f) ", off, raw, f);
				line += cell;
				if (++perLine >= 6) {
					REX::INFO("[VATS] health raw dump: {}", line);
					line.clear();
					perLine = 0;
				}
			}
			if (!line.empty()) {
				REX::INFO("[VATS] health raw dump: {}", line);
			}
		}
	}

	bool GetActorHealth(RE::Actor* a_actor, HealthReading& a_out)
	{
		if (!a_actor) {
			return false;
		}

		auto* avList = RE::ActorValue::GetSingleton();
		if (!avList || !avList->health) {
			REX::ERROR("[VATS] health: ActorValue singleton or ->health ActorValueInfo unavailable");
			return false;
		}
		const RE::ActorValueInfo* healthInfo = avList->health;

		float base = 0.0f;
		if (!FindByAvPointer(a_actor->avStorage.baseValues, healthInfo, kBaseValueStride, base)) {
			// Diagnostic (2026-08-23): dump once per actor if the new
			// pointer-keyed hypothesis still doesn't find health. Remove
			// once GetActorHealth is confirmed working.
			static std::unordered_set<std::uint32_t> s_logged;
			const std::uint32_t                       formID = a_actor->GetFormID();
			if (s_logged.insert(formID).second) {
				const auto& arr = a_actor->avStorage.baseValues;
				REX::WARN("[VATS] health: no baseValues entry for healthInfo={} on formID=0x{:08X}, size={}, dumping raw element buffer",
					static_cast<const void*>(healthInfo), formID, arr.size());
				DumpRawFloatWindow(arr.data(), std::min<std::size_t>(arr.size(), 64) * kBaseValueStride);
			}
			return false;
		}

		// Modifiers entry is optional - an actor that's never taken/healed
		// damage and has no permanent/temporary health bonuses simply has no
		// entry at all, which is a legitimate "no modifiers" case (all three
		// stay 0), not a failure. Diagnostic (2026-08-23): the bar Alexander
		// tested never moved (always full), suggesting either no entry is
		// ever found here (kModifierStride=24 was inferred by analogy to
		// baseValues' confirmed 16, never independently checked) or the
		// found entry's 3 slots aren't in the [permanent, temporary, damage]
		// order ACTOR_VALUE_MODIFIER assumes - logs whichever is true once
		// per actor. Remove once the bar is confirmed tracking real damage.
		RE::Modifiers mods{};
		float         permanent = 0.0f, temporary = 0.0f, damage = 0.0f;
		const bool    foundMods = FindByAvPointer(a_actor->avStorage.modifiers, healthInfo, kModifierStride, mods);
		if (foundMods) {
			permanent = mods.modifiers[static_cast<std::size_t>(RE::ACTOR_VALUE_MODIFIER::kPermanent)];
			temporary = mods.modifiers[static_cast<std::size_t>(RE::ACTOR_VALUE_MODIFIER::kTemporary)];
			damage = mods.modifiers[static_cast<std::size_t>(RE::ACTOR_VALUE_MODIFIER::kDamage)];
		}
		{
			// Logs on change rather than once-ever-per-actor, specifically
			// so a full test (target at full health, then damaged) shows
			// whether "damage" ever moves at all, not just its value at
			// first sighting.
			static std::unordered_map<std::uint32_t, float> s_lastLoggedCurrent;
			const std::uint32_t                              formID = a_actor->GetFormID();
			const float                                      current = base + permanent + temporary + damage;
			const auto                                        it = s_lastLoggedCurrent.find(formID);
			if (it == s_lastLoggedCurrent.end() || it->second != current) {
				const auto& arr = a_actor->avStorage.modifiers;
				REX::INFO("[VATS] health modifiers: formID=0x{:08X} base={:.1f} found={} slots=[{:.1f},{:.1f},{:.1f}] current={:.1f} modifiers.size={}",
					formID, base, foundMods, mods.modifiers[0], mods.modifiers[1], mods.modifiers[2], current, arr.size());
				s_lastLoggedCurrent[formID] = current;
				if (!foundMods) {
					DumpRawFloatWindow(arr.data(), std::min<std::size_t>(arr.size(), 64) * kModifierStride);
				}
			}
		}

		a_out.max = base + permanent + temporary;
		a_out.current = a_out.max + damage;  // damage modifier is stored negative
		return true;
	}

	std::uint32_t GetActorExtraHealthSegments(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return 0;
		}

		auto* avList = RE::ActorValue::GetSingleton();
		if (!avList || !avList->legendaryRank) {
			return 0;
		}

		float rank = 0.0f;
		if (!FindByAvPointer(a_actor->avStorage.baseValues, avList->legendaryRank, kBaseValueStride, rank)) {
			return 0;
		}
		return rank > 0.0f ? static_cast<std::uint32_t>(rank + 0.5f) : 0;
	}
}
