# StarfieldVATS — Handoff / Project Snapshot (2026-08-24)

Read this first in a new chat session to pick up where things left off. This is a
point-in-time snapshot; the code and Alexander's own testing may have moved past
some of this by the time you read it — verify against actual file contents and
the SFSE log before trusting anything below as still true.

**Updated 2026-08-24 (two rounds today)**: ADS is no longer a stuck problem —
see "ADS handling" below. Combat-HUD hiding (hit marker/damage numbers) now
has a real fix, not just failed guesses — see "Combat-HUD hiding" below. The
health bar has a second, still-unconfirmed attempt — see "Health bar" below.
Everything else in this file is otherwise unchanged from 2026-08-23.

**This supersedes the previous version of this file from 2026-08-23 evening.**
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

**Health bar — second attempt made 2026-08-24, still UNCONFIRMED.** Three
avStorage-based hypotheses were tried and disproven in the prior session
(kept below for the record, don't re-try any of these):
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
   entire window despite confirmed hits landing. `src/HealthReader.cpp`
   still contains this known-broken read as the last-resort fallback (see
   below) — not the primary path anymore.

**2026-08-24 second attempt: `src/HealthWidgetReader.h/cpp`, wired into
`Overlay.cpp` ahead of the old `HealthReader::GetActorHealth` fallback.**
Reads HONKCORE's `EnemyHealthMeter_mc` widget (the lead from the previous
handoff — HONKCORE's `hudmenu.gfx` has zero SFSE dependency, so whatever it
displays must already be data the native engine pushes to the HUD) via
`ScaleformGFxASMovieRootBase::GetVariable` — same zero-new-REL::ID
mechanism `CrosshairVisibility`/`CombatHudVisibility` already use. The exact
AS variable path was still unknown, so `Probe()` tries a matrix of parent
paths (real `IMenu::GetRootPath()` plus static `_root...` fallbacks) x
property suffixes (`.percent`, `.value`, `._xscale`, `._width`, etc.), logs
every combination that resolves (path, type, value), and best-effort
auto-picks the first numeric hit as the live candidate. **This is
diagnostic-guess, not confirmed** — same status the three disproven
avStorage hypotheses each had before testing ruled them out. Two open
risks: (a) the auto-picked path may just be wrong (that's what the probe
log is for — read it after a real combat test), (b) even if the path is
right, it's unconfirmed whether this native widget tracks the specific
actor VATS has Locked, versus whatever the vanilla combat/AI system
considers the player's "current" engaged target — nothing in this project
ever sets that association. **Next session: read the SFSE log after
Alexander fights something with VATS active, check whether a
`health-widget: using '...' as best-effort candidate` line appears and
whether the in-game bar actually moved on a hit; if it didn't, the full
`health-widget: '...' = ...` log lines above it are the next lead.**

**Native combat-HUD elements (hit marker, kill marker, damage numbers, crit
text/banner) — real fix attempted 2026-08-24, mechanism now sound, still
UNCONFIRMED whether it fully works in practice.** Root cause found in the
prior session: a strings dump of `hudmenu.gfx` found `SpawnNewClip` right
next to `onHitDamageIndicatorAnimationFinish`, meaning these clips
(`DamageNumberText_mc`, `CritText_mc`, `CritBanner_mc`, `HitIndicator_mc`,
`KillIndicator_mc`) are almost certainly created fresh on each hit and
destroyed once their animation finishes — a one-shot `Hide()` call at Lock
time (the original design) ran before any hit had happened, so it always
found nothing, regardless of whether the guessed paths were even right.
Fixed by turning `CombatHudVisibility::Hide()` into `HideActive()`, called
every frame while Locked from `Overlay::Draw()` instead of once from
`VATSController.cpp`'s lock transition — each candidate `(prefix, clip)`
path is re-checked every frame, so a clip spawned mid-combat gets caught
and hidden within a frame or two rather than never being found. Still
open: whether the *paths themselves* are actually correct was never
confirmed (the original per-hit-existence problem may have been masking a
separate wrong-path problem) — same open question Alexander should look
for in the log (`combat-hud: hid newly-visible '...'` lines) after a real
combat test. If nothing logs at all despite a confirmed hit, the paths
themselves are still wrong and need fresh guessing/probing, same approach
as `HealthWidgetReader.cpp` above. A possible one-frame flash before the
hide catches up is expected and acceptable per Alexander's original ask
(hide is the primary goal, not zero-frame-perfect suppression); intercepting
the native call before it reaches Scaleform at all (real new reverse
engineering, no lead on the function) remains the fallback if per-frame
hiding turns out insufficient.

