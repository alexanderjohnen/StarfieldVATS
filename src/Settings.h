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

		// FOV in degrees, used to self-compute the HUD projection since
		// locating the engine's own camera object in memory failed after
		// three separate strategies.
		//
		// Corrected 2026-08-26: this used to name "fFPGeometryFOV" as the
		// setting to match. That one governs the first-person *viewmodel*
		// (hands and weapon), not the world projection. The world FOV is
		// "fFPWorldFOV" in StarfieldPrefs.ini, and on Alexander's machine
		// the two do not agree - 90 vs 89.6. Small on its own, but it
		// means every value ever set here was being matched against the
		// wrong game setting.
		//
		// Float rather than int for exactly that reason: the game writes
		// the world FOV with decimals, and 89.6 cannot be spelled as an
		// int.
		float cameraFovDegrees{ 90.0f };

		// Whether the value above is the HORIZONTAL field of view (with
		// the vertical one derived from the aspect ratio) or the vertical
		// one. The projection has assumed horizontal since it was written
		// and that assumption has never been tested.
		//
		// It matters, and it matters in a specific shape: get the axis
		// wrong and the resulting error is ZERO at the screen centre and
		// grows the further the target sits from it. That is exactly the
		// "off on some characters, fine on others" pattern Alexander keeps
		// reporting - a target he is looking straight at versus one off to
		// the side. Hence an INI toggle: flipping it settles the question
		// in one look, with no rebuild and no fourth guess at
		// fAimPointRadiusFactor.
		bool cameraFovIsHorizontal{ true };

		// --- Aim assist (2026-08-22) ---
		// Hit chance is a cone around the crosshair, Alexander's idea
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

		// PARKED 2026-08-25, Alexander's call, and the right one.
		//
		// When a locked target dies with budget left, hop to the next enemy
		// instead of dropping out of VATS. The feature works; what does not
		// work is choosing a sensible target, because this project has no
		// line-of-sight test. Three successive filters were tried and none
		// of them is one: the crosshair activation target is
		// occlusion-correct but only reaches interaction range, so it
		// essentially never fired; restricting to actors engaged with the
		// player still picks them through walls, since an enemy shooting at
		// you from another room is still shooting at you; and the on-screen
		// projection check catches targets outside the view but not ones
		// plainly visible on the far side of a wall.
		//
		// Alexander's testing: enemies behind walls, and on other floors.
		// Rather than keep stacking approximations, this stays off until
		// real occlusion exists - see the depth-buffer proposal in
		// HANDOFF.md. All the surrounding work (resource cost, liveness,
		// friendly filtering, wider cone, on-screen check) stays in place
		// and correct, so re-enabling is a one-line change once occlusion
		// lands.
		bool autoAdvanceOnKill{ false };

		// How far the auto-advance will look, in metres. Much shorter than
		// the ordinary acquisition range: hopping to something across the
		// map would be a surprise rather than a convenience, and the player
		// still has to be roughly facing the next target since the normal
		// view cone (iConeDegrees) applies unchanged.
		float autoAdvanceRangeMeters{ 30.0f };

		// Only advance to what the game's own crosshair activation target
		// reports (the value behind the vanilla "hold E" prompt). That is
		// occlusion-correct by construction - the game will not offer an
		// interaction through a wall - which is what the cone scan cannot
		// do: it has no line-of-sight test at all and happily hopped to an
		// enemy in the next room (Alexander, 2026-08-25). The cost is
		// range, since it only reaches normal interaction distance, so a
		// distant next enemy simply does not trigger an advance. That is
		// the safer failure: no hop beats a hop through a wall.
		//
		// Turning this off falls back to the cone scan within
		// fAutoAdvanceRangeMeters, walls included.
		// Default flipped to false on 2026-08-25: requiring the crosshair
		// target made auto-advance essentially never fire, since it only
		// reaches interaction distance and the next enemy in a firefight
		// rarely is that close. The cone scan is used instead, with
		// bAutoAdvanceRequireEngaged below standing in for the missing
		// line-of-sight test.
		bool autoAdvanceRequireCrosshair{ false };

		// Only advance to actors whose own combat target is the player.
		// Applies to the advance ONLY, never to ordinary acquisition -
		// there it would make it impossible to open a fight from stealth.
		bool autoAdvanceRequireEngaged{ true };

		// The advance gets its own, wider cone than ordinary acquisition
		// (Alexander's question, 2026-08-25 - and a good one). Acquisition
		// happens while the player is deliberately aiming at someone, so 35
		// degrees is generous there. An advance happens the moment a target
		// drops, when they are still pointed at the body that fell and the
		// next enemy can be well off to one side. Reusing the narrow cone
		// would reject candidates for a reason that does not apply.
		//
		// The scan's telemetry already logs outsideCone and
		// closestMissAngle, so if this still turns out too narrow the log
		// says by how much rather than needing another guess.
		std::uint32_t autoAdvanceConeDeg{ 60 };

		// How long the lock stays open after a kill, waiting for the player
		// to put their crosshair on the next enemy. Without this the
		// crosshair-based advance could essentially never fire: at the
		// instant of death the player is by definition still aiming at the
		// body that just dropped, so the one attempt made at that moment
		// always failed. Nothing is drawn while waiting.
		std::uint32_t autoAdvanceGraceMs{ 1200 };

		// Excludes the player's own teammates (companions) from targeting.
		// Measured rather than assumed - see IsTargetable in Targeting.cpp
		// for the companion/enemy probe this is built on.
		//
		// Note this is NOT a full faction/relationship check, which is how
		// Bethesda actually models friend/neutral/enemy. The engine's own
		// combined answer, Actor::IsHostileToActor, is unreachable here (an
		// Address Library ID of 0), and reconstructing it would mean
		// walking the actor's faction list, the player's, and the
		// faction-reaction records - several unverified structures deep. So
		// this covers the case Alexander actually hit, companions, and
		// leaves neutral civilians targetable.
		bool ignoreFriendlyActors{ true };

		// Stricter: only actors currently fighting the player at all. Off
		// by default because it would also exclude an enemy who has not
		// noticed you yet, which breaks opening a fight from stealth.
		bool requireHostileTarget{ false };

		// Where on a target the box sits and rounds are steered, as a
		// multiple of how high the target's bounding-sphere centre already
		// is above its own feet. 1.0 aims at that raw centre - which is the
		// geometric middle of the whole body, i.e. hip height on a standing
		// humanoid, and visibly too low. 1.5 lands around chest height.
		//
		// Deliberately proportional rather than a fixed distance, because
		// Where to aim on the target, as a multiple of the height its
		// bounding sphere's CENTRE already sits above its own feet. 1.0 is
		// that centre itself - the geometric middle of the whole body,
		// hip height on a standing human. 1.5 lands on the chest.
		//
		// Chosen 2026-08-26 after the first non-humanoid tests, which
		// killed the radius-based model outright: the radius measures a
		// creature's longest extent, not its height, so a sprawling
		// ground-hugger (radius 2.16, body 30cm up) wanted an aim point
		// 2.22m in the air. Centre height is a height, so it degrades
		// gracefully across body plans - and it tracks pose for free.
		// See WorldBoundProbe::GetAimPoint for the measured table.
		float aimPointCentreFactor{ 1.5f };
		// Time constant of the low-pass on the aim point, in seconds. 0
		// disables smoothing entirely.
		//
		// The bounding sphere moves with the animation on every creature,
		// and on a flying one it is violent: a measured 2.04m swing in the
		// centre's height in time with the wingbeat, which the box
		// followed. Only the offset from the actor's root is filtered, so
		// a moving target still tracks with zero lag.
		//
		// 0.35s is chosen against what has to survive it: a wingbeat is a
		// few hertz and disappears, while a crouch or a collapse takes
		// several tenths of a second and comes through nearly intact.
		float aimPointSmoothingSeconds{ 0.35f };

		// Starfield's hit and kill markers are hidden while Locked. That
		// hide holds for ordinary hits but not for critical ones, which
		// briefly flash the marker back up - so this additionally offsets
		// it, putting that unavoidable crit flash somewhere chosen rather
		// than over the middle of the VATS overlay. See
		// CombatHudVisibility.h.
		//
		// Off until the coordinate space is known: with it off, the object's
		// current position is logged instead (once per session), which is
		// what the offsets below should be derived from. Picking one blind
		// risks throwing the banner off-screen.
		// How long the vanilla hit/kill/crit indicators stay suppressed
		// after a lock ends. The restore happens as soon as the indicator
		// animation parks itself; this is the ceiling for when it does not.
		//
		// 2500ms is a compromise and cannot be anything else: the game
		// raises crit events several seconds after a kill, so suppressing
		// until they stop would mean suppressing indefinitely. The costs
		// are asymmetric - a stray crit flash after a fight is cosmetic,
		// while a hit marker that never returns is a functional regression
		// in the base game - which argues for cutting sooner.
		std::uint32_t hudRestoreDelayMs{ 2500 };

		// Radius of the target marker in pixels. FIXED - it does not
		// scale with the target's distance or size.
		//
		// Distance scaling was tried for a day (2026-08-25/26) and cost
		// three separate rounds of complaints, every one of them an
		// artefact of the scaling itself rather than a bad constant: too
		// big up close, growing before shrinking when walking backwards,
		// frozen at a cap while the target kept growing. Fallout 4 and 76
		// both use a fixed size and none of that is noticeable there.
		//
		// The reason it works there, and the condition for it working
		// here: a fixed size only looks wrong while the marker claims to
		// ENCLOSE the target, because only then is there something for its
		// size to disagree with. 22px is a marker ON the target rather
		// than a box AROUND it - roughly 5% of a human's on-screen height
		// at 3m, still inside the silhouette at 30m. Raise it and it
		// starts making that claim again.
		float targetMarkerRadius{ 22.0f };

		// Vertical position of the companion-shield readout, as a fraction
		// of screen height. 0.88 sits on the lower edge of the scanner's
		// own ring - Alexander's placement, so the readout belongs to the
		// scanner's furniture instead of floating over the middle of the
		// view, where it sat at 0.62 and read as clutter.
		//
		// A setting rather than a constant because the ring is the GAME's,
		// not ours: we never learn its radius, so this number was measured
		// off a screenshot and may want nudging at another resolution or UI
		// scale. If it ever drifts off the ring, that is the reason.
		float shieldReadoutY{ 0.88f };

		// Draws a labelled cross at each of the three candidate anchor
		// points - the actor's own origin (feet), the raw bounding-sphere
		// centre, and the lifted aim point the box actually uses - and
		// logs where each one lands on screen.
		//
		// This is the measurement the box-centring problem has been
		// missing. If the FEET cross sits on the target's feet, the
		// projection is correct and whatever error is left lives in the
		// vertical lift alone. If the feet cross drifts too - and drifts
		// further the further the target is from the screen centre - the
		// projection is the cause, and no amount of aim-point tuning will
		// ever fix it. Off by default; this is a diagnostic, not a
		// feature.
		bool debugAimMarkers{ false };

		bool  moveCritMarker{ false };
		float critMarkerOffsetX{ 0.0f };
		float critMarkerOffsetY{ -180.0f };

		// How much this mod writes to
		// Documents\My Games\Starfield\SFSE\Logs\StarfieldVATS.log.
		//   0 nothing at all, not even errors
		//   1 warnings and errors only
		//   2 + state transitions and one-time findings (default)
		//   3 + per-shot and per-frame diagnostics
		//
		// Applied at Load() to Log::g_level, which VATS_LOG/VATS_TRACE
		// check BEFORE formatting their arguments (see Log.h - REX formats
		// unconditionally, so a level check inside the logger would not
		// have saved anything). Level 3 additionally re-enables a probe that
		// is otherwise skipped outright rather than merely silenced:
		// WorldBoundProbe per frame in Overlay::Draw.
		int logLevel{ 2 };

		// Whether to install the system-wide WH_KEYBOARD_LL hook that makes
		// iBackKey end a VATS lock instead of opening the character menu.
		// Worth its own switch because the cost is not confined to this
		// game: a low-level keyboard hook sits in the input path of every
		// application on the machine. Turn it off if the back key is not
		// wanted for VATS - the mod is otherwise unaffected.
		bool interceptBackKey{ true };

		// How much of the per-frame overlay path runs. A bisect control
		// for the memory-growth hunt of 2026-08-27, not a feature.
		//   0 nothing - the Present hook just passes through, no HUD at all
		//   1 the ImGui half only (NewFrame, Draw, Render), no D3D work
		//   2 + the 11on12 wrap and Flush, but not the backend draw
		//   3 everything, the normal HUD (default)
		// Everything else about the mod - targeting, projectile redirect,
		// input hooks - keeps working at every stage.
		//
		// Measured so far: stage 0 holds flat within half a megabyte over
		// two minutes, stage 2 adds about 2GB a minute (~518MB per 15s,
		// roughly half a megabyte per presented frame) with handles and
		// threads unmoved. Stage 1 came out flat too (-9.7MB over 2.2
		// minutes), which clears ImGui itself and leaves only the five D3D
		// lines below it - stage 2 splits those. See D3DHook.cpp
		// InitializeOrRender.
		int overlayStage{ 3 };

		// Skip the whole per-frame draw when there is nothing to draw. On by
		// default and worth keeping on its own merits - that work could
		// never have produced a pixel. Turn it off only to measure the
		// renderer, since with it on an idle scene never reaches the draw
		// and any renderer bug would read as flat. See D3DHook.cpp.
		bool skipEmptyFrames{ true };

		// How long the VATS key must be held before it means cancel rather
		// than act, in milliseconds. Long enough that a normal tap can never
		// be mistaken for one, short enough not to feel like a wait.
		std::uint32_t holdToCancelMs{ 400 };

		// --- Support: the damage-resistance shield ---

		// How much resistance one shield grants, applied to all three of
		// Starfield own types at once. 500 is the top of the range the game
		// own aid items occupy (they run 60 to 500), so it cannot be absurd
		// by the game standards, and companions need it.
		float shieldDamageResist{ 500.0f };

		// Ceiling on stored shield time. Items add to it (see AidItems.h for
		// what each is worth) and stop at this. 300s is chosen so the rarest
		// item, Hypergiant Heart, fills the bar in one go while everything
		// else has to be topped up - which is where the actual decision
		// lives: spend now, or save it.
		float shieldMaxSeconds{ 300.0f };

		// --- Support (Aspekt 1: Begleiter heilen) ---


	};
}
