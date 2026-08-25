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

		// Ends an active VATS lock the instant the player manually presses
		// the ADS button (AdsBlocker.cpp) - replaces an earlier attempt to
		// block ADS outright (four approaches tried, all failed or were
		// too broad, see AdsBlocker.h for the full trail). Alexander's own
		// suggestion 2026-08-24.
		bool endLockOnAds{ true };

		// Which physical button counts as "ADS" for the setting above
		// (AdsBlocker.cpp) - must match Alexander's actual ADS keybind by
		// hand, same caveat as scannerToggleKeyVK above. Only
		// VK_RBUTTON/VK_MBUTTON/VK_XBUTTON1/VK_XBUTTON2 are meaningful
		// (mouse buttons only).
		std::uint32_t adsButtonVK{ 0x02 };  // VK_RBUTTON

		// Hides Starfield's native crosshair while Locked (our own HUD
		// target box replaces it) via CrosshairVisibility.cpp. Best-effort -
		// see that file for the unconfirmed setting-name search.
		bool hideCrosshairWhileLocked{ true };

		// Shows the Locked target's current/max health in the HUD box
		// (HealthReader.cpp). Restored 2026-08-25, see HealthReader.h.
		bool showTargetHealth{ true };

		// Speed (m/s) forced onto the equipped weapon's projectile while
		// Locked, alongside the hitscan->real-projectile type flip
		// (ProjectileTypeOverride.cpp). A vanilla ballistic round travels
		// at 500 m/s, which at a measured typical engagement distance of
		// ~6m means the round reaches its target inside a SINGLE game
		// frame - there is physically no in-flight window to steer it,
		// which is why redirect never visibly worked for guns while it
		// always worked for (slow) rockets. Lowering the speed gives the
		// round real flight time to be homed. 0 disables the override.
		float lockedProjectileSpeed{ 80.0f };

		// --- VATS resource bar (2026-08-25, Alexander's design) ---
		// See VatsResource.h for the full reasoning. Capacity comes from the
		// player's FULL health, refill rate from their FULL oxygen, and the
		// budget is spent per point of damage dealt rather than per shot.
		bool vatsResourceEnabled{ true };

		// Capacity = full health x this. At 1.0 a player with 1820 max
		// health can deal 1820 damage across a lock - roughly four
		// 480-health enemies on one full bar. Lower it to make VATS scarcer.
		float vatsCapacityPerHealth{ 1.0f };

		// Refill per second = full oxygen x this. At 0.5 a player with 270
		// max oxygen regains 135 points/second, refilling that same 1820
		// bar in about 13 seconds out of VATS.
		float vatsRefillPerOxygen{ 0.5f };

		// Multiplier on what a point of damage costs. Raised to 2.0 on
		// 2026-08-25 after Alexander's first real test: the bar was
		// draining too slowly relative to how much of it there is. Kept as
		// its own knob rather than folded into capacity, so "how big is the
		// bar" and "how fast does using it cost" stay independently
		// tunable - halving capacity would also have halved what a fresh
		// lock starts with, which is a different change.
		float vatsCostPerDamage{ 2.0f };

		// When a locked target dies with budget left, hop straight to the
		// next living enemy instead of dropping out of VATS. Deliberately
		// gated behind the resource system existing (Alexander deferred it
		// until then, 2026-08-25) - without a cost, chaining targets would
		// be free and VATS would never end on its own.
		bool autoAdvanceOnKill{ true };

		// How far the auto-advance will look, in metres. Much shorter than
		// the ordinary acquisition range: hopping to something across the
		// map would be a surprise rather than a convenience, and the player
		// still has to be roughly facing the next target since the normal
		// view cone (iConeDegrees) applies unchanged.
		float autoAdvanceRangeMeters{ 30.0f };

		// Where on a target the box sits and rounds are steered, as a
		// multiple of how high the target's bounding-sphere centre already
		// is above its own feet. 1.0 aims at that raw centre - which is the
		// geometric middle of the whole body, i.e. hip height on a standing
		// humanoid, and visibly too low. 1.5 lands around chest height.
		//
		// Deliberately proportional rather than a fixed distance, because
		// Starfield's non-humanoid creatures come in wildly different
		// shapes: a factor scales with whatever body it is applied to,
		// while a fixed "chest offset" assumes human proportions and would
		// aim over a low, sprawling creature entirely. Also clamped to the
		// top of the target's own bounding sphere. UNTESTED against
		// non-humanoids as of 2026-08-25 - set this back to 1.0 in the INI
		// to restore the exact previous behaviour without a rebuild if some
		// creature reacts badly.
		float aimPointHeightFactor{ 1.5f };

		// Starfield's hit and kill markers are always hidden while Locked.
		// The crit marker is separate: Alexander wants to keep crit
		// feedback, just out of the way of the VATS overlay, so it can be
		// moved instead of hidden. Offsets are in the parent clip's own
		// coordinate space, whose scale is not known yet - the original
		// coordinates are logged on the first move so these can be
		// calibrated from a real session (see CombatHudVisibility.h).
		bool  moveCritMarker{ false };
		float critMarkerOffsetX{ 0.0f };
		float critMarkerOffsetY{ -180.0f };
	};
}
