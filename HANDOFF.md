# StarfieldVATS — Handoff / Project Snapshot (2026-08-23, evening)

Read this first in a new chat session to pick up where things left off. This is a
point-in-time snapshot; the code and Alexander's own testing may have moved past
some of this by the time you read it — verify against actual file contents and
the SFSE log before trusting anything below as still true.

**This supersedes the previous version of this file from earlier today.** The
hitscan/real-projectile problem that previous version called "THE BIG OPEN
PROBLEM" has a working solution now — see below. Don't re-read the old framing
as still-open; it isn't.

## What this is

An SFSE mod for Starfield recreating **Fallout 76-style real-time VATS**: a
hotkey locks a target while the game keeps running (no time-slow, no menu
pause), shows a hit-chance percentage, and shots land (or deliberately miss)
according to that percentage — all without the camera/weapon visibly
snapping onto the target. Project root: `C:\Dev\StarfieldVATS`, git repo with
real commit history since 2026-08-22. **Now public and open-source** at
https://github.com/alexanderjohnen/StarfieldVATS (GPL-3.0, inherited from
CommonLibSF) — `docs/FINDINGS.md` there has the confirmed struct-offset
corrections in a cleaner, more citable form than this file; `docs/
CONTRIBUTIONS.md` documents who did what. Keep both in sync with major
changes going forward. Game: Steam, `G:\Program Files (x86)\Steam\steamapps\
common\Starfield`, v1.16.244.

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
once it exists, only seeds it on first deploy.

## THE HITSCAN PROBLEM — SOLVED (mostly), here's how

Alexander's idea: instead of redirecting an instantaneous hitscan raycast
(which has no in-flight object to redirect, and would have required
disassembling Starfield's internal `RangedAttackModule`/aim-assist functions —
high-risk, unmapped, no header, the thing the *previous* version of this file
spent a full section warning about), flip the weapon's ammo into behaving like
a **real, simulated projectile** at runtime while Locked. Then the
already-working projectile redirect (rockets/grenades) just... works on
regular guns too.

**Confirmed via `ProjectileFlagProbe.cpp` (read-only diagnostic) across 9
weapons** (7 hitscan, a rocket launcher, and "Shingen" — a Nexus Mods
homing-bullet weapon inspected via xEdit for ground truth): a byte at
`BGSProjectile::data + 0x84` reads `0x00` on both confirmed-real-projectile
weapons and `0x02` on every hitscan weapon, zero exceptions. `flags`/
`gravity`/`speed`/`range` for Shingen matched its xEdit-authored values
exactly, so the offset chain to get there is solid — see `docs/FINDINGS.md`
for the full offset corrections this took (CommonLibSF's headers were wrong
in five separate places along this chain, each independently verified).

**`ProjectileTypeOverride.cpp`** force-writes `0x00` into that byte on the
equipped weapon's live projectile (`WeaponAmmoData+0x20` — also undocumented
in the header, found by sweeping for a `formType==kPROJ` pointer) for the
duration of one held trigger, restores the original value when the hold ends.
Reference-counted per projectile pointer (not a single token) because two
holds can legitimately overlap in time now — see the click-handling fix
below.

**Confirmed working in-game**: hitscan weapons now visibly curve their shots
toward a Locked target, matching the rolled hit/miss chance, the same way
rockets/grenades already did. Several follow-on bugs were found and fixed
while testing this (see "Lessons" below) — as of this writing, Alexander has
not reported a "goes straight" case since the last fix (kPBEA exclusion +
the click/concurrency fix), but this has happened before and then recurred
for a different reason, so don't treat it as fully closed without more
testing.

**What's NOT done**: there's no ground-truth confirmation that a redirected
shot actually deals damage in the right place — everything so far only
confirms the round's velocity/direction was rewritten, not what happened
after. An attempt to wire up `RE::TESHitEvent` for this crashed the game
instantly (see "Known-broken/open" below) and is currently disabled.

## Other current status

### Confirmed working
- Off → Locked on a single hotkey press, only while the hand scanner is open;
  scanner auto-closes via simulated keypress (`SendInput`, Alexander's idea —
  see `docs/CONTRIBUTIONS.md` for why this beat the alternatives).
- Locked persists/tracks correctly regardless of camera direction; ends
  outright (not just hides) on Pause/DataMenu/Dialogue/LoadingMenu/StarMap.
- Distance-based hit chance (LOS gate exists at lock-time via `commandTarget`,
  see below for the *not*-yet-done per-shot LOS gating).
