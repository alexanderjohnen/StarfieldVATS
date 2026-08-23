#pragma once

namespace VATS
{
	// Wraps a single, permanently-allocated RE::BSInputEnableLayer to
	// disable specific vanilla control actions at the engine's own input-
	// mapping level while VATS is Locked — the same mechanism the game
	// itself uses whenever a menu disables player controls, rather than
	// our own OS-level SetWindowsHookEx attempts (BackKeyInterceptor,
	// AimAssist's wheel handling), which turned out not to reliably
	// suppress Starfield's own reaction even when they successfully
	// detected the input (found 2026-08-22: Tab kept opening DataMenu and
	// the mouse wheel kept changing POV despite both hooks firing and
	// returning 1). This is meant to run *alongside* those hooks, not
	// replace them - the hooks are still useful for reacting to input
	// (ForceOff on Tab, cycling on wheel), this is what should actually
	// stop the vanilla action from happening.
	//
	// USER_EVENT_FLAG::POVSwitch, ::TabMenuMaybe, and ::WheelZoom are all
	// marked "Unconfirmed" in CommonLibSF's own UserEvents.h - the
	// underlying BSInputEnableManager calls are well-mapped (non-zero
	// Address Library IDs, and this system is exercised constantly by the
	// game itself), but which flag actually corresponds to which vanilla
	// action needs Alexander's in-game confirmation.
	class EngineInputLayer
	{
	public:
		// Allocates the layer. Call once at startup.
		static void Init();

		// true while Locked: disables POVSwitch/TabMenuMaybe/WheelZoom.
		// false: re-enables them. No-op if Init() failed/hasn't run.
		static void SetBlocked(bool a_blocked);

		// Independent of SetBlocked above - only touches
		// USER_EVENT_FLAG::Fighting. Tried 2026-08-23 as a lower-risk
		// alternative for blocking ADS while Locked after three other
		// approaches failed (OS-level input hook: fired but had zero effect
		// on ADS engaging; RE::PlayerCamera camera-state polling: the
		// engine never actually enters kIronSights during a real ADS;
		// RE::PlayerControls::PlayerIronSightsStartEvent registration:
		// crashed outright on an unmapped Address Library ID, see
		// AdsBlocker.h/main.cpp).
		//
		// CONFIRMED TOO BROAD (2026-08-23, screenshot): disabling Fighting
		// holstered the weapon entirely on lock, not just ADS - same
		// "collateral damage" pattern as TabMenuMaybe/POVSwitch above.
		// Currently unused (see VATSController.cpp's Advance()) but kept
		// implemented in case a narrower flag combination is found later.
		static void SetAdsBlocked(bool a_blocked);
	};
}
