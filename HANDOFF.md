# StarfieldVATS — Handoff (2026-08-25, end of session)

Read this first in a new chat. Point-in-time snapshot — verify against actual
code/log before trusting anything here, offsets and "confirmed" claims can go
stale. **If anything below is unclear or seems inconsistent with the code:
check the public repo (https://github.com/alexanderjohnen/StarfieldVATS,
`git log --oneline` + commit bodies for the "why") and also search this
machine's other Claude Code session history/transcripts — this project has
had many prior sessions not fully summarized here, and searching past chats
for a topic (health bar, hit marker, a specific offset, a past crash) often
surfaces detail this file deliberately left out to stay short.**

## What this is

SFSE mod for Starfield: real-time FO76-style VATS. Hotkey locks a target
while the game keeps running; every shot redirects toward the target unless
something physically blocks it (no dice roll anymore, see below); a real
player-fired round gets its trajectory bent in-flight (no camera/weapon
snap). `C:\Dev\StarfieldVATS`, game v1.16.244. Alexander tests in-game;
Claude cannot — always read the SFSE log yourself
(`Documents\My Games\Starfield\SFSE\Logs\StarfieldVATS.log`, truncated fresh
every game launch, not appended) rather than guessing. **Commit after every
deployed build.**

Build/deploy (fails if Starfield is running — check with
`tasklist //FI "IMAGENAME eq Starfield.exe"` first; it has sometimes shown
**two** processes, both need to be gone):
```
cd C:\Dev\StarfieldVATS && powershell -ExecutionPolicy Bypass -File deploy.ps1
```

Local is currently **8 commits ahead of origin/main** (last pushed:
"Move the crosshair-hiding credit to where it belongs"). Ask before pushing —
this project deliberately keeps `docs/hudmenu-decompiled/` (Bethesda's
copyrighted decompiled UI assets) out of git entirely (`.gitignore`), stripped
from history once already; don't re-add it to a commit.

## Confirmed working, this session's big finding

The redirect mechanic itself was root-caused and fixed. At normal projectile
speed (500 m/s), a round's entire flight at typical engagement range takes
about one game frame — there is no frame in which its trajectory can be
rewritten, which is why redirect only ever worked on slow ordnance (rockets)
and never on guns, no matter how the polling/timing was tuned.
**`Settings::lockedProjectileSpeed` (INI `[Combat] fLockedProjectileSpeed`,
default 80 m/s) force-slows the equipped weapon's projectile for the
duration of a lock**, giving real homing time. Confirmed in-game:
"jeder Hit war ein richtiger Hit" across a real fight.