- Hitscan-to-real-projectile conversion + redirect (this session's main work,
  see above).
- Overlay no longer draws over native `MessageBoxMenu` popups (found today —
  Alexander screenshotted the VATS HUD overlapping a Scanner tutorial tip).

### Known-broken / open
- **No ground-truth hit confirmation.** `RE::TESHitEvent::GetEventSource()`
  (or the generic `BSTEventSource<T>::RegisterSink` it needs — first time
  this project has used either) hard-crashes on every launch: `REL/IDDB.cpp
  (417): Failed to find offset for Address Library ID! Invalid ID: 0`. At
  least one required `REL::ID` isn't mapped in the Address Library for
  1.16.244.0. `src/HitEventLogger.cpp/h` has the intended implementation,
  but its `Start()` call in `main.cpp` is commented out. Needs either a
  mapped alternative event/ID, or reporting the gap upstream to CommonLibSF.
  This is the most valuable next thing to unblock — it's the only way to
  stop inferring "did the redirect actually work" from indirect signals.
- **Pose-unaware aim point.** The redirect target is the actor's ref-origin
  (feet) plus a fixed chest-height offset (`GameOffsets::kAimPointChestZ`,
  `AimAssist.cpp`'s `ResolveAimWorldPos`) — doesn't account for prone/crouch.
  Alexander reported shooting a prone target while the aim point stayed at
  standing-chest height, zero hits. Needs real skeletal/bone-based targeting
  (deferred since the start of the project) or at minimum a posture-aware Z
  offset — not started, explicitly deferred by Alexander to "one thing at a
  time" after the hitscan work.
- **LOS/wall-blocking for hit chance is still a no-op at fire time** (only
  checked once, at lock time, via `commandTarget`). `HasDetectionLOS` is
  still stubbed to always return true (see the older crash history in
  `starfield-vats-mod-design` memory for why).
- **Tab still does not end the lock** (`BackKeyInterceptor.cpp`'s hook
  installs but never logs "back key seen" — root cause not found, see the
  git history for the fuller investigation from 2026-08-22/23).
- The `type=0x02→0x00` finding is a strong, clean correlation across 9
  samples, not an independently proven causal mechanism — see `docs/
  FINDINGS.md`'s explicit caveat on this. If a weapon type ever shows
  different behavior, this is the first thing to re-examine.
- `lib/commonlibsf` has shown a modified submodule pointer (git status) for
  several sessions now, never intentionally committed — hasn't been
  investigated, probably harmless (a local checkout drift), but flag it to
  Alexander if it's still there.

## Architecture (current files, brief)

- `src/main.cpp` — SFSE entry, installs hooks/watchers on `kPostDataLoad`.
  `HitEventLogger::Start()` is commented out (crashes, see above).
- `src/HotkeyWatcher.cpp/h`, `src/BackKeyInterceptor.cpp/h` — hotkey polling.
- `src/VATSController.cpp/h` — Off/Locked state machine, `ForceOff()`.
- `src/Targeting.cpp/h` — `GetCrosshairActivationTarget()`, `HasDetectionLOS`
  (stubbed true).
- `src/AimAssist.cpp/h` — fire-button hook, hit/miss roll, `SteeringLoop()`
  (per-hold redirect scan + type-override engage/disengage). Recently
  changed: releases its single-steering-thread gate as soon as the button is
  released (not after the post-release grace period) so fast follow-up
  clicks aren't silently dropped — see git log 2026-08-23 for why.
- `src/ProjectileTracker.cpp/h` — redirects real, in-flight `RE::Projectile`
  objects (now covers both original slow-ordnance weapons AND hitscan
  weapons once type-overridden). Excludes `kPBEA` (BeamProjectile,
  0x4F) from its candidate scan — that's a weapon's decorative laser-sight
  beam, not the actual fired round; redirecting it instead of the real round
  was one cause of "shots go straight."
- **`src/ProjectileFlagProbe.cpp/h`** (new today) — read-only diagnostic that
  found/confirmed the offset chain and the `type` byte correlation above.
- **`src/ProjectileTypeOverride.cpp/h`** (new today) — the actual
  hitscan-to-real-projectile write, reference-counted per projectile.
- **`src/HitEventLogger.cpp/h`** (new today, disabled) — intended
  ground-truth hit confirmation via `TESHitEvent`, currently crashes.
- `src/AimAssistProbe.cpp/h` — native aim-assist exploration
  (`aimAssistEnabled` force-write, confirmed insufficient alone — superseded
  by the type-override approach above for practical purposes, kept as-is).
- `src/UI/Overlay.cpp/h` — HUD; now also skips drawing (not `ForceOff()`)
  while a `MessageBoxMenu` is open.
- `src/UI/CameraProject.cpp/h`, `src/UI/D3DHook.cpp/h` — render hook +
  projection, stable, not touched this session.
- `src/GameOffsets.h`, `src/SafeMem.h/cpp` — centralized verified offsets;
  `SafeRead`/`SafeWrite`, SEH-guarded.
- `src/Settings.h/cpp`, `res/StarfieldVATS.ini` — INI-backed config.

## Lessons from this session (2026-08-23)

1. **A CommonLibSF struct-offset error isn't always a clean, single wrong
   number — it can be a systemic shift.** `RE::Projectile`'s
   movementDirection/velocity/age were all off by a uniform `-0x10`;
   `BGSProjectile::data`'s base was off by `+0x8`, which then correctly
   explains every field inside it without further per-field guessing. When
   one field looks wrong, check whether a whole neighboring block is
   shifted by a constant before treating each field as an independent guess.
2. **A `static_assert` on total struct size doesn't prove every field inside
   is at the claimed offset.** `AMMO_DATA`'s static_assert passed even
   though its first field (`projectile`) was actually an unresolved
   `TESFormID` + padding, not the `BGSProjectile*` pointer the header
   claimed — an 8-byte pointer and a 4-byte FormID + 4 bytes padding
   produce an identical total size.
3. **`FormTypes.h`'s inline comments are hex without a "0x" prefix, not
   decimal** — misread once (`kAMMO` as `0x1F` instead of `0x31`), which
   made a working pointer chain look broken for a full test cycle.
4. **A byte-for-byte match against an independently-authored reference (a
   real mod's xEdit data) is much stronger confirmation than internal
   self-consistency alone** — this is what turned "plausible offset" into
   "confirmed offset" for the whole `BGSProjectile::data` chain.
5. **Marking an entry "handled" before confirming a write actually
   succeeded can burn its only real attempt.** `ProjectileTracker` used to
   mark a projectile handled as soon as its `shooterHandle`/`age` gates
   passed, before checking whether `velocity` was even non-zero yet (it
   reads zero for the first ~10ms after spawn) — once the shooterHandle
   offset got fixed and started matching immediately, this meant every
   round's only redirect attempt happened during that dead window. Fixed by
   only marking handled once a write is about to actually happen.
6. **A fix that changes timing can silently reintroduce a different bug.**
   Adding a post-release grace period (to catch shots whose spawn lags
   behind the click) made the "only one steering thread at a time" gate
   block fast follow-up clicks far more often, since the gate wasn't
   released until the whole grace period finished. The fix for the second
   bug required also making the previously-single-token
   `ProjectileTypeOverride` state reference-counted, since the two fixes
   together mean overlapping holds are now a normal occurrence, not a rare
   race.
7. **A mapped, non-zero-looking header entry can still be entirely absent
   from the Address Library for a specific game build.** `RE::TESHitEvent::
   GetEventSource()` compiles cleanly and looks exactly like every other
   `BSTEventSource<T>` usage, but crashes instantly — "Invalid ID: 0" is
   CommonLibSF's own clean abort when it can't resolve the address, not
   the kind of wild-pointer crash a wrong calling convention causes. Same
   underlying lesson as `commonlibsf-unmapped-ids` memory, new instance.

## Persistent memory (separate from this file)

- `starfield-vats-mod-design` — overall design, status, backlog. **The "Open
  problem: making hitscan weapons hit/miss" section is now stale** — update
  it to point here / to `docs/FINDINGS.md` rather than re-reading it as
  still-open.
- `starfield-vats-ui-hook` — render hook + UI overlay specifics.
- `commonlibsf-unmapped-ids` (file: `feedback-commonlibsf-unmapped-ids.md`)
  — the general "mapped ID ≠ safe" pattern, now with a second confirmed
  instance (TESHitEvent) beyond the original four.
- `feedback-commit-every-vats-build` — commit after every deployed build.
- `feedback-model-tier-recommendations` — Sonnet vs Fable 5 guidance.

This file exists as a single self-contained recap for a fresh chat. For the
hitscan/offset work specifically, `docs/FINDINGS.md` in the repo is now the
more polished, public-facing version of the same information — keep them
consistent if either changes.
