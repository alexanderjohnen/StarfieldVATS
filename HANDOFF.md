# StarfieldVATS — Handoff (2026-08-26)

Read this first in a new chat. Point-in-time snapshot — verify against the
actual code and log before trusting anything here; offsets and "confirmed"
claims go stale. **If something here looks inconsistent with the code:**
check the public repo (https://github.com/alexanderjohnen/StarfieldVATS —
`git log --oneline` plus commit bodies, which carry the "why"), and search
this machine's other Claude Code session transcripts. Many prior sessions
are not summarised here, and searching past chats for a topic (health bar,
hit marker, a specific offset, a past crash) often surfaces detail this
file leaves out to stay readable.

## What this is

SFSE mod for Starfield: real-time FO76-style VATS. A hotkey locks a target
while the game keeps running; shots are redirected toward it in flight (no
camera or weapon snap, no dice roll). `C:\Dev\StarfieldVATS`, game
v1.16.244.

**Alexander tests in-game; Claude cannot.** Always read the SFSE log
yourself — `Documents\My Games\Starfield\SFSE\Logs\StarfieldVATS.log`,
truncated fresh every launch, not appended — rather than guessing from a
description.

Build/deploy (fails while Starfield is running — check first, it has
sometimes shown **two** processes and both must be gone):
```
tasklist //FI "IMAGENAME eq Starfield.exe"
cd C:\Dev\StarfieldVATS && powershell -ExecutionPolicy Bypass -File deploy.ps1
```

