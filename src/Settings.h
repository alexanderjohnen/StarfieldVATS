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
		//   1 = simulated real press of scannerToggleKeyVK (animation+sound
		//       — the proven-working default; confirmed across many test
		//       sessions, including twice in a row in the same session on
		//       2026-08-22 right before an unrelated crash)
		//   2 = UIMessageQueue kHide (no animation/sound — an older,
		//       never-reverted-to fallback, kept only in case a future
		//       session ever needs to isolate this specific mechanism)
		// A same-day crash was briefly misattributed to mode 1 from just
		// two data points that happened to coincide in timing with it —
		// wrong call, mode 1 has far more successful test sessions behind
		// it than that. Don't flip this default again without much
		// stronger evidence than "the log line right before the crash
		// happened to be about scanner-close" (see [[commonlibsf-unmapped-ids]]
		// lesson #3 — don't chase the last log line if it's just the last
		// thing that happened to log, not something shown to be the cause).
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

		// Normalized screen-space radius (0..1, screen-diagonal-ish units)
		// within which the chance cone applies at all; beyond it, chance
		// is 0 and aim-assist does nothing (same as being off-screen
		// entirely). Starting guess, unverified/untuned.
		float assistRadius{ 0.15f };

		// Converts the on-screen pixel distance from crosshair to target
		// into raw SendInput mouse-move units. Starfield's own
		// fMouseHeadingSensitivity (StarfieldPrefs.ini) affects how far a
		// given raw mouse delta actually turns the camera, but the exact
		// conversion formula isn't known — this is a plain empirical scale
		// factor to tune in-game by trial and error instead, same spirit
		// as targetConeDeg above. Starting guess, unverified.
		float mouseSensitivityScale{ 1.0f };
	};
}