**ADS handling — SOLVED 2026-08-24, via a completely different strategy.**
Four approaches at *blocking* ADS while Locked were tried and failed (kept
below for the record, don't re-try any of these without new information):
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
   — **crashed the game outright** on the main menu, before any save
   loaded: `REL/IDDB.cpp(417): Failed to find offset for Address Library
   ID! Invalid ID: 0`, the same clean CommonLibSF abort `HitEventLogger`
   already hit. One of `PlayerIronSightsStartEvent::GetEventSource`,
   `PlayerIronSightsEndEvent::GetEventSource`, or
   `BSTEventSource<T>::RegisterSink` itself isn't mapped for 1.16.244.0.
4. `USER_EVENT_FLAG::Fighting` via `EngineInputLayer::SetAdsBlocked`
   (still implemented, unused/disabled in `VATSController.cpp`'s
   `Advance()`) — confirmed via screenshot to holster the weapon entirely
   on lock, not just block ADS. Same over-broad-flag pattern already known
   for `TabMenuMaybe`/`POVSwitch`. Kept implemented (harmless, unused) in
   case a narrower flag combination is found later.

   A fifth idea (unbind/rebind ADS at the `ControlMap` level) was
   investigated but never attempted — no curated singleton address exists
   for `ControlMap` in CommonLibSF's `IDs.h`, would need a real
   disassembler to find one. Moot now that a much simpler fix exists below.

**The actual fix (Alexander's own idea, 2026-08-24): stop trying to block
ADS at all — end the VATS lock the instant ADS starts instead.**
`src/AdsBlocker.h/cpp` (re-enabled in `main.cpp`) reuses the exact
`WH_MOUSE_LL` detection technique from failed attempt #1 above — which was
already proven to reliably *see* the button-down, the only thing that
failed there was trying to *swallow* it. On a real (non-injected) press of
`Settings::adsButtonVK` (renamed from `adsReleaseKeyVK`; INI `iAdsButton`,
mouse buttons only, default VK_RBUTTON) while `Controller::GetMode() ==
kLocked`, it calls `Controller::Get().ForceOff()` — the same thread-safe,
engine-call-light unlock path `BackKeyInterceptor` already uses from its
own background hook thread for the Tab key. The button itself is never
swallowed; Starfield sees the real ADS press exactly like it always did,
VATS just steps out of the way. New setting `Settings::endLockOnAds`
(renamed from `blockAdsWhileLocked`; INI `bEndLockOnAds`, default true)
gates it. Builds and deploys clean; **not yet confirmed in-game by
Alexander** — next session should verify a real ADS press ends the lock
and doesn't otherwise misfire (e.g. on other right-click activity, like
scanner interactions).

- **Pose-unaware aim point, no per-shot LOS gating, Tab not ending lock,
  the `type=0x02→0x00` correlation caveat, `lib/commonlibsf` submodule
  drift** — all unchanged from the previous handoff version, not touched
  this session. See git history for detail if needed.

## Architecture (files touched or added this session)

- **`src/CrosshairVisibility.cpp/h`** — hides native crosshair via
  `bCrosshairEnabled:GamePlay` INIPref. Confirmed working.
- **`src/CombatHudVisibility.cpp/h`** — reworked 2026-08-24: `Hide()` (a
  one-shot call at Lock time) is now `HideActive()`, called every frame
  while Locked from `Overlay::Draw()` instead — see "Known-broken" above
  for why the one-shot version could never have worked regardless of path
  correctness. Still hides hit marker/kill marker/damage numbers/crit text
  via `ScaleformGFxASMovieRootBase::GetVariable`/`SetVariable`, using
  `IMenu::GetRootPath()` for the real HUDMenu root. Whether the candidate
  paths themselves are correct is still unconfirmed.
- **`src/HealthReader.cpp/h`** — unchanged this session, now the last-
  resort fallback (see `HealthWidgetReader` below). Reads target health
  from `Actor::avStorage`; base-value lookup mechanically works but the
  value it finds isn't live current health (see "Known-broken" above).
  Still used for `legendaryRank` (HUD segment-pip count), untouched by this
  session's health-bar work.
- **`src/HealthWidgetReader.cpp/h`** (new, 2026-08-24) — second attempt at
  live target health, reading HONKCORE's `EnemyHealthMeter_mc` HUD widget
  instead of `Actor::avStorage`. Probes a matrix of candidate AS paths,
  logs every one that resolves, best-effort auto-picks the first numeric
  hit. Wired into `Overlay.cpp` ahead of `HealthReader::GetActorHealth`.
  **Unconfirmed** — see "Known-broken" above.
- **`src/AdsBlocker.h/cpp`** — rewritten 2026-08-24: no longer tries to
  block ADS (the `PlayerIronSightsStartEvent` version that crashed is
  gone). Now a `WH_MOUSE_LL` hook that calls `Controller::Get().ForceOff()`
  on a real press of `Settings::adsButtonVK` while Locked. Re-enabled in
  `main.cpp`. See "ADS handling" above.
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
  `bHideCrosshairWhileLocked`, `bShowTargetHealth`, and (2026-08-24)
  `bEndLockOnAds`/`iAdsButton` (renamed from the original
  `bBlockAdsWhileLocked`/`iAdsReleaseKey` when the ADS strategy changed).
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
