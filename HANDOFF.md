# StarfieldVATS — Handoff / Project Snapshot (2026-08-23)

Read this first in a new chat session to pick up where things left off. This is a
point-in-time snapshot; the code and Alexander's own testing may have moved past
some of this by the time you read it — verify against actual file contents and
the SFSE log before trusting anything below as still true.

## What this is

An SFSE mod for Starfield recreating **Fallout 76-style real-time VATS**: a
hotkey locks a target while the game keeps running (no time-slow, no menu
pause), shows a hit-chance percentage, and the goal is for shots to actually
land according to that percentage — all without the camera/weapon visibly
snapping onto the target. Project root: `C:\Dev\StarfieldVATS`, **now a proper
git repo with real commit history** (was not, for the whole project, until
2026-08-22 — every commit since then is a real checkpoint, use `git log`/`git
diff`/`git bisect` instead of guessing at what changed). Game: Steam,
`G:\Program Files (x86)\Steam\steamapps\common\Starfield`, v1.16.244.

Alexander tests in-game; Claude cannot. Iterative build → deploy → Alexander
tests → reports back → repeat. **Always read the SFSE log yourself**
(`Documents\My Games\Starfield\SFSE\Logs\StarfieldVATS.log`, readable directly
via the Read tool) rather than asking Alexander to paste it. **Commit after
every deployed build**, win or lose — this was a hard lesson (see below).

## Build & deploy

```
cd C:\Dev\StarfieldVATS
powershell -ExecutionPolicy Bypass -File deploy.ps1
```

Fails with a file-lock error whenever Starfield is running — ask Alexander to
close it first. `deploy.ps1` never overwrites the live `StarfieldVATS.ini`
once it exists, only seeds it on first deploy — new INI keys added in code
must also be hand-added to both `res\StarfieldVATS.ini` (the template) and the
deployed copy at `G:\...\Data\SFSE\Plugins\StarfieldVATS.ini` (Alexander's live
config).

## THE BIG OPEN PROBLEM: making hitscan weapons actually hit/miss per the rolled chance

This is what most of the last session was spent on, unsuccessfully so far.
**Read this whole section before touching aim-assist/hit-resolution code
again** — it records several dead ends in detail specifically so they aren't
retried.

### The goal

Weapon/camera should never visibly snap onto the target (explicitly requested
— an earlier `SendMouseMove`-based camera-steering design was removed for
exactly this reason). Instead, the *shot itself* should be steered to land per
the rolled hit/miss, the way classic Bethesda VATS (FO3/NV/FO4) does it —
bend the actual fired shot, not the camera.

### Finding #1: most Starfield weapons are hitscan, not simulated projectiles

Confirmed via `BGSProjectileData::BGSProjectileFlags::kHitScan` (see
`lib/commonlibsf/include/RE/B/BGSProjectile.h`) and cross-referenced against
known Creation Engine (FO4-lineage) behavior: **standard ballistic weapons
resolve via an instant hitscan raycast in the same frame as the trigger
pull.** There is no real, findable `RE::Projectile` world object to redirect
in-flight for ordinary guns — only slow ordnance (rockets, grenades, plasma)
spawns one. This was confirmed the hard way: `ProjectileTracker.cpp`'s cell-
reference-array scan (which DOES correctly find real projectiles for slow
ordnance — see below) never once found a real bullet across many logged test
shots with a standard ballistic weapon, only a persistent `kPBEA`
(BeamProjectile) entry that turned out to be the weapon's laser-sight
attachment, not ammunition. Alexander confirmed no beam/laser weapons (e.g.
the Cutter) were in use during that test, ruling out a weapon-type confound.

**Consequence:** `ProjectileTracker::RedirectFreshProjectiles` (redirects a
real in-flight `Projectile`'s `velocity`/`movementDirection`, and now also
`desiredTargetHandle` — see below) **only ever helps with rockets/grenades/
plasma, never with standard guns.** Don't expect it to fire for a pistol/rifle
test — that's correct, expected behavior, not a bug to chase.

### Finding #2: classic VATS bends the fire vector, doesn't touch the camera

