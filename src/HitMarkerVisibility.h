#pragma once

namespace VATS
{
	// Hides Starfield's native hit-marker/hit-indicator overlay
	// (HitIndicator_mc within HUDMenu's own Scaleform movie) while VATS is
	// Locked - a separate HUD element from the crosshair (see
	// CrosshairVisibility.h; confirmed distinct 2026-08-23 via HONKCORE's
	// own hudmenu.gfx, which lists "default.HitIndicator" and
	// "default.Crosshair" as separate default widgets, and via Alexander's
	// own observation that hiding the crosshair left the hit marker
	// showing).
	//
	// Uses RE::UI::GetMenuMovie("HUDMenu") (a plain menuMap lookup, no
	// REL::ID at all) to reach the same ScaleformGFxASMovieRootBase::
	// GetVariable/SetVariable mechanism found for the crosshair
	// investigation - real virtual calls on an already-live engine object,
	// no REL::ID beyond what's already proven safe elsewhere. The exact AS
	// variable path to HitIndicator_mc's visibility is unconfirmed - only
	// its bare symbol name was found (a strings dump of hudmenu.gfx, not
	// its actual placement in the display list) - see the candidate list
	// in the .cpp. A wrong guess is a safe no-op (IsAvailable/GetVariable
	// simply return false), same guarantee as every other setting/variable
	// lookup this project has relied on today.
	class HitMarkerVisibility
	{
	public:
		static void Hide();
		static void Restore();
	};
}
