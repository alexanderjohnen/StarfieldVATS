# StarfieldVATS — Handoff / Project Snapshot (2026-08-22, evening)

Read this first in a new chat session to pick up where things left off. This is a
point-in-time snapshot; the code and Alexander's own testing may have moved past
some of this by the time you read it — verify against actual file contents and
the SFSE log before trusting anything below as still true.

## What this is

An SFSE mod for Starfield recreating **Fallout 76-style real-time VATS**: a
hotkey locks a target while the game keeps running (no time-slow, no menu
pause). Project root: `C:\Dev\StarfieldVATS`. Game: Steam,
`G:\Program Files (x86)\Steam\steamapps\common\Starfield`, v1.16.244.

Alexander tests in-game; Claude cannot. Iterative build → deploy → Alexander
tests → reports back → repeat. **Always read the SFSE log yourself**
(`Documents\My Games\Starfield\SFSE\Logs\StarfieldVATS.log`, readable directly
via the Read tool) rather than asking Alexander to paste it.

## Build & deploy

```
cd C:\Dev\StarfieldVATS
powershell -ExecutionPolicy Bypass -File deploy.ps1
```

Fails with a file-lock error whenever Starfield is running — ask Alexander to
close it first (occasionally a dead `Starfield.exe` lingers after the window
closes; check Task Manager if a retry doesn't help). `deploy.ps1` never
overwrites the live `StarfieldVATS.ini` once it exists, only seeds it on first
deploy — new INI keys added in code must also be hand-added to both
`res\StarfieldVATS.ini` (the template) and the deployed copy at
`G:\...\Data\SFSE\Plugins\StarfieldVATS.ini` (Alexander's live config) if you
want them visible/documented there, though the coded default kicks in either
way if the key is simply missing.

## Architecture (current files)

- `src/main.cpp` — SFSE entry point, installs the render hook and hotkey
  watcher on `kPostDataLoad`.
- `src/HotkeyWatcher.cpp/h` — polls `GetAsyncKeyState` for the configured
  activation key (`Settings::activationKeyVK`, INI `[Controls] iActivationKey`,
  Alexander's live setting is **N**, decimal 78), edge-triggered, calls
  `Controller::RequestAdvance()`.
- `src/VATSController.cpp/h` — the state machine. **Two states: Off ↔
  Locked** (collapsed today from an earlier three-state Off/Aiming/Locked
  design — see "Design decisions" below for why). `Advance()` runs on an SFSE
  task-thread queue (game thread), not the render thread. Also owns:
  - The scanner auto-close-on-lock logic (`CloseScannerIfOpen`,
    `SendKeyPress`) — simulates a real OS-level keypress of the scanner's own
    toggle key via `SendInput`, with a retry loop for a known submenu edge
    case. See "Hard-won lessons".
  - `ForceOff()` — thread-safe, callable directly from the render thread (no
    task-queue hop needed, touches only our own state), used to hard-end a
    Locked session when certain menus/transitions open. See "Hard-won
    lessons".
- `src/Targeting.cpp/h` — target acquisition:
  - `GetCrosshairActivationTarget()` — **primary and, as of today, only path
    actually used for locking**. Reads `PlayerCharacter::commandTarget`
    (offset `GameOffsets::kPlayerCommandTarget` = 0x0F90) directly. Real
    engine-computed crosshair/activation pick, pixel-precise, respects real
    geometry occlusion for free. Plain data read, no function call.
  - `FindNearestActorToCrosshair()` — the older cell-scan/cone-angle
    fallback. **No longer called from the lock path at all** (that fallback
    usage was removed 2026-08-22 for reintroducing through-wall targeting,
    twice, in two different files — see prior lessons). Still present in the
    file but effectively dead code now that Aiming mode (its last caller) is
    gone; not yet deleted, worth doing in a future cleanup pass.
- `src/UI/Overlay.cpp/h` — the HUD: renders the "VATS: OFF/LOCKED" status
  readout (top-left, always on while gameplay-visible), the Locked target
  box, and the "TARGETING (N)" hotkey hint (shown while Off, scanner open,
  and something's under the crosshair). Also owns the menu-gating logic that
  calls `Controller::ForceOff()` — see "Hard-won lessons".
- `src/UI/CameraProject.cpp/h` — self-computed pinhole-camera world→screen
  projection. Unchanged this session.
- `src/UI/D3DHook.cpp/h` — the render hook itself. Unchanged, stable.
- `src/GameOffsets.h` — every empirically-verified struct offset, centralized.
  **Never trust a CommonLibSF header offset without in-game verification.**
- `src/Settings.h/cpp` — INI-backed settings (`StarfieldVATS.ini`). Now has
  two independent key settings: `activationKeyVK` (the mod's own hotkey,
  Alexander's live value: N) and `scannerToggleKeyVK` (the *real in-game*
  hand-scanner keybind, Alexander's: Q — default 0x51). These are
  deliberately separate concepts; don't conflate them.

## Current status: two-state lock-on with several menu/transition edge cases fixed today

### Confirmed working (Alexander tested, in-game, today)
- Off → Locked on a single hotkey press, **only while the hand scanner is
  open** — pressing the hotkey with the scanner closed is now a no-op
  (`[VATS] ignored (scanner not open)` in the log), by design (see "Design
  decisions").
- Locked persists and tracks correctly regardless of camera direction, same
  as before the state-machine simplification.
- Scanner auto-closes on lock with the *real* close animation/sound (via
  simulated keypress, not a hard UI-message hide) — confirmed working in the
  base scanner view, and confirmed working (after a retry-loop fix) from
  inside the Techrunner scan-action submenu (E on an NPC) too.
- VATS now actually **ends** (not just visually hides) when any of these open
  while Locked: Pause menu (Esc), the Tab-opened character hub ("DataMenu" —
  Status/Inventory/Map/Quests tabs), Dialogue, a cell-transition loading
  screen. Previously the overlay was merely hidden during some of these
  (`UI::menusVisible`), which left a stale Locked state hanging in the
  background — Alexander flagged this after seeing "VATS: LOCKED" and the
  target box rendering through both the Tab character hub and the Esc pause
  menu (two separate screenshots, 2026-08-22).

### Known-open / unconfirmed
- **`StarMap` menu-name is an unverified guess.** CommonLibSF has no
  standalone RTTI class for it (only a large family of nested
  `StarMap__BodyInfoToUI`/`GalaxyMarkerData`/etc. UI-data structs strongly
  suggesting that's the real top-level name) — used in the `ForceOff()` gate
  anyway since a wrong `IsMenuOpen` name just degrades to a permanent no-op,
  never a crash. **Not yet confirmed** whether opening the star/system map
  while Locked actually ends VATS — ask Alexander to check.
- **`FindNearestActorToCrosshair()` (the cell-scan fallback in
  `Targeting.cpp`) is now dead code** — nothing calls it since Aiming mode
  was removed. Not deleted yet; candidate for a cleanup pass, but leaving it
  for now in case some future feature (e.g. body-part visibility work) wants
  a fallback scan again.
- **"Und sowas" — Alexander expects more menu/transition edge cases to turn
  up** that should also `ForceOff()` but haven't been hit/reported yet. The
  agreed approach (his call, 2026-08-22): fix these **reactively** as he
  finds them in play, using the real menu name each time (same
  `IsMenuOpen("X")` pattern as `PauseMenu`/`DataMenu`/`DialogueMenu`/
  `LoadingMenu`), rather than trying to preemptively enumerate every vanilla
  + mod menu now.
- Body-part-level visibility (hit-chance formula input) — **still not
  solved**, unchanged from before this session. Havok/NiPick raycasting
  confirmed a dead end (no mapped functions). Depth-buffer/stencil sampling
  idea still unexplored. See [[starfield-vats-mod-design]] for full detail.
- Body-shaped silhouette outline (cosmetic) — still deliberately not
  pursued, unchanged from before this session.

## Design decisions (locked, updated today)

- **Two-state Off/Locked toggle, not three-state Off/Aiming/Locked.** The
  Aiming state (a dimmer highlight box, committed to Locked on a *second*
  press) was added earlier today specifically so Alexander could see the
  highlight before committing. A few messages later, once activation was
  gated on the scanner being open, Alexander asked "do we still need Aiming
  at all?" — reasoning: the scanner's own vanilla highlight, plus the
  always-shown "TARGETING (N)" hint (which already runs in the Off state,
  independent of any VATS mode), already give that same "see before
  committing" feedback. Agreed and removed the same session. **If a future
  request sounds like "bring back a two-step confirm before locking," reread
  this — it was tried, and removed by explicit request, not by accident.**
- **Activation requires the hand scanner to be open.** Originally VATS could
  be triggered from anywhere; Alexander flagged this as wrong (no highlight
  UI would even be visible). Gated in `VATSController::Advance()`'s Off
  branch via `IsMenuOpen("MonocleMenu")`. Locked state itself is **not**
  gated this way — it persists after the scanner closes (that's the point of
  the auto-close-on-lock feature below), only the *entry* transition checks.
- **Scanner closes itself via a simulated real keypress, not an engine
  message.** `UIMessageQueue::AddMessage("MonocleMenu", kHide)` was the first
  attempt — works, but closes with zero transition (no animation, no sound).
  Alexander asked directly: "what stops us from just sending a keypress, like
  I pressed Q myself?" Investigated whether CommonLibSF exposes a cleaner
  *internal* input-injection path first (`BSInputDeviceManager`/`ButtonEvent`
  — RTTI-confirmed classes exist, but zero mapped member functions, so no
  safe internal call is available) — concluded Alexander's OS-level
  `SendInput` idea was actually the better approach anyway, not just a
  fallback: Starfield can't distinguish it from a real press (single-player,
  no anti-cheat), so it gets the genuine native animation/sound for free
  without needing any engine internals at all.

## Hard-won lessons from today (read before repeating any of this work)

1. **A Win32 `SendInput` down+up with zero delay can be silently missed.**
   First close-scanner attempt sent both events back-to-back with no gap and
   the scanner didn't close. Almost certainly the same class of bug as our
   own `HotkeyWatcher`'s `isDown`/`wasDown` edge-trigger polling, just
   experienced from the other side: whatever poll-driven edge detection the
   game uses internally can land in the gap and never observe the "down"
   state. Fixed with a real hold duration (~60ms) between down and up, sent
   from a detached background thread so it doesn't block the game thread.
2. **One simulated keypress doesn't always fully close a menu — sub-states
   within the same top-level menu name aren't distinguishable via
   `IsMenuOpen`.** Pressing E on an NPC while scanning opens a scan-action
   submenu (Techrunner's Scan/Mark/etc. list) that's still part of the same
   `MonocleMenu` Scaleform (confirmed via CommonLibSF's ID list —
   `MonocleMenu_Bioscan`, `MonocleMenu_SocialSpell`, etc. are all sub-events
   of the one menu, no separate name to check). A single close-keypress in
   that state only backs out of the submenu into the normal scan view — the
   scanner itself stays open, and we have no way to tell which sub-state
   we're in via any mapped API. Fixed with a retry loop (`CloseScannerIfOpen`
   in `VATSController.cpp`: press, wait ~250ms, check `IsMenuOpen` again,
   repeat up to 3 times) instead of guessing the exact sub-state — this
   generalizes to any depth of nesting without needing new detection code
   per case.
3. **Hiding an overlay is not the same as ending the state it represents —
   and the "is a menu blocking gameplay" signal isn't one field.**
   `UI::menusVisible` was already used to hide the HUD during "normal"
   blocking menus (pause, inventory), but (a) it left Locked mode dangling
   underneath even while hidden, and (b) Starfield's Tab-opened character
   hub ("DataMenu") doesn't flip `menusVisible` at all, so the overlay leaked
   straight through it (screenshot-confirmed) — and separately the Esc pause
   menu leaked through too in another screenshot, meaning even the "already
   fixed" case wasn't fully fixed. Alexander's explicit correction: such
   menus should **end** VATS, not just visually hide it. Fixed with
   `Controller::ForceOff()` (safe to call directly from the render thread —
   it only touches our own atomic/mutex state, no engine calls, so no
   SFSE-task-queue hop needed unlike `Advance()`) called from
   `Overlay::Draw()` whenever `menusVisible` is false OR any of
   `PauseMenu`/`DataMenu`/`DialogueMenu`/`LoadingMenu`/`StarMap` is open
   (first four RTTI-confirmed in CommonLibSF, `StarMap` an educated guess —
   see "Known-open" above).
4. **CommonLibSF's Starfield port has real RTTI-confirmed classes with zero
   mapped member functions** — found for both `BSInputDeviceManager`/
   `ButtonEvent` (this session) and (from an earlier session) Havok/NiPick.
   The RTTI table (`IDs_RTTI.h`) proves a class *exists* and gives you its
   singleton/vtable location, but says nothing about whether any of its
   methods are safely callable — check `grep`-ing for a dedicated header
   wrapper (e.g. `RE/B/BSInputDeviceManager.h`) before assuming a class with
   an RTTI hit is usable the way a fully-wrapped Skyrim-CommonLib class
   would be. See [[commonlibsf-unmapped-ids]] for the general pattern.
5. **Deploy failures aren't always "game still open"** — occasionally a dead
   Starfield process lingers after the visible window closes; check Task
   Manager if a deploy keeps failing despite Alexander confirming the game is
   closed. (Carried over from a prior session, still true.)

## Reference: menu names confirmed usable via `RE::UI::IsMenuOpen` today

All via the same low-risk pattern already established for `MonocleMenu` (a
string lookup, degrades to "always false" if wrong, never crashes):

- `PauseMenu` — RTTI-confirmed (ID 857854). The Esc menu (Quicksave/Save/
  Load/Settings/etc.).
- `DataMenu` — RTTI-confirmed (ID 857723). Tab-opened character hub
  (Status/Inventory/Map/Quests tabs) — confirmed this is what Alexander
  means by "Charaktermenü", per his own description.
- `DialogueMenu` — RTTI-confirmed (ID 864633). NPC conversations.
- `LoadingMenu` — RTTI-confirmed (ID 864734). Cell-transition loading
  screens (matches the `loadingmenu.swf` interface file name from an earlier
  session's research).
- `StarMap` — **unconfirmed**, see "Known-open" above.
- `MonocleMenu` — already known from a prior session, the hand scanner.

## Persistent memory

Alexander's Claude memory system (separate from this file) has three related
entries:
- `starfield-vats-mod-design` — overall design, status, backlog, lessons.
  Updated today with the state-machine simplification, the scanner
  auto-close mechanism, and the `ForceOff` menu-gating pattern.
- `starfield-vats-ui-hook` — render hook + UI overlay specifics. **Not yet
  updated with today's Overlay.cpp changes (Aiming box removal, the
  `ForceOff` gate) — worth doing in a future session if it drifts out of
  sync with the code.**
- `commonlibsf-unmapped-ids` — the general "mapped ID ≠ safe" pattern,
  reinforced today by the `BSInputDeviceManager`/`ButtonEvent` finding.

This file exists as a single self-contained recap for a fresh chat, not a
replacement for those.