Confirmed via direct outreach to the actual author of one of the two existing
Fallout 76-style real-time VATS mods for Fallout 4 (Nexus:
"Fallout 76 VATS - F4SE" and "V.A.T.S. From Fallout76" — both closed-source,
no code available). Their answer, paraphrased: separate target selection from
the normal crosshair-based hit calculation; classic Bethesda VATS rolls
hit/miss *before* firing, then bends the actual fire vector so the shot
resolves per that roll — this is why VATS bullets visibly curve in FO3/NV/FO4.
They could not give exact Starfield internals (never worked with Starfield).

### Finding #3: Starfield DOES have a native "bullet bending" aim-assist system — found and reached, but doesn't work when force-enabled

This is the most concrete lead so far. Full pointer chain, **confirmed live
in-game 2026-08-22/23** (cross-validated against a known-good sibling field's
`formType`, not just a guess that compiles):

```
Actor::inventoryList                                    (TESObjectREFR + 0xA0,
                                                           BSGuarded<BGSInventoryList*,
                                                           BSReadWriteLock> — read the
                                                           raw pointer directly, do
                                                           NOT call
                                                           BSReadWriteLock::LockRead/
                                                           UnlockRead — see "dead
                                                           ends" below)
  -> BGSInventoryList::data                              (+0x28, BSTArray<BGSInventoryItem>,
                                                           same {size@0,cap@4,data@8}
                                                           shape used elsewhere in this
                                                           project)
    -> first item where object->formType==kWEAP (0x30)
       AND IsEquipped() (item.flags & 0x7)
      -> BGSInventoryItem::instanceData                  (+0x08, BSTSmartPointer —
                                                           _ptr at +0 — this is the
                                                           LIVE per-item instance,
                                                           mods/attachments applied,
                                                           NOT TESObjectWEAP's static
                                                           form template)
        -> WeaponDataAim*                                (TESObjectWEAPInstanceData + 0x18)
          -> aimModel (BGSAimModel*)                      (+0x28 — cross-check field,
                                                           formType must read kAMDL/0x93;
                                                           confirmed MATCH on every shot)
          -> BGSAimAssistModel*                           (+0x38 — NOT +0x30, that slot
                                                           is a BGSWwiseEventForm/audio
                                                           cue, a dead end already tried
                                                           and ruled out; formType
                                                           confirmed kAAMD/0x94 on every
                                                           shot)
            -> AimAssistData (embedded inline at +0x38 within the form,
               per RE::BGSBaseFormT<T,...>::data — NOT a further pointer)
              -> bulletBendingConeAngle (+0x38 within that: reads 12.0,
                 a real/sane value)
              -> aimAssistEnabled (+0x5C: reads FALSE by default)
```

**Test performed:** force-wrote `aimAssistEnabled = true` on every shot.
Confirmed via logging that the write held (stayed `true` across many
subsequent shots/reads, was not reset by the engine each frame). **Alexander
confirmed: no visible change in actual shot/hit behavior.** So flipping this
one bool is NOT sufficient by itself.

### Finding #4: no further plain-data lead exists — the real gate is a function, not a field

Two follow-up research passes (background agents, thorough) both dead-ended:

- **No separate "current aim-assist target" field exists anywhere** (checked
  `PlayerCharacter.h`, `Actor.h` in full — every candidate offset near
  `commandTarget`/0x0F90 is still an unlabeled `unk0Fxx`/`unk10xx`). The real
  mechanism is an **event/query pattern**, not a stored field:
  `PlayerAutoAimActorEvent` (a `BSTValueRequestEvent<...>` — ask/answer, not a
  passive value) and `IAimAssistImpl`/`RangedAimAssistImpl`/`MeleeAimAssistImpl`
  (the actual per-shot decision functors). **All of these exist only as
  RTTI/VTABLE symbol IDs in `IDs_RTTI.h`/`IDs_VTABLE.h` — CommonLibSF has
  never given them a header/class layout.** REL::IDs found (non-zero, i.e.
  mapped, but never independently verified in this project):
  `RangedAimAssistImpl` RTTI 851442, `IAimAssistImpl` RTTI 851443,
  `MeleeAimAssistImpl` RTTI 851444.
- **No global "active input device" (gamepad vs. mouse+KB) plain field
  either.** `BSInputDeviceManager` and `ControlMap` are both RTTI/VTABLE-only,
  no header. `InputEvent::DeviceType` enum exists
  (`kKeyboard/kMouse/kGamepad/kKinect`) but only per-event, not as a
  persistent "current scheme" flag anywhere mapped.

