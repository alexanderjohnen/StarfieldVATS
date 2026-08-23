#pragma once

namespace VATS
{
	// Hides Starfield's native crosshair while VATS is Locked, restoring it
	// on unlock - our own HUD target box is the whole point once Locked, the
	// native reticle underneath is redundant clutter. Starfield's own
	// options menu has a real Interface > Crosshair on/off toggle
	// (confirmed via community reporting, not by any CommonLibSF header -
	// there's no dedicated HUD/crosshair class in it at all), which almost
	// certainly persists as an INIPref setting the same way every other
	// Settings-menu toggle does. The exact key name is unconfirmed - see the
	// candidate list in the .cpp - but INIPrefSettingCollection::GetSetting
	// is a plain linked-list name lookup with no REL::ID function call of
	// its own (only the singleton pointer), so a wrong guess degrades to
	// "setting not found, hide/restore is a no-op" rather than any crash
	// risk.
	class CrosshairVisibility
	{
	public:
		// Hides the crosshair if a backing setting was found (searched once,
		// cached), remembering its prior value. No-op if none of the
		// candidate setting names resolved - check the log for which names
		// were tried.
		static void Hide();

		// Restores whatever value Hide() found before changing it. No-op if
		// no backing setting was ever found.
		static void Restore();
	};
}