Also this session:
- **Hit-chance roll removed entirely** (Alexander's call) — every shot now
  redirects toward the target unless a real obstruction blocks it; no RNG,
  no distance falloff. `AimAssist.cpp`'s `ComputeChancePercent` is gone.
- **`ProjectileTypeOverride` now engaged for the whole lock**, not per
  trigger-hold — removes the race where a round could leave the barrel
  before the hitscan→real-projectile flip landed (the likely explanation
  for the old automatic-vs-semi-auto asymmetry). `Controller::
  SyncProjectileOverride()` re-checks the equipped weapon every frame while
  Locked and swaps the override if it changed — **fixes a real bug found
  this session**: switching weapons mid-lock left the new weapon un-flipped
  (confirmed from a log: zero redirects for the rest of that hold) until
  VATS was toggled off/on. Confirmed fixed logically; not yet re-tested
  in-game with a fresh weapon switch.
- **Heap corruption fixed**: `ProjectileTracker`'s tracked-rounds map is
  keyed by raw pointer, and Starfield's allocator recycles projectile slots
  aggressively (one address reused 23x in one session) — `HomeProjectile`
  now re-validates formType/shooterHandle before every write, making a
  write into a recycled allocation structurally impossible. This was a
  latent bug the speed override massively amplified (rounds now live ~6x
  longer). Root-caused from an actual crash log, confirmed via commit
  history, not yet independently re-confirmed crash-free over a long
  session.
- **Three regressions were introduced and reverted in the same session**
  (see `docs/CONTRIBUTIONS.md` for the honest accounting) — a hook-
  synchronous `Engage()` call that stalled system-wide input and broke
  firing outright, a pre-launch projectile write that crashed
  (float-where-a-pointer-belongs signature), and a stale `desiredTargetHandle`
  write removed as unjustified risk. All reverted same-session; not
  currently in the code.
- **FavoritesMenu now ends an active lock** — confirmed working by
  Alexander. **DataMenu/StarMap/DialogueMenu/PauseMenu/LoadingMenu** already
  did.
- **Aim point**: replaced the fixed feet+1.2 chest-height offset with
  `WorldBoundProbe::GetAimPoint` (`RE::NiAVObject::worldBound`, a pose-
  current bounding-sphere center, falls back to the old fixed offset if the
  chain fails). Works well in most real testing (tracks a death/collapse
  animation smoothly). **Known bad case, screenshot-confirmed**: a wide/
  crouching attack pose pulls the sphere center toward hip height (the
  sphere has to enclose spread-out limbs too, not just the torso) — a
  follow-up `BoneProbe` searched the skeleton for a named "center of mass"
  bone (COM/Spine/Chest/...) and found nothing useful (only a "Root" node
  sitting at literal feet height). **Deprioritized/parked** per Alexander's
  own real-world testing showing worldBound is good enough most of the
  time — revisit only if the bad pose recurs as an actual practical problem.

## Open, diagnostics deployed but NOT YET TESTED — pick these up first

- **Health bar**: both prior theories for live current health are now
  *disproven with hard evidence* (not just "didn't find it yet"). This
  session's log showed `avStorage.baseValues`' health entry read once at
  full HP and then genuinely never changed across a real fight with
  confirmed hits — almost certainly MAX health, not current (the two are
  equal at full HP, which is exactly when the original cross-check
  happened, so it never actually distinguished the two). `avStorage.
  modifiers` was raw-dumped and parses cleanly at the already-used 24-byte
  stride (20 real entries, all plausible) but genuinely has no health
  entry — not a parsing bug. Live health must live elsewhere entirely.
  `HealthReader::ScanForLiveHealthCandidates` (new this session) blind-
  scans a 0x2000-byte window of the Actor object itself for any float that
  *decreases* while staying within ~2%-105% of the actor's own known max
  HP — same raw-memory-diffing technique that originally found
  ProjectileTracker's real offsets. Deployed, **next in-game test needed**:
  lock a target, deal damage over several hits (not one-shot), check the
  log for `[VATS] health scan:` lines with a real HP-sized candidate.
- **Hit marker**: still doesn't hide (`CombatHudVisibility.cpp`) despite
  the JPEXS-confirmed real container name+root and both dot- and slash-
  notation paths, all failing `IsAvailable()`. Added a baseline sanity
  probe this session: `GetVariable(root, realRootPath)` with **no child
  appended**, mirroring an engine-internal call CommonLibSF's own
  `GameMenuBase.h` uses. If this baseline also fails, the fault is
  upstream of any path guess (wrong root/movie pointer, wrong timing), not
  the path strings; if it succeeds, the root chain is proven good and the
  fault is specifically in reaching a named child. **Next test needed**:
  check the log for `combat-hud: baseline GetVariable(...)` after a lock.
- **"Lock ends when target dies"**: code has existed since 2026-08-22
  (dead-bit check → `ForceOff()` in `Overlay.cpp`'s Locked branch), but
  Alexander reports it does NOT work in practice. Investigated this
  session: the log genuinely could never have distinguished this working
  from not, since `ForceOff()` logs one generic line shared by every
  trigger (dead target, any blocking menu). Added a distinct log line for
  the dead-triggered case, plus continuous log-on-change of the target's
  raw `boolBits` (not just when the dead bit specifically flips) - if a
  visibly-dead target never shows bit `0x800` set, `GameOffsets::
  kActorDeadBit` itself is the suspect (e.g. Starfield's crawling/downed
  state might not be "dead" yet by this bit, and true death - the bit
  actually flipping - might come later or not fire the way assumed).
  **Next test needed**: lock a target, kill it, check whether `[VATS]
  target dead bit set...` ever appears and whether the lock actually ends.

## Backlog: real occlusion via the depth buffer (2026-08-25)

**The problem it solves.** Nothing in this project can currently answer
"is there a wall between the player and that actor". Havok/`bhkWorld`/
`NiPick` have no bindings anywhere in CommonLibSF (grepped, zero hits),
and the one engine LOS function tried, `HasDetectionLOS`, is stubbed out
as a **confirmed** crash cause. So the cone scan happily picks targets
through walls, which is exactly what Alexander hit when auto-advance
hopped to an enemy in the next room. `commandTarget` sidesteps it for the
crosshair case only, and only at interaction range.

**The idea.** We already project a world position to screen coordinates
every frame for the target box (`CameraProject.cpp`). The game's depth
buffer says how far away the *visible* geometry is at any screen pixel.
Compare the two: if the actor's own distance is greater than the depth
sampled at its projected position, something is drawn in front of it, so
it is occluded. One sample answers "is this target visible at all";
several samples across the body answer "which parts are exposed", which
is what a per-body-part hit system would eventually need.

**Why this project specifically should like it.** It happens entirely
inside our own rendering code - the D3D12 present hook we already own
(`UI/D3DHook.cpp`). No struct offsets, no Address Library IDs, none of
the category that has hard-crashed this project twice. That is a
genuinely different risk profile from every other occlusion approach
considered so far.

**Corroboration it is the right mechanism.** Alexander noticed
(screenshot, 2026-08-22) that Starfield's own scanner outline is clipped
exactly at an actor's visible silhouette - occluded parts get no outline
at all. That is depth-testing against the scene, not a physics raycast,
i.e. the game solves this same problem the same way.

**What it would take.** Locating the depth-stencil resource in the D3D12
frame and getting it readable on the CPU (a resolve/copy into a readback
buffer, one frame of latency, which is irrelevant at these timescales),
then converting sampled depth back to view distance through the same
projection constants `CameraProject` already uses. Estimated one to two
sessions. Deliberately NOT started for auto-advance alone - see the
cheaper option below - but it is the right answer the moment occlusion is
needed for more than one feature.

**Cheaper stand-in, not started either.** Restrict auto-advance to actors
whose `currentCombatTarget` is the player (`GameOffsets::kPlayerHandle`,
measured 2026-08-25). Not real occlusion, but strongly correlated - an
actor actively shooting at the player is rarely behind a wall, and one in
the next room who has not noticed them is excluded. Zero new offsets.
Would need to apply to the advance only, never to ordinary acquisition,
or it would make it impossible to open a fight from stealth.

## Discussed, NOT implemented yet

- **VATS resource/energy bar**: Alexander wants a second bar near the
  target box (HUD-only, position left/right/top/bottom of it, whichever
  looks best) showing how much longer VATS can be sustained. Design
  discussion only so far - recommended a project-owned resource (simple
  float in `Controller`, drains per second while Locked, regenerates while
  Off, INI-tunable) over hooking Starfield's real O2 bar, specifically to
  avoid a new native write/offset for a cosmetic feature, and to avoid
  competing with O2's other real uses (sprinting etc.). Not started.
- **Auto-advance to next target in radius X on kill**: explicitly deferred
  by Alexander until the resource/energy system above exists, so hopping
  targets isn't free.

## Persistent memory

`starfield-vats-mod-design`, `starfield-vats-ui-hook`,
`commonlibsf-unmapped-ids`, `feedback-commit-every-vats-build`,
`feedback-model-tier-recommendations` — see those for anything older than
this session not covered above. `docs/CONTRIBUTIONS.md` has the detailed,
honest attribution writeup (including this session's regressions) if asked
about who contributed what.
