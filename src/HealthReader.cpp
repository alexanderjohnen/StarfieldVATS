#include "HealthReader.h"

#include "SafeMem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace VATS
{
	namespace
	{
		constexpr std::size_t kBaseValueStride = 16;  // ActorValueInfo* (8) + float (4), 8-byte aligned

		// avStorage.modifiers' real element stride, by the same reasoning
		// already proven for baseValues: real key is an 8-byte
		// ActorValueInfo* (not the header's claimed 4-byte uint32_t
		// index), payload is RE::Modifiers (confirmed sizeof 0xC - three
		// floats: permanent/temporary/damage, see ActorValueStorage.h).
		// 8 (key) + 12 (payload) = 20, rounded to 24 for 8-byte alignment
		// - the same pattern baseValues used (12 real bytes -> 16 stride).
		// UNLIKE baseValues' 16, this number has never been independently
		// confirmed (no raw-dump cross-check, no getav comparison) - it's
		// an analogy, not a proven fact. 2026-08-23's original modifiers
		// search already used this exact value and pointer-keyed lookup
		// and found nothing; 2026-08-25's baseValues finding (current
		// reads once at full health, then genuinely never changes across
		// an entire real fight - confirmed via redirect-hit correlation
		// in the same log) means "damage writes into baseValues directly"
		// no longer holds up, so modifiers is worth re-checking with a
		// live, throttled probe instead of the old one-shot check - and,
		// if it still never resolves, this stride guess itself becomes a
		// real suspect rather than baseValues being the answer.
		constexpr std::size_t kModifierStride = 24;

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

		// Re-checking modifiers live (2026-08-25) - see kModifierStride's
		// comment for why. Logs the 3-float payload on change, so this
		// answers definitively: if a `damage` component (index 2) ever
		// moves as the target takes real damage, THAT'S the live value to
		// use instead of baseValues; if this array never has a health
		// entry at all across a whole real fight (not just once, like the
		// original 2026-08-23 check), the 24-byte stride guess itself is
		// the next suspect, not "modifiers doesn't hold health" - so a
		// one-time raw dump fires the first time it's checked per actor,
		// regardless of whether an entry was found, to have real bytes to
		// inspect either way.
		{
			RE::Modifiers mods{};
			const bool    foundMods = FindByAvPointer(a_actor->avStorage.modifiers, healthInfo, kModifierStride, mods);
			static std::unordered_map<std::uint32_t, std::array<float, 3>> s_lastLoggedMods;
			const std::array<float, 3>                                     nowMods{ mods.modifiers[0], mods.modifiers[1], mods.modifiers[2] };
			const auto                                                     modIt = s_lastLoggedMods.find(formID);
			if (!foundMods || modIt == s_lastLoggedMods.end() || modIt->second != nowMods) {
				REX::INFO("[VATS] health modifiers: formID=0x{:08X} found={} permanent={:.1f} temporary={:.1f} damage={:.1f} (baseValues current={:.1f})",
					formID, foundMods, nowMods[0], nowMods[1], nowMods[2], current);
				s_lastLoggedMods[formID] = nowMods;
			}

			static std::unordered_set<std::uint32_t> s_dumped;
			if (s_dumped.insert(formID).second) {
				const auto& arr = a_actor->avStorage.modifiers;
				REX::INFO("[VATS] health modifiers: raw dump for formID=0x{:08X}, size={}, healthInfo={}",
					formID, arr.size(), static_cast<const void*>(healthInfo));
				DumpRawFloatWindow(arr.data(), std::min<std::size_t>(arr.size(), 32) * kModifierStride);
			}
		}

		// Diagnostic, kept intentionally live (not "remove once confirmed" -
		// restored 2026-08-25 specifically *because* nobody ever confirmed
		// whether `current` genuinely changes across a real fight or stays
		// pinned to its first-seen value before this file was deleted).
		// Logs the actual computed current/max on change, so the next
		// in-game test settles it definitively: if this line never repeats
		// with a different `current` during a real fight, the read itself
		// is stale/wrong; if it does change but the HUD bar still doesn't
		// move, the bug is in Overlay.cpp's drawing instead.
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

	void ScanForLiveHealthCandidates(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}

		// Throttled to ~5Hz per actor - frequent enough to catch a
		// decrease shortly after it happens (this only needs to notice
		// SOME frame where old>new, not every one), cheap enough that a
		// ~440-float SafeRead-guarded scan every 200ms is a non-issue -
		// same order of magnitude as the per-frame probes already running
		// (worldBound/bone), just gated slower since this one holds a
		// snapshot per actor rather than being stateless.
		static std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> s_lastScan;
		const std::uint32_t                                                             formID = a_actor->GetFormID();
		const auto                                                                      now = std::chrono::steady_clock::now();
		if (const auto it = s_lastScan.find(formID); it != s_lastScan.end() && now - it->second < std::chrono::milliseconds(200)) {
			return;
		}
		s_lastScan[formID] = now;

		// Window chosen to comfortably cover avStorage (confirmed at
		// 0x260, Actor.h) and extend well past it into whatever follows -
		// deliberately blind/broad rather than a targeted guess, since
		// the whole point is not knowing where else health could be
		// cached. Widened 2026-08-25 (0x1000 -> 0x2000) after the first
		// pass found nothing convincing in the smaller window. 0x2000
		// bytes = 2048 candidate float slots, one SafeRead-guarded 4-byte
		// read each - still cheap at the 5Hz-per-actor throttle below.
		constexpr std::size_t kScanBytes = 0x2000;

		// Plausibility range narrowed 2026-08-25: the flat 1-5000 window
		// from the first pass let through several candidates (a smoothly
		// decaying ~149 value that plateaued - looks like a fading timer/
		// blend weight, not discrete hit damage; a couple of near-
		// unchanged small values) that don't obviously look like health
		// at all. This actor's own known MAX health (already reliably
		// read from avStorage.baseValues - that value itself isn't in
		// question, only whether it's current or max) is a much tighter,
		// per-actor ground truth: a real current-health field should
		// start at-or-below max and only matters within roughly that
		// range, not an arbitrary global window. Falls back to the old
		// flat range if maxHealth isn't available yet (e.g. baseValues
		// lookup hasn't resolved for this actor).
		HealthReading baseValuesReading{};
		const bool     haveMax = GetActorHealth(a_actor, baseValuesReading) && baseValuesReading.max > 0.0f;
		const float    kMinPlausibleHP = haveMax ? baseValuesReading.max * 0.02f : 1.0f;
		const float    kMaxPlausibleHP = haveMax ? baseValuesReading.max * 1.05f : 5000.0f;

		static std::unordered_map<std::uint32_t, std::vector<float>> s_snapshots;
		std::vector<float>&                                          snapshot = s_snapshots[formID];
		const bool                                                   firstScan = snapshot.empty();
		if (firstScan) {
			snapshot.assign(kScanBytes / sizeof(float), 0.0f);
		}

		const auto* base = reinterpret_cast<const std::byte*>(a_actor);
		for (std::size_t off = 0; off < kScanBytes; off += sizeof(float)) {
			float current = 0.0f;
			if (!SafeRead(base + off, &current, sizeof(current))) {
				continue;
			}
			float& prev = snapshot[off / sizeof(float)];
			if (!firstScan && current < prev && current >= kMinPlausibleHP && current <= kMaxPlausibleHP &&
				prev >= kMinPlausibleHP && prev <= kMaxPlausibleHP) {
				REX::INFO("[VATS] health scan: formID=0x{:08X} offset=0x{:03X} DECREASED {:.1f} -> {:.1f} (knownMax={:.1f}, range=[{:.1f},{:.1f}])",
					formID, off, prev, current, haveMax ? baseValuesReading.max : -1.0f, kMinPlausibleHP, kMaxPlausibleHP);
			}
			prev = current;
		}
	}
}
