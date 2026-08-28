#pragma once

#include <cstdint>

namespace VATS
{
	// The base-game aid items this mod knows how to spend, with what each
	// one is worth when handed to a companion.
	//
	// A fixed table rather than reading the values out of each ALCH record,
	// which was Alexander's call and the right one twice over. The set is
	// tiny and these are base-game form IDs, so they never change - and
	// walking an item's effect list for its magnitude and duration is by
	// some distance the riskiest thing this feature could have done. The
	// table costs nothing and removes that entirely.
	//
	// The form IDs come from Alexander cross-checking two sources, and two
	// of them needed it. Boudicca was 0x0029959F in a Google-AI-assisted
	// table and 0x0029A849 in xEdit reading the real game data; the second
	// won and the better source later agreed. Giant Heart moved from
	// 0x0029959A to 0x00122EA3 the same way. Both would have failed
	// SILENTLY - a wrong form ID means the item is simply never found,
	// which reads as a broken feature rather than a wrong number.
	//
	// The original seven are additionally confirmed present in a real
	// inventory by the probe on 2026-08-28, all form type 54. See
	// docs/FINDINGS.md.
	//
	// Red Amp is the one entry whose STATS are unverified - it was in the
	// unreliable source and absent from the better one, though the item
	// itself is confirmed to exist. Blend is deliberately absent: it is a
	// drink, not an aid item.
	//
	// The numbers are deliberately NOT the game's own:
	//
	//   Healing - the game heals a percentage over five seconds (4/7/10%).
	//   Over-time is not available to us (it would need a real magic
	//   effect) and RestoreActorValue is instant anyway, so these heal at
	//   once. The amounts are raised well above the game's, because 4% of
	//   max health per button press is not worth a button press; the
	//   ordering between the three items is preserved.
	//
	//   Percentages, not flat numbers, so a heal stays meaningful as
	//   companions level - a fixed amount is generous early and worthless
	//   later.
	//
	//   Shield - every item grants the SAME damage resistance and differs
	//   only in duration. Per-item DR would need a rule for what happens
	//   when you top up with a different item (higher wins? average?
	//   replace?) and the bar could no longer show one number. Duration is
	//   the one axis that stacks cleanly, so it carries the item worth.
	//
	//   The conversion is DR x seconds, expressed at our fixed 500: an
	//   item that natively gives 250 DR for 300s is 75,000 DR-seconds,
	//   which at 500 DR is 150 seconds. One rule, no per-item judgement,
	//   and it preserves each item character - a burst item stays a burst.
	//
	//   That puts Alien Genetic Material LAST at 30s despite it having the
	//   second-highest DR in the game, which is deliberate and was argued
	//   over. Our shield fixes DR at 500, so an item native DR is
	//   mechanically irrelevant - only total protection can be expressed.
	//   AGM is 15,000 DR-seconds; Giant Heart is 105,000. Ranking AGM
	//   above it would let an item worth a seventh of the protection win
	//   on a snapshot. Its low in-game price agrees, and no double-count
	//   is involved since price is not used at all.
	struct AidItem
	{
		std::uint32_t formID;
		const char*   name;
		float         healPercent;     // of the target's MAX health, 0 = not a heal item
		float         shieldSeconds;   // 0 = grants no shield time
	};

	inline constexpr AidItem kAidItems[]{
		{ 0x0000ABF9, "Med Pack",               25.0f,   0.0f },
		{ 0x0029A847, "Trauma Pack",            40.0f,   0.0f },
		{ 0x002A9DE8, "Emergency Kit",          60.0f,   0.0f },
		{ 0x00122E9C, "Hypergiant Heart",        0.0f, 300.0f },  // 500 DR x 300s
		{ 0x00122EA3, "Giant Heart",             0.0f, 210.0f },  // 350 DR x 300s
		{ 0x002A5024, "Battlestim",              0.0f, 150.0f },  // 250 DR x 300s
		{ 0x0029A849, "Boudicca",                0.0f, 110.0f },  // 300 DR x 180s
		{ 0x002C587F, "Red Trench",              0.0f, 110.0f },  // 300 DR x 180s
		{ 0x001F3E86, "Red Amp",                 0.0f,  95.0f },  // 400 DR x 120s - STATS UNVERIFIED
		{ 0x0003D3AB, "Heal Gel",                0.0f,  90.0f },  // 150 DR x 300s
		{ 0x0029CAD9, "Heart+",                  0.0f,  50.0f },  // 200 DR x 120s
		{ 0x000C1F57, "Alien Genetic Material",  0.0f,  30.0f },  // 500 DR x  30s
	};

	[[nodiscard]] inline const AidItem* FindAidItem(std::uint32_t a_formID)
	{
		for (const auto& item : kAidItems) {
			if (item.formID == a_formID) {
				return &item;
			}
		}
		return nullptr;
	}
}