**Bottom line: the only remaining path to make native aim-assist actually
work is hooking `RangedAimAssistImpl` (or similar) — a real, never-before-
exercised engine function call, exactly the risk category that has caused
every one of this project's crashes so far** (see
`commonlibsf-unmapped-ids` memory — mapped ≠ safe). This is a genuine
decision point, not a "just try it" — Alexander was asked directly and chose
**"keep pursuing native aim-assist"** over a subtle-camera-nudge fallback or
leaving hitscan weapons unaffected, **fully aware this now means either a
risky function hook, or more research to find an even-narrower plain-data
lever if one exists.** Don't re-litigate this choice without new information;
do bring it up again if the hook attempt itself produces a bad result (crash,
no effect, etc.) — that would be new information.

### What NOT to re-try (confirmed dead ends, don't re-derive)

- `WeaponDataAim + 0x30` is **not** `BGSAimAssistModel*` — it's a
  `BGSWwiseEventForm` (audio cue), formType `kWWED`/0xC0. The real one is
  `+0x38`.
- `MiddleHighProcessData::lastBoundWeapon`
  (`Actor::currentProcess->middleHigh->lastBoundWeapon`, offset 0x450) is
  **not** "currently equipped weapon" despite the name — confirmed null on
  every single shot in-game across a full test session, despite
  `currentProcess`/`middleHigh` both resolving fine. Use the
  `Actor::inventoryList` path instead (Finding #3 above).
- Forcing `AimAssistData::aimAssistEnabled = true` alone does **not** change
  shot behavior (Finding #3).
- `RE::ProcessLists` has no projectile-tracking field in either Starfield's or
  Skyrim SE's CommonLib headers — only actor-handle arrays. Don't look there
  for "list of active projectiles."
- Don't call `BSReadWriteLock::LockRead()`/`UnlockRead()` — already on the
  project's "mapped but crashed anyway" list (via
  `TESObjectCELL::ForEachReference`'s internal use of it, an earlier
  session). Read guarded (`BSGuarded<T, BSReadWriteLock>`) fields' raw
  pointer directly instead, unsynchronized, SafeRead-guarded — this project's
  established risk tradeoff (see `SafeMem.h`/`ProjectileTracker.cpp`'s
  comments on why an unsynchronized plain-data read/write is judged lower
  risk than a fresh engine function call).

### Side improvement made along the way (works, unrelated to the hitscan problem)

`ProjectileTracker::RedirectFreshProjectiles` (for the rockets/grenades case
that DOES have a real Projectile object) now also writes
`desiredTargetHandle` (`RE::Projectile` + 0x184) to the target's formID on a
rolled hit, in addition to the existing direct `velocity`/`movementDirection`
override — Alexander's observation that ship-combat missile lock-on already
does real-time native homing, and `desiredTargetHandle` is presumably what
drives it. Untested whether this actually improves anything over the direct
velocity write alone (both are applied together; the direct write already
guarantees this frame's redirect regardless). Uses the target's formID as a
handle stand-in (same assumption as the player-handle trick elsewhere) —
not guaranteed correct for a dynamically-spawned NPC, but degrades gracefully
(bad handle = no extra native homing, not a crash).

## Other current status

### Confirmed working
- Off → Locked on a single hotkey press, only while the hand scanner is open.
- Locked persists and tracks correctly regardless of camera direction.
- Scanner auto-closes on lock via simulated keypress (`iScannerCloseMode=1`,
  default) — confirmed reliable **as long as `EngineInputLayer::SetBlocked`
  is NOT active at the same time** (see below).
- VATS ends (not just hides) on Pause/DataMenu/Dialogue/LoadingMenu/StarMap
  (StarMap name still unconfirmed guess).
- Distance-based hit chance (replaced an earlier, explicitly-rejected
  screen-space "must aim precisely at crosshair" cone — Alexander pointed at
  FO4/76 VATS reference screenshots showing high chance despite wildly
  off-crosshair aim). `Settings::fullChanceRangeMeters`/
  `maxEffectiveRangeMeters`, linear falloff, LOS still a hard gate via
  `HasDetectionLOS` (see below — currently a no-op).
- Body-part targeting (Suit/Helmet/Pack) removed entirely per Alexander's
  request — single chest-height aim point now, `GameOffsets::kAimPointChestZ`.
  May come back later, not scoped/started.

### Known-broken / open
- **`HasDetectionLOS` is currently a stub that always returns `true`** — the
  real implementation (a raw hand-cast `REL::ID(170456)` call, calling shape
  copied from Cassiopeia Papyrus Extender's usage, never independently
  verified) was the actual root cause of the multi-session "hard crash on
  locking, no log" bug — see `starfield-vats-mod-design` memory's "Crash on
  locking" section for the full misdiagnosis trail before this was found.
  Confirmed crash-free with it stubbed out. **LOS/wall-blocking for hit
  chance does not currently exist** — only distance matters right now. Needs
  the calling convention re-derived properly (ideally from disassembly, not
  guessing) before re-enabling.
- **`EngineInputLayer::SetBlocked` is disabled again** (2026-08-23, third
  time this has flipped). History: originally disabled on suspicion of
  causing the crash above (wrong — cleared once `HasDetectionLOS` was found).
  Re-enabled to fix "Tab opens DataMenu instead of ending the lock" — caused
  a *new*, confirmed problem: it broke the scanner-close-via-keypress
  mechanism (0/N successes, log-confirmed — fixed by reordering so
  `SetBlocked(true)` only applies after the scanner-close sequence finishes).
  Even after that reorder, direct in-game testing showed
  `USER_EVENT_FLAG::TabMenuMaybe` blocks **far more than Tab** — Alexander
  reported the favorites menu and other unrelated single-key menu shortcuts
  all went dead while Locked. AND it still didn't fix the original problem —
  `BackKeyInterceptor`'s own diagnostic logging (still in the code, logs
  every matching keydown regardless of outcome) shows **zero "back key seen"
  events across a full test session**, meaning the low-level keyboard hook
  isn't even firing for Tab, a separate, still-unsolved problem. Net result:
  disabled again, real collateral damage with no confirmed benefit. **Don't
  re-enable without first confirming exactly what `TabMenuMaybe` gates** (the
  CommonLibSF header itself marks it "Unconfirmed").
- **Tab still does not end the lock.** `BackKeyInterceptor.cpp`'s
  `WH_KEYBOARD_LL` hook installs successfully every session
  ("back-key interceptor started" always logs) but never once logs
  "back key seen" despite Alexander confirming he pressed Tab while Locked.
  The hook pattern is otherwise proven (identical structure to `AimAssist`'s
  mouse hook, which demonstrably works — many "hold started" log lines).
  Not yet root-caused. Worth checking: is Tab perhaps intercepted by
  something else (Windows, Steam overlay, another mod) before reaching a
  low-level hook at all? Or is `Settings::backKeyVK` reading incorrectly? Log
  shows `iBackKey=0x9` (correct, VK_TAB) at startup.
- `VKToDisplayLabel` (Overlay.cpp) now supports F1-F24 in the "TARGETING (N)"
  hint (Alexander rebound `iActivationKey` to F17/0x80) — was previously
  letters/digits only, showing "?".
- Body-part-level visibility / occlusion for hit-chance — not solved, see
  `starfield-vats-mod-design` memory for the deeper history.

## Architecture (current files, brief — read the actual files for detail, comments are extensive and mostly still accurate)

- `src/main.cpp` — SFSE entry, installs hooks/watchers on `kPostDataLoad`.
- `src/HotkeyWatcher.cpp/h` — polls for the activation key, edge-triggered.
- `src/VATSController.cpp/h` — Off/Locked state machine, scanner auto-close,
  `ForceOff()`.
- `src/Targeting.cpp/h` — `GetCrosshairActivationTarget()` (the only path
  actually used for locking), `HasDetectionLOS` (currently stubbed true, see
  above), `FindNearestActorToCrosshair` (dead code, unused fallback).
- `src/AimAssist.cpp/h` — fire-button hook, hit/miss roll (distance-based
  chance), calls `ProjectileTracker::RedirectFreshProjectiles` and
  `AimAssistProbe::ForceAimAssist` per shot.
- `src/AimAssistProbe.cpp/h` — the native-aim-assist pointer chain (Finding
  #3 above). Currently force-writes `aimAssistEnabled=true` (confirmed
  insufficient alone — see above). Next work on the hitscan problem happens
  here or in a new file alongside it.
- `src/ProjectileTracker.cpp/h` — redirects real, slow-ordnance
  `RE::Projectile` objects only (rockets/grenades/plasma) — velocity/
  movementDirection + desiredTargetHandle. Does nothing for hitscan weapons
  by design (there's no object to find).
- `src/UI/Overlay.cpp/h` — HUD: status readout, target box, "TARGETING (N)"
  hint (now F-key aware), the `ForceOff()` menu-gating logic.
- `src/UI/CameraProject.cpp/h`, `src/UI/D3DHook.cpp/h` — render hook + screen
  projection, stable, not touched recently. `WorldToScreen` no longer depends
  on ImGui's `io.DisplaySize` (crashed once when the Present hook lost a
  race with BetterConsole for the swapchain vtable — `D3DHook` now tracks
  backbuffer size in atomics independent of ImGui; `D3DHook` also has a
  vtable-patch fallback for when MinHook itself fails to hook, same BetterConsole
  race).
- `src/EngineInputLayer.cpp/h` — currently unused (see "Known-broken" above),
  kept for a future, better-understood re-enable.
- `src/GameOffsets.h`, `src/SafeMem.h/cpp` — centralized verified offsets;
  `SafeRead`/`SafeWrite`, SEH-guarded.
- `src/Settings.h/cpp`, `res/StarfieldVATS.ini` — INI-backed config.

## Hard-won lessons (don't repeat)

1. **Don't chase the last log line before a crash without confirming it's
   the cause, not just the last thing that happened to log.** The
   `HasDetectionLOS` crash took 5 wrong turns (EngineInputLayer, IsMenuOpen-
   on-wrong-thread, ImGui/display-size, scanner-close mechanism, disabling
   AimAssist entirely) before the real cause was found — each looked
   plausible from timing alone. What actually worked: asking Alexander to
   pin down the last known-good build from memory (a specific feature it
   predated), since there was no git history to bisect against at the time.
2. **This project now has real git history — use it.** Commit after every
   deployed build, whether it worked or not. The lack of this cost a full
   afternoon of manual archaeology once (see lesson 1).
3. **A mapped, non-zero `REL::ID` is not proof of safety** — 4+ confirmed
   crashes/dead-ends from mapped-but-untested engine function calls this
   project alone (`BSPointerHandleManagerInterface::GetSmartPointer`,
   `TESObjectCELL::ForEachReference`'s internal `LockRead`,
   `HasDetectionLOS`, `Actor::GetActorKnowledge` was zeroed entirely). Prefer
   plain data reads/writes over any wrapper ending in a function call. This
   is the central tension in the current open problem (Finding #4 above) —
   there's no more plain-data lead left for native aim-assist, only a
   function hook.
4. **Struct-offset claims (even from CommonLibSF, even with a plausible
   field name) need in-game cross-validation, not just "it compiles."**
   `lastBoundWeapon` sounded exactly right and was completely wrong.
   `WeaponDataAim + 0x30`'s own header comment said "AimModelData" and was
   also wrong (a stale/misapplied label — the real AimModelData-shaped
   neighbor is `aimModel` at +0x28, one slot earlier). The technique that
   actually worked both times: sweep every candidate field in a struct and
   check `formType` against the expected value, rather than trusting a
   single guess.
5. **A "fix" can have collateral damage that isn't visible until directly
   tested in-game** — `EngineInputLayer::SetBlocked` looked correct on paper
   twice (both times) and broke something unrelated both times (scanner-
   close, then favorites-menu-and-more). Ask Alexander to specifically test
   *unrelated* systems after any engine-input-layer change, not just the
   thing you were trying to fix.
6. **Don't remove a working, well-liked mechanism because of a
   misattributed bug.** Alexander was visibly frustrated once this session
   when a scanner-close mode that had worked reliably for a long time kept
   getting swapped away based on thin evidence — the actual fix was
   reordering *around* it, not replacing it. If something Alexander says
   "works fine" is implicated by a new bug, look harder for what changed
   around it before touching the thing itself.

## Persistent memory (separate from this file)

- `starfield-vats-mod-design` — overall design, status, backlog, the full
  "Crash on locking" misdiagnosis history, ship-combat VATS backlog idea.
- `starfield-vats-ui-hook` — render hook + UI overlay specifics, the
  BetterConsole vtable race, ImGui/display-size fix.
- `commonlibsf-unmapped-ids` — the general "mapped ID ≠ safe" pattern.
- `feedback-commit-every-vats-build` — commit after every deployed build,
  no exceptions.

This file exists as a single self-contained recap for a fresh chat, not a
replacement for those — but for the hitscan/aim-assist problem specifically,
this file is currently the most complete record.
