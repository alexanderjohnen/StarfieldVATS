#pragma once

namespace VATS
{
	// Hides Starfield's native floating damage numbers while VATS is
	// Locked, restoring the player's own preference on unlock - not
	// because they're wrong, but because per-hit damage numbers spawn as
	// anonymously-named MovieClips with no stable path a mod could ever
	// address individually (confirmed 2026-08-24 via a real JPEXS decompile
	// of hudmenu.gfx, see HANDOFF.md/CombatHudVisibility.h - AS3's
	// addChild() with no explicit name gives every popup a different
	// internal name each time). Rather than fight that, this flips
	// Starfield's own Interface > "Show Damage Numbers" menu toggle off for
	// the duration of the lock and back on afterward - Alexander found the
	// real ini key (`bDamageNumbersEnabled` under `[Interface]`, confirmed
	// via a real StarfieldPrefs.ini sample, same naming convention as
	// `bCrosshairEnabled` under `[GamePlay]`).
	//
	// Same mechanism and same safety profile as CrosshairVisibility - a
	// plain GameSettingCollection/INIPrefSettingCollection::GetSetting name
	// lookup (no REL::ID function call of its own beyond the singleton
	// pointer) plus Setting::GetBool/SetBool, not a single Scaleform call
	// anywhere. A wrong guess degrades to "setting not found, hide/restore
	// is a no-op" rather than any crash risk - unlike everything else
	// touched today, see CombatHudVisibility.h (and git history for the
	// now-removed HealthWidgetReader.cpp) for why that distinction matters
	// this particular session.
	class DamageNumbersVisibility
	{
	public:
		// Hides damage numbers if a backing setting was found (searched
		// once, cached), remembering the player's prior value. No-op if
		// none of the candidate setting names resolved - check the log for
		// which names were tried.
		static void Hide();

		// Restores whatever value Hide() found before changing it. No-op
		// if no backing setting was ever found.
		static void Restore();
	};
}