**Commit after every deployed build.** `docs/hudmenu-decompiled/`
(Bethesda's copyrighted decompiled UI assets) is deliberately gitignored
and was stripped from history once already — never add it to a commit.

## Working conventions that have repeatedly paid off

- **Measure before choosing a number.** Several things in this project were
  guessed at twice and wrong twice (the aim-point factor, the HUD restore
  delay) before a probe settled them in one round. If a constant has been
  adjusted by feel more than once, stop and log the inputs.
- **Pin down *when* before explaining *why*.** Two wrong theories about the
  crit marker both rested on an unexamined assumption about when it
  appeared. Alexander settled it by noticing he could *hear* the crit
  without seeing it.
- **A safety rule is not a universal.** See "risk categories" below.
- **Keep `res/StarfieldVATS.ini` in sync with `Settings.cpp`.** Twelve
  settings once became silently untunable. `deploy.ps1` now checks both
  directions (template vs code, deployed vs template) and warns —
  including, since 2026-08-26, that each key sits under the SECTION its
  `GetPrivateProfile*` call names. A key in the wrong section is read as
  absent and falls back to the code default while looking perfectly
  correct in the file; `bDebugAimMarkers` landed under `[Resource]`
  instead of `[HUD]` and the diagnostic it gates stayed off through two
  test sessions before the log gave it away.
- **Just edit the INI.** When a change needs a setting, write it into both
  the template and the deployed file rather than telling Alexander which
  line to add — he has asked for this explicitly.

## Risk categories, learned the hard way

1. **`REL::ID`-backed engine calls are the crash category.** An ID of `0`
   (unmapped) crashes instantly; a *mapped* ID is still no guarantee. Two
   confirmed hard crashes came from this. `IsHostileToActor` and
   `GetActorKnowledge` are both ID 0 — unusable.
2. **Virtual calls through an object's own vtable are NOT that category.**
   No Address Library lookup is involved. This distinction was missed for
   days and cost the whole live-health search (below). Caveat: a vtable
   *slot index* is itself reverse-engineered, so a deep slot on a large
   class is its own risk — `ActorValueOwner` was safe because it is a tiny
   interface and the sub-object was located by reading RTTI, not guessed.
3. **Struct offsets from CommonLibSF headers cannot be trusted blindly.**
   `TESObjectCELL::references` was off by 8. Verify empirically; wrap in
   `SafeRead` regardless.
4. **Enum *values* cannot be trusted either.** `BOOL_BITS::kDead` is inert
   in this game — it reads identically on a living actor and a corpse.
   `kPlayerTeammate` (1<<26) *was* confirmed, by measuring a companion
   against an enemy. Measure, don't assume.

## Current state — confirmed working in-game

- **Redirect**: hitscan weapons are flipped to real projectiles for the
  duration of a lock and slowed (`fLockedProjectileSpeed`, 80 m/s) so there
  is actually a frame in which to steer them. At stock 500 m/s a round
  crosses a typical engagement in one frame, which is why redirect only
  ever worked on rockets.
- **Live enemy health** via `ActorValueOwner::GetActorValue` — the same
  accessor behind the console's `getav` and Papyrus' `Actor.GetValue`.
  `ActorValueProbe.cpp` finds the sub-object at `Actor+0x070` by walking
  the MSVC RTTI class hierarchy (read from the base class descriptor's
  `mdisp`, not guessed) and re-verifies it on every call.
- **Health bar** on the target box.
- **Lock ends when the target dies** (`health <= 0`; overkill reads
  negative). Replaced a dead-bit check that had never once fired.
- **Corpses are not targetable**, same root cause.
- **Companions are not targetable** (teammate bit, measured).
- **VATS resource bar** — Alexander's design: full player health sets
  capacity, full player oxygen sets refill rate, budget is spent per point
  of damage *dealt* (measured as the target's health dropping, so armour
  counts). Maximums, not current values, to avoid a death spiral. Entirely
  self-contained; never writes to the player's stats.
- **Hit/kill/crit markers hidden** while Locked (`visible`, the AS3
  spelling — `_visible` is AS2 and was the entire earlier "unreachable
  path" saga). Restore is deferred until the indicator parks on its "End"
  frame, capped by `iHudRestoreDelayMs`.
- **Target box scales with distance**, capped at its original size.
  Sized from the bounding sphere's angular size (`ProjectedRadiusPixels`,
  strictly 1/depth) since 2026-08-26 — the earlier method of projecting a
  second point and measuring the pixel gap made the size depend on screen
  position and camera pitch, which showed up as the box growing before it
  shrank when backing away. The old cap at the previous fixed 36px size is
  gone too: it bound from roughly 5-7m inward, freezing the box while the
  target kept growing, which reads as the box shrinking up close. The
  size is now a bounded ramp between two DISTANCES rather than a pixel
  range: `fBoxMaxSizeDistanceMeters` (8.0) and `fBoxMinSizeDistanceMeters`
  (16.0), Alexander's design. Inside 8m it holds the size it has at 8m,
  beyond 16m the size it has at 16m, plain 1/depth in between — so it
  varies by exactly 2:1 and never inverts. Both old fixed pixel bounds
  (36px ceiling, 16px floor) are gone; they meant a different thing on
  every resolution and bit at whatever distance the maths produced. NOT
  yet re-confirmed in-game.

## Open / unverified

- **Auto-advance to the next enemy is PARKED** (`bAutoAdvanceOnKill=0`,
  Alexander's call). The mechanism works; picking a sensible target does
  not, because there is no line-of-sight test. Three filters were tried and
  none is one: the crosshair activation target is occlusion-correct but
  only reaches interaction range, so it essentially never fired; "engaged
  with the player" still picks through walls, since an enemy shooting at
  you from the next room is still shooting at you; the on-screen projection
  check catches targets outside the view but not ones plainly visible
  through a doorway on another floor. Everything around it (resource cost,
  liveness, friendly filtering, 60° cone, on-screen check) is in place and
  correct — **re-enabling is one INI line once the depth-buffer occlusion
  below exists.** That is now this feature's blocking dependency.
- **Box centring — three models tried, none right, and the reason is now
  structural rather than a bad constant. READ THIS BEFORE TOUCHING IT
  AGAIN.** A 17-actor dungeon run on 2026-08-26 refuted the model that had
  just been built on a 3-actor sample:

  - `centre/radius` across 17 actors spans **0.47 to 0.86** (plus one
    corpse at -0.03). The three-actor reading of 0.741/0.778/0.778 that
    the feet+radius anchor was chosen from was simply under-sampled.
  - Consequence: the pose cap now fires on **10 of 17 actors**, so for the
    majority the aim point is `1.5 x centreAboveFeet` — i.e. right back to
    being driven by the sphere centre the change was meant to escape.

  The structural problem, stated plainly so the next attempt does not
  re-derive it: **the radius is pose-blind and the sphere centre is
  character-noisy.** A crouching enemy keeps its radius but drops its
  centre; two standing enemies share a pose but differ in centre. Any
  model built from those two numbers alone must trade one against the
  other, which is what all three attempts have done:

  1. centre x multiplier — pose-correct, amplified character spread.
  2. centre + radius x lift — less spread, still centre-anchored.
  3. feet + radius x factor — character-stable, pose-blind, so the cap has
     to do the pose work and now dominates.

  Some of the spread that was called error may not be: a crouching enemy's
  chest really is lower.

  **The way out is not a fourth factor — it is the skeleton.**
  `BoneProbe` walks the actor's node tree, and its guessed candidate list
  (`COM`/`Spine`/`Chest`/...) had only ever matched `HumanExportRoot`,
  which sits at the feet and is useless as an aim point. **`bonedump`
  settled why on the first run (2026-08-26): "1 nodes visited".** The walk
  never descended at all — so this was never a bone-naming problem, it was
  the children-array offset being rejected on every actor.

  `sizeof(RE::NiAVObject)` is 0x130 and the header static_asserts it, but
  a header assert proves what the header believes, not what the game's
  memory does — risk category 3, the same way `TESObjectCELL::references`
  was off by 8. `BoneProbe` now **probes** for the offset (0x0F0..0x1C0)
  and validates a candidate by BACK-REFERENCE: the first child's
  `NiAVObject::parent` (0x038) must point at the node the array came from.
  That is strong enough to trust without an eye on it. The resolved offset
  is logged once (`bone: children offset FOUND at 0x...`). BUILT, NOT YET
  DEPLOYED — Starfield was running.

  If the walk now descends, a real chest bone is pose-correct and
  creature-correct by construction and would end this problem rather than
  improve it. If the probe reports nothing found, the children array is
  not a plain BSTArray at any offset in that window and the bone route
  needs rethinking.

  Confirmed sound and not worth re-testing: the projection near the screen
  centre (screenshots at 6m and 7m put the `FEET` cross on the boots), and
  the absence of horizontal error (`|sphere.x - feet.x|` averages 0.0068
  normalized over 1004 samples spanning the full screen width). Both pose
  safety paths were confirmed firing on a real ragdoll.

  **Still open: the FOV axis.** Every screenshot so far had the target
  near x=0.5, which is exactly where a wrong axis has zero error by
  construction. One screenshot with the target at the far left or right
  edge settles it. Treat `bCameraFovIsHorizontal` as probable but
  unproven.
- **Never tested against non-humanoid creatures at all.** The aim point is
  proportional specifically so it scales to any body shape, but no alien
  has been fought with it.
- **Neutral civilians remain targetable.** Only companions are filtered. A
  real faction/relationship check is out of reach (`IsHostileToActor` is ID
  0, and reconstructing it means walking the actor's faction list, the
  player's, and the faction-reaction records).
- **Crit markers still occasionally appear seconds after a kill.** The game
  raises those events late; suppressing until they stop would mean
  suppressing indefinitely, which would break the base game's HUD. The
  2500ms ceiling is a deliberate compromise, not a fix.
- Heap-corruption fix in `ProjectileTracker` (re-validating formType and
  shooter before every write) has not been independently re-confirmed over
  a long session.

## Backlog: real occlusion via the depth buffer

**The problem it solves.** Nothing in this project can answer "is there a
wall between the player and that actor". Havok/`bhkWorld`/`NiPick` have no
bindings anywhere in CommonLibSF (grepped, zero hits), and the one engine
LOS function tried, `HasDetectionLOS`, is stubbed out as a **confirmed**
crash cause. This is what blocks auto-advance, and it would also serve
hit-chance and per-body-part targeting later.

**The idea.** A world position is already projected to screen coordinates
every frame for the target box (`CameraProject.cpp`). The game's depth
buffer says how far away the *visible* geometry is at any screen pixel. If
the actor is further away than the depth sampled at its projected
position, something is drawn in front of it. One sample answers "is this
target visible"; several across the body answer "which parts are exposed".

**Why this project should like it.** It lives entirely inside the D3D12
present hook we already own (`UI/D3DHook.cpp`) — no struct offsets, no
Address Library IDs, none of the category that has crashed this project
twice.

**Corroboration.** Alexander noticed that Starfield's own scanner outline
clips exactly at an actor's visible silhouette — occluded parts get no
outline. That is depth-testing, not a physics raycast: the game solves the
same problem the same way.

**What it takes.** Locating the depth-stencil resource in the D3D12 frame
and getting it readable on the CPU (a resolve/copy into a readback buffer;
one frame of latency is irrelevant here), then converting sampled depth
back to view distance using the projection constants `CameraProject`
already has. Estimated one to two sessions.

## Discussed, not implemented

- **Own crit indicator drawn on the VATS box**, instead of suppressing the
  vanilla one. Requires detecting a crit. The engine-side route is closed
  (`BSUIDataManager`, which `HitKillIndicator.as` subscribes to for
  `uHitType === CRITICAL`, does not appear anywhere in CommonLibSF). The
  Scaleform side looks feasible: a crit calls `gotoAndPlay` on the banner,
  and `currentLabel` was confirmed readable. Would need low-rate polling
  from the game thread — never per-frame from the render thread, which is
  the mechanism suspected in an earlier crash.
- **Ship-combat VATS** — see the `starfield-vats-mod-design` memory.

## Persistent memory

`starfield-vats-mod-design`, `starfield-vats-ui-hook`,
`commonlibsf-unmapped-ids`, `reference-starfield-vats-github`,
`feedback-commit-every-vats-build`, `feedback-model-tier-recommendations`.
`docs/CONTRIBUTIONS.md` has the detailed, honest attribution writeup —
including the misjudgments — if asked who contributed what.
