#include "AdsProbe.h"

#include "SafeMem.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace VATS
{
	namespace
	{
		// Broad, uninformed guesses at the Havok Behavior graph's boolean
		// variable name for aim-down-sights state - no confirmed source,
		// same "try candidates, safe on a miss" approach as
		// CrosshairVisibility's setting-name search. Starfield's own patch
		// notes/UI use the term "ADS" explicitly (unlike Skyrim/FO4's
		// "aiming" framing), so both vocabularies are represented.
		constexpr const char* kBoolCandidates[] = {
			"IsAiming", "bIsAiming", "Aiming", "bAiming",
			"IsADS", "bADS", "ADS", "IsAimed",
			"IsZooming", "bIsZooming", "IsScoped", "bScoped",
			"bInIronSights", "IsInIronSights",
		};

		void LogSnapshotOnce(const char* a_tag)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}

			std::string hits;
			std::string misses;
			for (const char* name : kBoolCandidates) {
				bool value = false;
				if (player->GetGraphVariableImplBool(RE::BSFixedString(name), value)) {
					hits += name;
					hits += value ? "=true " : "=false ";
				} else {
					misses += name;
					misses += ' ';
				}
			}

			REX::INFO("[VATS] ads-probe [{}]: graph hits: {} | not found: {} | actorState1=0x{:08X} actorState2=0x{:08X}",
				a_tag, hits.empty() ? "(none)" : hits, misses.empty() ? "(none)" : misses,
				player->actorState1, player->actorState2);
		}
	}

	void LogAimStateSnapshot(const char* a_tag)
	{
		LogSnapshotOnce(a_tag);
		std::thread([tag = std::string(a_tag)]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			LogSnapshotOnce((tag + "+200ms").c_str());
		}).detach();
	}

	void DumpPlayerRawRange(const char* a_tag)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		const auto* base = reinterpret_cast<const std::byte*>(player);
		constexpr std::size_t kStart = 0x0;
		constexpr std::size_t kEnd = 0x400;

		std::string line;
		char        cell[24];
		int         perLine = 0;
		for (std::size_t off = kStart; off < kEnd; off += 4) {
			std::uint32_t raw = 0;
			if (!SafeRead(base + off, &raw, sizeof(raw))) {
				continue;
			}
			std::snprintf(cell, sizeof(cell), "%03zX:%08X ", off, raw);
			line += cell;
			if (++perLine >= 8) {
				REX::INFO("[VATS] ads raw dump [{}]: {}", a_tag, line);
				line.clear();
				perLine = 0;
			}
		}
		if (!line.empty()) {
			REX::INFO("[VATS] ads raw dump [{}]: {}", a_tag, line);
		}
	}
}
