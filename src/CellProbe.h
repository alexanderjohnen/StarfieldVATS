#pragma once

namespace VATS
{
	// Diagnostic scanner: locates the cell's reference array at runtime by
	// scanning the TESObjectCELL struct for a BSTArray<{size, cap, data}>
	// whose entries point back at the cell via TESObjectREFR::parentCell
	// (an offset proven correct in-game). Results go to the SFSE log.
	// Every game-memory access is SEH-guarded — this cannot crash on a
	// wrong offset, it just reports what it found.
	void RunCellProbe(const RE::TESObjectCELL* a_cell);
}
