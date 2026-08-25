#include "HealthReader.h"

#include "ActorValueProbe.h"
#include "SafeMem.h"

#include <algorithm>
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

		// baseValues supplies MAX health only. Establishing that took
		// several sessions: its health entry reads correct at full HP and
		// then never moves again across an entire fight with confirmed hits
		// landing, which looked like a working "current" read purely
		// because current and max are equal at full health - exactly when
		// the original console cross-check happened.
		float current = 0.0f;
		if (!FindByAvPointer(a_actor->avStorage.baseValues, healthInfo, kBaseValueStride, current)) {
			static std::unordered_set<std::uint32_t> s_logged;
			if (s_logged.insert(a_actor->GetFormID()).second) {
				REX::WARN("[VATS] health: no baseValues entry for healthInfo={} on formID=0x{:08X}, size={}",
					static_cast<const void*>(healthInfo), a_actor->GetFormID(), a_actor->avStorage.baseValues.size());
			}
			return false;
		}

		const std::uint32_t formID = a_actor->GetFormID();

		// baseValues' health entry is MAX health - established 2026-08-25
		// by watching it stay pinned at its full-HP value across an entire
		// real fight with confirmed hits landing. The running-highest guard
		// stays anyway, so a temporary buff can only ever grow the scale.
		static std::unordered_map<std::uint32_t, float> s_maxSeen;
		float&                                          maxSeen = s_maxSeen[formID];
		if (current > maxSeen) {
			maxSeen = current;
		}

		// Live current health, via the engine's own accessor rather than
		// any data field - see ActorValueProbe.h for the full reasoning
		// (short version: Techrunner displays live health and ships no
		// SFSE plugin at all, so Papyrus' Actor.GetValue, and therefore
		// ActorValueOwner::GetActorValue, is where the real number lives;
		// there was never a hidden field to find). Falls back to the
		// baseValues number - i.e. max - if the ActorValueOwner sub-object
		// can't be identified, so the bar degrades to "always full"
		// instead of vanishing.
		float      liveCurrent = 0.0f;
		const bool haveLive = TryGetLiveHealth(a_actor, liveCurrent);
		if (haveLive) {
			current = liveCurrent;
			if (current > maxSeen) {
				maxSeen = current;
			}
		}

		// Log-on-change, kept permanently: this is the one line that shows
		// whether the live read is still working, and a silent regression
		// here would take the health bar and the death trigger with it.
		static std::unordered_map<std::uint32_t, float> s_lastLoggedCurrent;
		const auto                                      it = s_lastLoggedCurrent.find(formID);
		if (it == s_lastLoggedCurrent.end() || it->second != current) {
			REX::INFO("[VATS] health: formID=0x{:08X} current={:.2f} max={:.1f} (live={})", formID, current, maxSeen, haveLive);
			s_lastLoggedCurrent[formID] = current;
		}

		a_out.current = current;
		a_out.max = maxSeen;
		return true;
	}

	bool GetActorBaseValue(RE::Actor* a_actor, const RE::ActorValueInfo* a_info, float& a_out)
	{
		if (!a_actor || !a_info) {
			return false;
		}
		return FindByAvPointer(a_actor->avStorage.baseValues, a_info, kBaseValueStride, a_out);
	}
}
