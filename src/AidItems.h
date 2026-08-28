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
	// The form IDs are MEASURED, not looked up: the inventory probe found
	// all seven in a real inventory on 2026-08-28, every one of them form
	// type 54. See docs/FINDINGS.md.
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
	//   the one axis that stacks cleanly, so it carries the item's worth:
	//   Alien Genetic Material has the game's strongest DR, so it gets the
	//   longest time rather than a bigger number.
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
		{ 0x000C1F57, "Alien Genetic Material",  0.0f, 300.0f },
		{ 0x00122E9C, "Hypergiant Heart",        0.0f, 300.0f },
		{ 0x0029CAD9, "Heart+",                  0.0f, 120.0f },
		{ 0x001F3E86, "Red Amp",                 0.0f, 120.0f },
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
