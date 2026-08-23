# StarfieldVATS — Handoff / Project Snapshot (2026-08-23, late evening)

Read this first in a new chat session to pick up where things left off. This is a
point-in-time snapshot; the code and Alexander's own testing may have moved past
some of this by the time you read it — verify against actual file contents and
the SFSE log before trusting anything below as still true.

**This supersedes the previous version of this file from earlier the same day.**
That version covered the hitscan/real-projectile breakthrough (still true, see
below, untouched this round) but predates an entire later session of HUD/QoL
work (crosshair, health bar, hit/miss feedback, click-detection fix, native
combat-HUD elements, four separate ADS-blocking attempts). Don't re-read the
"Other current status" section from the old version — it's replaced wholesale
below.

## What this is

An SFSE mod for Starfield recreating **Fallout 76-style real-time VATS**: a
hotkey locks a target while the game keeps running (no time-slow, no menu
pause), shows a hit-chance percentage, and shots land (or deliberately miss)
according to that percentage — all without the camera/weapon visibly
snapping onto the target. Project root: `C:\Dev\StarfieldVATS`, git repo with
real commit history since 2026-08-22. **Public and open-source** at
https://github.com/alexanderjohnen/StarfieldVATS (GPL-3.0, inherited from
CommonLibSF) — `docs/FINDINGS.md` there has the confirmed struct-offset
corrections in a cleaner, more citable form than this file; `docs/
CONTRIBUTIONS.md` documents who did what. Keep both in sync with major
changes going forward (CONTRIBUTIONS.md has NOT been updated with this
session's work yet — do that before it goes stale). Game: Steam, `G:\Program
Files (x86)\Steam\steamapps\common\Starfield`, v1.16.244.

Alexander tests in-game; Claude cannot. Iterative build → deploy → Alexander
tests → reports back → repeat. **Always read the SFSE log yourself**
(`Documents\My Games\Starfield\SFSE\Logs\StarfieldVATS.log`, readable directly
via the Read tool) rather than asking Alexander to paste it. **Commit after
every deployed build**, win or lose.

## Build & deploy

```
cd C:\Dev\StarfieldVATS
powershell -ExecutionPolicy Bypass -File deploy.ps1
```

Fails with a file-lock error whenever Starfield is running — ask Alexander to
close it first. `deploy.ps1` never overwrites the live `StarfieldVATS.ini`
once it exists, only seeds it on first deploy — new settings this session
default correctly even though Alexander's live ini doesn't list them.

## THE HITSCAN PROBLEM — SOLVED, unchanged this session

Weapons' ammo is flipped to behave like a real, simulated projectile at
runtime while Locked (`ProjectileTypeOverride.cpp`), so the already-working
projectile redirect (`ProjectileTracker.cpp`) works on regular guns too, not
just rockets/grenades. Confirmed working in-game across many test sessions.
Full detail in the previous handoff version (git history) and `docs/
FINDINGS.md`. **Still no ground-truth hit-damage confirmation** —
`RE::TESHitEvent::GetEventSource()`/`BSTEventSource<T>::RegisterSink` crashes
on an unmapped Address Library ID (`src/HitEventLogger.cpp/h`, disabled in
`main.cpp`) — unchanged, not re-attempted this session.

## This session's work: HUD/QoL polish + four ADS-block attempts

Started from three feature requests (hide crosshair while Locked, block ADS
while Locked, show target health) and grew considerably once testing
surfaced more native HUD elements and a genuine click-detection bug.

### Confirmed working
- **Crosshair hiding** (`CrosshairVisibility.cpp`) — toggles the real
  `bCrosshairEnabled:GamePlay` INIPref setting (found via a community dump of
  Starfield's known ini settings, stepmodifications.org topic 19019 — NOT
  guessed). Confirmed via log: `crosshair: found bool setting
  'bCrosshairEnabled:GamePlay' via INIPrefSettingCollection`.
- **Click detection fix** (`AimAssist.cpp`) — removed the global
  `s_steering` single-thread gate that could silently drop an entire fast
  follow-up click if it arrived before the previous `SteeringLoop`'s
  ~2-20ms poll tick noticed the button release (zero roll, zero HUD
  feedback, zero redirect attempt for that click — this is what Alexander
  was seeing as "misses that aren't tracked"). Every press now spawns its
  own `SteeringLoop`, tagged with a per-press generation counter
  (`IsMyPressStillHeld`) so an older hold's loop can't be fooled by a newer
  press starting during its post-release grace window.
  `ProjectileTypeOverride` was already reference-counted specifically to
  support overlapping holds, so removing the gate didn't need anything else
  to change. Also records a guaranteed-miss `RecordShotResult(false)` in the
  two early-return branches (target off-screen at hold start, zero chance)
  instead of returning silently — a real trigger pull now always gives HUD
  feedback.
- **Hit/miss HUD flash** (`Controller::RecordShotResult`/`GetLastShotResult`
  in `VATSController.h/cpp`, drawn in `Overlay.cpp`) — target box flashes red
  for 900ms on a hit, shows "MISS" above the box on a miss. Confirmed
  working per Alexander ("Hit und Miss funktioniert").
- **Target health bar renders** — a red bar + white legendary-segment pips
  draws under the target box (`Overlay.cpp`'s `DrawHealthBar`). Visually
  confirmed via screenshot. **Does NOT yet track real damage — see below,
  this is the most valuable open item.**

### Known-broken / open

**Health bar shows a static number, never updates (HIGH PRIORITY, 3
hypotheses tried and disproven this session — don't re-try any of these):**
1. ~~`avStorage.baseValues`/`modifiers` keyed by a 4-byte AV index
   (CommonLibSF's declared `BSTTuple<uint32_t, T>`)~~ — disproven: reading
   with that stride produces corrupted-looking entries (keys that are
   really the low 32 bits of 8-byte pointers, values that are really their
   high 32 bits misread as floats).
2. ~~Real key is `ActorValueInfo*` (8-byte pointer), health's *current*
   value lives in `avStorage.modifiers` as an `ACTOR_VALUE_MODIFIER::
   kDamage` entry~~ — disproven: `avStorage.modifiers` never has a health
   entry at all even after confirmed damage was dealt (`found=false`,
   `modifiers.size=20` — a real, populated array, just never touched for
   health specifically).
3. ~~Damage is written straight into `avStorage.baseValues` (no modifier
   layer), so reading that value live tracks current health~~ — disproven
   by a clean, deliberate test: Alexander did `getav health` (480.00), hit
   the target once, did `getav health` again (461.90) — the mod's own
   `current` reading (logged every change) stayed at 480.0 across that
   entire window despite confirmed hits landing (log has both the
   `aim-assist: hold started -> HIT` and `projectile redirect: HIT` lines
   in between the two `getav` calls). Whatever `avStorage.baseValues` holds
   for health, it is NOT live current health — probably a static/design-time
   max, coincidentally equal to `getav health` only because the very first
   test target happened to be undamaged.

   **Next lead, untested**: HONKCORE's `hudmenu.gfx` (a pure Scaleform HUD
   replacement with zero SFSE dependency — proof the data it uses is
   already pushed into the AS layer by the native engine) has a real
   `EnemyHealthMeter_mc`/`EnemyHealthHolder_mc` widget fed by an
   `UpdateEnemyHealthData` callback, with sibling fields `EMDamageBar_mc`
   (electromagnetic/shield damage), `currLegendaryRank`,
   `currLegendarySegment` (confirms the legendary-segment idea used for the
   HUD pips was on the right track), `bIsStunned`. **Reading this via
   `ScaleformGFxASMovieRootBase::GetVariable` (the same safe, zero-new-
   REL::ID mechanism `CrosshairVisibility`/`CombatHudVisibility` already
   use) instead of raw `avStorage` memory is the recommended next attempt**
   — sidesteps the whole "where does Starfield actually store live health"
   question by reading whatever value the engine already computed and
   pushed to the HUD. Exact AS variable path is unconfirmed; use
   `IMenu::GetRootPath()` on the live `HUDMenu` instance (see
   `CombatHudVisibility.cpp` for the pattern — real root path is `'root1'`,
   NOT `'_root'` as commonly assumed from Skyrim/FO4 conventions) rather
   than guessing blind.

   `src/HealthReader.cpp` currently reads `avStorage.baseValues` live +
   caches the highest-ever-seen value as "max" — functionally wrong (bar
   just won't move) but harmless/safe, left as the current (broken) default
   pending the `EnemyHealthMeter_mc` attempt.

**Native combat-HUD elements (hit marker, kill marker, damage numbers, crit
text/banner) still visible despite VATS's own overlay being the intended
replacement** (`CombatHudVisibility.cpp`) — none of the tried
`GetVariable`/`SetVariable` paths resolved, even using the correct HUDMenu
root path (`'root1'`, via `IMenu::GetRootPath()`). Strings dump of
`hudmenu.gfx` found the clip names (`DamageNumberText_mc`, `CritText_mc`,
`CritBanner_mc`, `HitIndicator_mc`, `KillIndicator_mc`) and the string
`SpawnNewClip` right next to `onEnterFrame#onHitDamageIndicatorAnimationFinish`
— **strong suspicion these clips are dynamically created per-hit and
removed after their animation finishes, not persistent objects with a
settable `_visible`**, which would explain why every path guess failed
regardless of correctness: `IsAvailable()` checked once at Lock time (before
any hit occurs) would correctly report "not found" for something that
doesn't exist yet. If true, hiding these needs either (a) continuous
per-frame polling to catch and hide them the instant they spawn (may still
show one brief flash), or (b) intercepting the native call that pushes
`HudHitKillIndicatorData`/invokes `onHitKillDataChange` before it reaches
Scaleform at all (real new reverse-engineering, no lead on the native
function yet). Not attempted — next thing to try if picked back up.

**ADS blocking — four approaches tried, all failed, no fifth attempted this
session (don't re-try any of these without new information):**
1. OS-level `WH_MOUSE_LL` swallow of the configured button (deleted code,
   see git history) — the hook correctly fired and swallowed the button
   (confirmed via log), but ADS still visibly engaged anyway. Same class of
   problem `EngineInputLayer.h` already documented for the Tab-key
   interceptor: Starfield reads some input through a path an OS-level
   message hook can't see (raw input, most likely).
2. `RE::PlayerCamera::QCameraEquals(CameraState::kIronSights)` polling
   (deleted code) — the engine never once entered `kIronSights` during a
   real, visually-confirmed ADS (screenshot showed a scoped view; log never
   showed the poller's "camera entered iron-sights" line even once).
   Starfield's ADS apparently isn't a `PlayerCamera` state transition at
   all — possibly a per-weapon FOV/animation blend instead (matches
   Alexander's own observation that the Cutter uses a completely different
   "focus mode" instead of real ADS when its aim button is held, and that
   weapon mods like Shingen replace the ADS *animation* rather than
   touching a shared camera state).
3. `RE::PlayerControls::PlayerIronSightsStartEvent`/`EndEvent` registration
   (`src/AdsBlocker.h/cpp`, currently disabled in `main.cpp`) — **crashed
   the game outright** on the main menu, before any save loaded: `REL/
   IDDB.cpp(417): Failed to find offset for Address Library ID! Invalid ID:
   0`, the same clean CommonLibSF abort `HitEventLogger` already hit. One of
   `PlayerIronSightsStartEvent::GetEventSource`,
   `PlayerIronSightsEndEvent::GetEventSource`, or
   `BSTEventSource<T>::RegisterSink` itself isn't mapped for 1.16.244.0.
   Confirmed via screenshot, fixed by commenting out the `Start()` call.
4. `USER_EVENT_FLAG::Fighting` via `EngineInputLayer::SetAdsBlocked`
   (implemented, currently unused/disabled in `VATSController.cpp`'s
   `Advance()`) — confirmed via screenshot to holster the weapon entirely
   on lock, not just block ADS. Same over-broad-flag pattern already known
   for `TabMenuMaybe`/`POVSwitch`. `EngineInputLayer::SetAdsBlocked` itself
   is kept implemented (harmless, unused) in case a narrower flag
   combination is found later.

   **Fifth idea, explicitly NOT attempted (Alexander's own suggestion,
   genuinely promising but blocked by tooling, not risk)**: unbind/rebind
   the ADS control at the `ControlMap` level instead of detecting/reacting
   to input or state. A real `ControlMap` singleton (`BSTSingletonSDM<
   ControlMap>`, same pattern `PlayerCamera` uses) and a
   `nsControlMappingData::RemapHandler` function both exist and are named
   in CommonLibSF's raw RTTI ID tables (`IDs_RTTI.h`), but **neither has a
   curated `ID::ControlMap::*` singleton-pointer entry in `IDs.h`** the way
   `PlayerCamera` does — meaning there is currently no known address to
   even start from, not just an unverified one. Implementing this needs
   either a real disassembler (IDA/Ghidra — not available in a chat
   session) to find the singleton address independently, or finding another
   published SFSE mod's source that's already solved runtime control
   remapping (searched, found none — `StarZoom`'s source was checked and
   uses `INIPrefSettingCollection`/FOV settings instead, not `ControlMap`
   at all). Alexander is asking around; revisit if a lead turns up.

- **Pose-unaware aim point, no per-shot LOS gating, Tab not ending lock,
  the `type=0x02→0x00` correlation caveat, `lib/commonlibsf` submodule
  drift** — all unchanged from the previous handoff version, not touched
  this session. See git history for detail if needed.

## Architecture (files touched or added this session)

- **`src/CrosshairVisibility.cpp/h`** — hides native crosshair via
  `bCrosshairEnabled:GamePlay` INIPref. Confirmed working.
- **`src/CombatHudVisibility.cpp/h`** — attempts to hide hit marker/kill
  marker/damage numbers/crit text via `ScaleformGFxASMovieRootBase::
  SetVariable`, using `IMenu::GetRootPath()` for the real HUDMenu root
  (`'root1'`). Not yet working — see "Known-broken" above (likely
  dynamically-spawned clips, wrong strategy).
- **`src/HealthReader.cpp/h`** — reads target health from
  `Actor::avStorage`. Base-value lookup mechanically works (pointer-keyed,
  16-byte stride, empirically confirmed) but the value it finds isn't live
  current health — see "Known-broken" above for the `EnemyHealthMeter_mc`
  lead to try next. Also reads `legendaryRank` for the HUD's segment-pip
  count (same caveat, unconfirmed against a real legendary enemy).
- **`src/AdsBlocker.h/cpp`** — currently disabled (`main.cpp`'s
  `Start()` call commented out) after the `PlayerIronSightsStartEvent`
  crash. Kept in the tree with a full comment trail of what was tried and
  why it failed, per this project's own established convention (see
  `HitEventLogger.h`).
- **`src/EngineInputLayer.h/cpp`** — added `SetAdsBlocked` (toggles
  `USER_EVENT_FLAG::Fighting`), implemented but unused (too broad, see
  above).
- **`src/AimAssist.cpp`** — removed `s_steering` gate, added per-press
  generation tracking (`IsMyPressStillHeld`), records guaranteed-miss
  results in early-return branches. See "Confirmed working" above.
- **`src/VATSController.h/cpp`** — added `RecordShotResult`/
  `GetLastShotResult` (two atomics, cross-thread hit/miss state for the
  HUD flash); wires `CrosshairVisibility`/`CombatHudVisibility`/
  `EngineInputLayer::SetAdsBlocked` (currently a no-op call site, disabled)
  into the Lock/Unlock/`ForceOff` transitions.
- **`src/UI/Overlay.cpp`** — added `DrawHealthBar` (red bar + white
  legendary-segment pips) and the hit/miss red-flash/"MISS"-text logic in
  `DrawIfVisible`.
- **`src/Settings.h/cpp`, `res/StarfieldVATS.ini`** — added
  `bBlockAdsWhileLocked`, `iAdsReleaseKey`, `bHideCrosshairWhileLocked`,
  `bShowTargetHealth`.
- Deleted this session (superseded/dead ends, not worth resurrecting
  without new information): `src/AdsProbe.h/cpp` (raw-memory-dump
  diagnostic, superseded once the real signal turned out not to be on the
  Actor object at all), `src/HitMarkerVisibility.h/cpp` (superseded by the
  broader `CombatHudVisibility`).

## How the Scaleform/HUD investigation actually happened (useful technique,
reusable for the health/combat-HUD leads above)

Alexander pointed out that HONKCORE (`Data\interface\hudmenu.gfx`, a pure
Scaleform HUD replacement mod with zero SFSE dependency) must be reading
whatever the native engine already exposes, since it has no native code of
its own. Extracting readable ASCII strings from that compiled file (`grep
-a -o -E '[ -~]{5,}' hudmenu.gfx`) surfaced real AS variable/condition names
("isAiming", "bCrosshairEnabled"-adjacent settings, "EnemyHealthMeter_mc",
"DamageNumberText_mc", etc.) without needing a full Scaleform/SWF
decompiler. Combined with `RE::UI::GetMenuMovie("HUDMenu")` →
`asMovieRoot->GetVariable/SetVariable/IsAvailable` (real virtual calls, zero
new REL::ID risk beyond the menu lookup itself, which is a plain data
lookup) and `IMenu::GetRootPath()` (a real virtual call giving the actual
root path instead of guessing `_root`), this is a solid, low-risk technique
for reading/writing anything the native engine already pushes into the HUD
— **use this same technique for the `EnemyHealthMeter_mc` health lead
before falling back to more `avStorage` guessing.**

## Persistent memory (separate from this file)

- `starfield-vats-mod-design` — overall design, status, backlog.
- `starfield-vats-ui-hook` — render hook + UI overlay specifics.
- `commonlibsf-unmapped-ids` (file: `feedback-commonlibsf-unmapped-ids.md`)
  — the general "mapped ID ≠ safe" pattern; now three confirmed instances
  (TESHitEvent, PlayerIronSightsStartEvent/EndEvent).
- `feedback-commit-every-vats-build` — commit after every deployed build.
- `feedback-model-tier-recommendations` — Sonnet vs Fable 5 guidance.

This file exists as a single self-contained recap for a fresh chat. For the
hitscan/offset work specifically, `docs/FINDINGS.md` in the repo is the more
polished, public-facing version — keep them consistent if either changes.
`docs/CONTRIBUTIONS.md` has NOT been updated with this session's work yet.
