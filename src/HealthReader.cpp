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

		// avStorage.baseValues is declared by CommonLibSF's header as
		// BSTArray<BSTTuple<uint32_t, float>> (key = a 4-byte AV index),
		// but empirical inspection (2026-08-23, cross-checked against
		// Alexander's `getav health`=480 on a locked target) showed that
		// reading with that assumed 8-byte stride produces a corrupted-
		// looking sequence: "key" values that cluster tightly with a
		// constant high 16 bits (the signature of the low 32 bits of
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

		// Fallback diagnostic if FindByAvPointer's stride hypothesis is
		// ever wrong for a value: dumps raw floats starting at the array's
		// own data pointer (the heap-allocated element buffer itself, NOT
		// the Actor object). Same technique ProjectileTracker.cpp used to
		// correct RE::Projectile's offsets.
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

		// Diagnostic (2026-08-23) revealed avStorage.modifiers never has a
		// health entry even after Alexander damaged the target, and
		// baseValues' health entry read exactly equal to `getav health`
		// (the console's already-fully-modified current value) at first
		// sighting - together, that means Starfield writes damage straight
		// into baseValues for health rather than layering it on via an
		// ACTOR_VALUE_MODIFIER::kDamage entry the way older Creation Engine
		// titles do. So this reads the live current value directly, and
		// tracks "max" ourselves as the highest value ever observed for
		// this actor (updates upward too, in case of overheal/regen) -
		// accurate for any target first seen at full health, the common
		// case; a target re-locked mid-fight after already being damaged
		// will show a max that's really just "however much health it had
		// when we started watching it," a known, acceptable limitation.
		float current = 0.0f;
		if (!FindByAvPointer(a_actor->avStorage.baseValues, healthInfo, kBaseValueStride, current)) {
			// Diagnostic (2026-08-23): dump once per actor if the pointer-
			// keyed hypothesis ever fails to find health. Remove once
			// GetActorHealth is confirmed working.
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

		static std::unordered_map<std::uint32_t, float> s_maxSeen;
		const std::uint32_t                              formID = a_actor->GetFormID();
		float&                                            maxSeen = s_maxSeen[formID];
		if (current > maxSeen) {
			maxSeen = current;
		}

		// Diagnostic (2026-08-23): the rework compiles and returns true
		// (no WARN/ERROR fired in Alexander's last test) but the bar still
		// didn't move despite confirmed HITs in the log - logs the actual
		// computed current/max on change so the next test says whether
		// `current` genuinely never changes (the read itself is wrong/stale)
		// or it does change but the HUD isn't reflecting it (a drawing bug
		// in Overlay.cpp instead). Remove once the bar is confirmed
		// tracking real damage.
		static std::unordered_map<std::uint32_t, float> s_lastLoggedCurrent;
		const auto                                        it = s_lastLoggedCurrent.find(formID);
		if (it == s_lastLoggedCurrent.end() || it->second != current) {
			REX::INFO("[VATS] health: formID=0x{:08X} current={:.1f} max={:.1f}", formID, current, maxSeen);
			s_lastLoggedCurrent[formID] = current;
		}

		a_out.current = current;
		a_out.max = maxSeen;
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
