#pragma once

namespace VATS
{
	struct Settings
	{
		[[nodiscard]] static Settings& Get();

		void Load();

		bool          enabled{ true };
		std::uint32_t activationKeyVK{ 0x51 };  // virtual-key code, default 'Q'

		// The real in-game hand-scanner keybind (independent of
		// activationKeyVK above). Used to synthesize a key press when we
		// want the scanner to close itself the way it would from an actual
		// button press (animation + sound included) instead of a hard
		// UIMessageQueue kHide. Must be kept in sync with Alexander's actual
		// Starfield control binding by hand — there's no way to read it
		// from the game's own settings.
		std::uint32_t scannerToggleKeyVK{ 0x51 };  // virtual-key code, default 'Q'

		// How to close the hand scanner when a lock is acquired.
		//   0 = don't close it at all
		//   1 = simulated real press of scannerToggleKeyVK (animation+
		//       sound — the reliable, long-used default). Was briefly
		//       broken 2026-08-22 by EngineInputLayer::SetBlocked being
		//       applied too early (before this had a chance to run) -
		//       fixed properly by re-ordering (see CloseScannerIfOpen's
		//       comment in VATSController.cpp), not by switching away
		//       from this mode.
		//   2 = UIMessageQueue kHide (no animation/sound) — kept as a
		//       fallback only.
		std::uint32_t scannerCloseMode{ 1 };

		// The real in-game "back" key — same one that opens the Tab
		// character hub (DataMenu) when nothing else is open, but backs out
		// of whatever *is* open first (the scanner, a submenu, etc.) if
		// something is. Used by BackKeyInterceptor to end an active VATS
		// lock on that same first press instead of letting it also open
		// DataMenu — mirrors how the game already treats the scanner.
		// Independent of the other two keys above; must also be kept in
		// sync by hand.
		std::uint32_t backKeyVK{ 0x09 };  // virtual-key code, default Tab (VK_TAB)

		// Acquisition cone half-angle. 20 proved too strict in play — real
		// misses at 21-24 deg while visibly aiming at the target — so this
		// is both more generous by default and tunable without a rebuild.
		std::uint32_t targetConeDeg{ 35 };
		std::uint32_t maxTargetRange{ 5000 };  // world units

		// Horizontal FOV in degrees, matching Bethesda's own convention
		// (Starfield's own "fFPGeometryFOV:Camera" setting is horizontal,
		// default 90 — confirmed via Alexander's console output). Used to
		// self-compute the HUD projection since locating the engine's own
		// camera object in memory failed after three separate strategies.
		std::uint32_t cameraFovDegrees{ 90 };

		// --- Aim assist (2026-08-22) ---
		// Hit chance is a cone around the crosshair, Alexander's idea
		// 2026-08-22: 100% (well, centerHitChancePercent) dead center,
		// falling off linearly to 0% at assistRadius, same shape as an
		// NPC's own perception cone just pointed the other way. Replaces
		// the original flat placeholder. Two real benefits over flat: (1)
		// it's finally a *real* input to the formula (aim precision),
		// where flat was pure placeholder; (2) a well-aimed shot needs
		// almost no visible mouse-nudge at all, since high chance only
		// happens when already close to the target — directly addresses
		// Alexander's "feels like losing camera control" complaint against
		// the earlier flat-chance version, without abandoning "the number
		// decides, not aim precision alone" the way a hard radius cutoff
		// would have.
		std::uint32_t centerHitChancePercent{ 95 };

		// Distance-based chance falloff (2026-08-22 redesign) — replaces
		// the original screen-space "must aim precisely at the crosshair"
		// cone. Alexander's reference: FO4/FO76 VATS shows a high chance
		// number even while the reticle points nowhere near the target
		// (see starfield-vats-mod-design memory) — the whole point of
		// VATS is that it doesn't require aim precision, only a lock and
		// being in range/LOS. Doubly true here since the fired round gets
		// redirected in-flight regardless of exact aim (ProjectileTracker)
		// — tying the displayed/rolled chance to crosshair proximity would
		// have fought that mechanic, not complemented it. centerHitChancePercent
		// is now the chance at/under fullChanceRangeMeters, falling
		// linearly to 0% by maxEffectiveRangeMeters. Being on-screen at
		// all (roughly facing the target) and passing HasDetectionLOS are
		// still hard requirements — see ComputeChancePercent.
		float fullChanceRangeMeters{ 12.0f };
		float maxEffectiveRangeMeters{ 45.0f };

		// --- HUD extras (added alongside the hitscan-redirect work) ---

		// Blocks manual aim-down-sights while Locked (AdsBlocker.cpp) by
		// forcing the camera out of RE::CameraState::kIronSights the moment
		// it enters that state - button-agnostic (reacts to the resulting
		// camera state, not any specific keybind), so unlike the other keys
		// on this struct this one needs no VK to keep in sync by hand.
		bool blockAdsWhileLocked{ true };

		// Hides Starfield's native crosshair while Locked (our own HUD
		// target box replaces it) via CrosshairVisibility.cpp. Best-effort -
		// see that file for the unconfirmed setting-name search.
		bool hideCrosshairWhileLocked{ true };

		// Shows the Locked target's current/max health in the HUD box
		// (HealthReader.cpp).
		bool showTargetHealth{ true };
	};
}
