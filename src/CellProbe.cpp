#include "CellProbe.h"

#include "SafeMem.h"

namespace VATS
{
	namespace
	{
		// Offsets taken from the CommonLibSF headers at compile time, so the
		// probe always reports against whatever the library currently claims.
		constexpr auto kParentCellOff = offsetof(RE::TESObjectREFR, parentCell);      // proven correct in-game
		constexpr auto kFormIDOff = offsetof(RE::TESForm, formID);
		constexpr auto kFormTypeOff = offsetof(RE::TESForm, formType);
		constexpr auto kLocationOff = offsetof(RE::TESObjectREFR, data.location);     // proven by stage-1 position test
		constexpr auto kHeaderClaimedReferencesOff = offsetof(RE::TESObjectCELL, references);

		constexpr std::uint8_t kFormTypeACHR = 75;  // crash logger: "FormType: 75|ACHR"

		template <class T>
		[[nodiscard]] bool Read(const void* a_base, std::size_t a_off, T& a_out)
		{
			return SafeRead(static_cast<const std::byte*>(a_base) + a_off, &a_out, sizeof(T));
		}

		struct Candidate
		{
			std::size_t   offset{ 0 };
			std::uint32_t size{ 0 };
			std::uint32_t capacity{ 0 };
			std::uint32_t hits{ 0 };
			std::uint32_t checked{ 0 };
			std::uint64_t data{ 0 };
		};

		[[nodiscard]] std::optional<Candidate> ProbeArrayAt(const RE::TESObjectCELL* a_cell, std::size_t a_off)
		{
			std::uint32_t size = 0;
			std::uint32_t capacity = 0;
			std::uint64_t data = 0;
			if (!Read(a_cell, a_off, size) || !Read(a_cell, a_off + 4, capacity) || !Read(a_cell, a_off + 8, data)) {
				return std::nullopt;
			}
			if (size == 0 || size > 4096 || capacity < size || capacity > 8192) {
				return std::nullopt;
			}
			if (!data || (data & 7) != 0) {
				return std::nullopt;
			}

			Candidate cand{ a_off, size, capacity, 0, std::min<std::uint32_t>(size, 4), data };
			for (std::uint32_t i = 0; i < cand.checked; ++i) {
				std::uint64_t entry = 0;
				if (!Read(reinterpret_cast<const void*>(data), 8ull * i, entry) || !entry || (entry & 7) != 0) {
					continue;
				}
				std::uint64_t parent = 0;
				if (Read(reinterpret_cast<const void*>(entry), kParentCellOff, parent) &&
					parent == reinterpret_cast<std::uint64_t>(a_cell)) {
					++cand.hits;
				}
			}
			return cand.hits > 0 ? std::optional(cand) : std::nullopt;
		}

		void DumpEntries(const Candidate& a_best)
		{
			const auto count = std::min<std::uint32_t>(a_best.size, 10);
			for (std::uint32_t i = 0; i < count; ++i) {
				std::uint64_t entry = 0;
				if (!Read(reinterpret_cast<const void*>(a_best.data), 8ull * i, entry) || !entry) {
					VATS_LOG("    [{}] <unreadable or null>", i);
					continue;
				}

				std::uint32_t formID = 0;
				std::uint8_t  formType = 0;
				float         pos[3] = { 0, 0, 0 };
				const void*   e = reinterpret_cast<const void*>(entry);
				const bool    okID = Read(e, kFormIDOff, formID);
				const bool    okType = Read(e, kFormTypeOff, formType);
				const bool    okPos = SafeRead(static_cast<const std::byte*>(e) + kLocationOff, pos, sizeof(pos));

				VATS_LOG("    [{}] ptr=0x{:X} formID={} formType={}{} pos={}",
					i,
					entry,
					okID ? std::format("{:08X}", formID) : "?",
					okType ? std::format("{}", formType) : "?",
					(okType && formType == kFormTypeACHR) ? " (ACHR/actor)" : "",
					okPos ? std::format("({:.1f}, {:.1f}, {:.1f})", pos[0], pos[1], pos[2]) : "?");
			}
		}
	}

	void RunCellProbe(const RE::TESObjectCELL* a_cell)
	{
		VATS_LOG("=== CELL PROBE start: cell=0x{:X}, header claims references at 0x{:X} ===",
			reinterpret_cast<std::uint64_t>(a_cell), kHeaderClaimedReferencesOff);

		// What lives at the header-claimed offset right now, raw:
		{
			std::uint32_t size = 0;
			std::uint32_t capacity = 0;
			std::uint64_t data = 0;
			const bool    ok = Read(a_cell, kHeaderClaimedReferencesOff, size) &&
			                Read(a_cell, kHeaderClaimedReferencesOff + 4, capacity) &&
			                Read(a_cell, kHeaderClaimedReferencesOff + 8, data);
			VATS_LOG("  raw @0x{:X}: size={} cap={} data=0x{:X} (readable={})",
				kHeaderClaimedReferencesOff, size, capacity, data, ok);
		}

		std::vector<Candidate> found;
		for (std::size_t off = 0x30; off <= 0x1F8; off += 8) {
			if (auto cand = ProbeArrayAt(a_cell, off)) {
				found.push_back(*cand);
				VATS_LOG("  CANDIDATE off=0x{:X}: size={} cap={} backptr-hits={}/{}",
					cand->offset, cand->size, cand->capacity, cand->hits, cand->checked);
			}
		}

		if (found.empty()) {
			VATS_LOG("  no reference-array candidate found in 0x30..0x1F8");
		} else {
			const auto best = *std::max_element(found.begin(), found.end(),
				[](const Candidate& a_lhs, const Candidate& a_rhs) { return a_lhs.hits < a_rhs.hits; });
			VATS_LOG("  best candidate: off=0x{:X} — dumping first {} entries:",
				best.offset, std::min<std::uint32_t>(best.size, 10));
			DumpEntries(best);
		}

		VATS_LOG("=== CELL PROBE end ===");
	}
}
