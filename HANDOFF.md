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
- **Target marker scales with distance**, bounded between the size it has
  at `fBoxMaxSizeDistanceMeters` (8m) and at `fBoxMinSizeDistanceMeters`
  (16m).
- **HUD is a scanner-style ring** (2026-08-26, Alexander's design,
  UNTESTED in-game): a thin circle instead of FO4 corner brackets, the
  distance set beside it on a tick the way the scanner sets its range, and
  no "TARGET" caption. The two gauges are arcs on that ring instead of
  stacked bars - target health outside the bottom half, the player's VATS
  budget inside the top half. Mine inside, theirs outside, which is what
  keeps them apart now that neither carries a label.
  `bHealthBarBelowBox`/`bResourceBarBelowBox` are gone with the stacked
  layout that needed them.
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
- **The centre-height model amplifies the per-character spread, measured
  and confirmed in play 2026-08-26 — this is the cost that was accepted
  when it was chosen, now visible.** Two male pirates, same room, same
  pose, three seconds apart:

  | actor | radius | centreAboveFeet | aim |
  |---|---|---|---|
  | 0x0017E688 | 1.114 | 0.898 | 1.347 |
  | 0x0017E687 | 1.103 | 0.768 | 1.152 |

  Their radii differ by **1%** while their sphere-centre heights differ by
  **17%**, and the 1.5x factor turns a 13cm difference into **19.5cm** on
  the aim point. Alexander spotted it from screenshots before the numbers
  were looked at.

  What that isolates: the variance is not body size (radius says they are
  the same size) and not pose (both standing). The most likely remaining
  cause is EQUIPMENT inside the bounding sphere — a rifle carried low
  drags the centre down, a backpack or raised weapon pushes it up — which
  no factor on that centre can ever remove.

  This is the fourth time the same wall has been hit from a different
  side, and every time the answer has been the same: the bounding sphere
  does not know where a chest is. Do NOT build a fifth model. The options
  are (a) accept ~20cm on humans, (b) lower `fAimPointCentreFactor` to
  trade aim height for less spread (1.3 gives ~11cm and a slightly lower
  aim), or (c) the skeleton attempt above.

- **Aim point: settled on the sphere's CENTRE HEIGHT, plus smoothing
  (2026-08-26). Built, NOT yet deployed — Starfield was running.** The
  first non-humanoid tests decided it, after three models built on
  humanoids alone. Measured:

  | creature | radius | centreAboveFeet | centre/radius |
  |---|---|---|---|
  | human pirate | 1.03 | 0.84 | 0.81 |
  | mantid (spidery) | 2.96 | 1.40 | 0.47 |
  | hopper (ground-hugging) | 2.16 | 0.30 | 0.14 |
  | flyer | 3.09–4.50 | 0.14–2.18 | — |

  **The radius measures longest extent, not height.** A hopper reads a
  radius of 2.16 with its body 30cm off the ground, so the feet+radius
  model wanted to aim 2.22m up. All four non-humanoids hit the pose cap —
  i.e. the cap, not the model, was producing the aim point for the entire
  creature class. So the cap became the model:
  `aim = feet + fAimPointCentreFactor (1.5) x centreAboveFeet`, floor for
  collapsed bodies, nothing else. That lands at ~70% of body height on a
  human (the chest, up from the 64-68% measured before), 0.46m on a
  hopper, 2.10m on a mantid, and it tracks pose for free.

  This is the 2026-08-25 model returning, which was dropped for
  "amplifying the spread between characters". With creatures in the sample
  that objection is wrong: most of that spread is real, and 3cm between
  two pirates is a cheap price for not aiming two metres over an alien.

  **Smoothing (`fAimPointSmoothingSeconds`, 0.35) is new and was the
  missing piece under all three models.** The bounding sphere moves with
  the animation on everything — a guard's centre swings 0.09, the
  ground-huggers 0.32 — and on the flying alien it swings **2.04m in time
  with the wingbeat** (5938 samples), which the box followed. Only the
  offset from the actor's root is filtered, so a moving target still
  tracks with zero lag. State lives in a fixed 8-slot array under a mutex:
  `GetAimPoint` runs on both the render thread and AimAssist's steering
  thread, and an unguarded shared map there is the exact shape of the heap
  corruption this project already lost a day to.

  Also measured, and NOT yet acted on: **the horizontal offset theory is
  dead for humanoids but alive for sprawling creatures.**
  `|sphere.x - feet.x|` is 0.0013–0.0068 normalized on humans and
  **0.035** on the ground-huggers — their bounding sphere genuinely does
  not sit above their root. Which of the two is the better anchor for such
  a body is untested. Smoothing now filters x/y as well, so at least it
  no longer wanders.

  **Still open: the FOV axis.** Every screenshot so far had the target
  near x=0.5, where a wrong axis has zero error by construction. Both
  creature screenshots were third-person and the box looked left of the
  target — worth a first-person comparison shot, since a third-person
  shoulder offset applied outside `cameraRoot` would look exactly like
  that. One screenshot with the target at a screen edge settles the axis.

- **The skeleton route: the one remaining attempt is BUILT, not yet
  deployed (Starfield running). Run it, then decide.**
  The idea: read a real chest bone instead of deriving an aim point from
  the bounding sphere, which would be pose-correct and creature-correct by
  construction and would also serve per-body-part targeting later. Two
  findings, both 2026-08-26:

  - `bonedump` proved the tree walk never descended: **"1 nodes visited"**
    on every actor. So the years of only ever matching `HumanExportRoot`
    were never a bone-NAMING problem.
  - The children-offset probe (0xF0..0x1C0, validated by back-reference)
    then reported **not found**, on a human and a mantid alike.

  What keeps it from being fully closed: `worldBound` at 0x100 from that
  same pointer reads sane radii on every actor, so the header's
  NiAVObject layout is right up to at least 0x110 — which argues 0x130
  should have worked, and points at the *validator* rather than the
  window. It requires the first child's `parent` at 0x038 to point back,
  and 0x038 is itself a header claim.

  **Now built:** the probe validates by reading the first child's NAME
  (`ReadNodeName` is proven on these objects — it produced
  `HumanExportRoot` and `MantidA_mrRigRoot`) instead of following its
  parent pointer, logs every shape-plausible candidate with that name
  rather than auto-accepting one, and searches 0x0F0..0x220. Watch for
  `bone: children candidate at 0x... - N entries, first named '...'`. If
  nothing in that range yields a readable name, **stop** — the skeleton is
  not reachable this way, and centre-height plus smoothing is the answer.

  Note also that a named chest bone was never the right target anyway:
  the rig roots read `HumanExportRoot`, `MantidA_mrRigRoot`,
  `HopperA_mrRigRoot` — every creature family has its own naming. The
  naming-free version is the CENTROID of all bones, which is also far more
  animation-stable than a bounding sphere (a wingbeat moves a few joints
  of fifty and barely moves the mean, while it tears the sphere open by
  two metres).

- **Neutral civilians remain targetable.**
 Only companions are filtered. A
  real faction/relationship check is out of reach (`IsHostileToActor` is ID
  0, and reconstructing it means walking the actor's faction list, the
  player's, and the faction-reaction records).
- **Diagnostics need one INI switch before release.** Several log
  unconditionally (`worldBound`, `BoneProbe`, health). A shipping mod
  should be quiet, but "turn logging off" must mean flipping a setting,
  not deleting or commenting out code - these diagnostics decided nearly
  every question this week, and log volume has affected timing here
  before, so a release build can behave differently from a test run and
  you need to be able to switch them back on without a rebuild. Half an
  hour of work, not a feature. (Alexander's point, 2026-08-26: the log
  size itself is a non-issue - the file is truncated fresh every launch.)
- **Crit markers still occasionally appear seconds after a kill.**
 The game
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
- **Per-body-part targeting — DECIDED AGAINST, 2026-08-26. Do not propose
  it again without new evidence.** Alexander's reasoning, and he has the
  strongest kind: a three-slot version was built and played, and it was
  not fun - nowhere near FO76's feel. On top of that, Starfield has none
  of the mechanics that make body parts mean anything: no crippling, no
  dismemberment, no limb reaction animations. A body part is a damage
  multiplier and nothing else, so it is head or not-head. And creature
  rigs differ completely from each other (`HumanExportRoot` vs
  `MantidA_mrRigRoot` vs `HopperA_mrRigRoot`), so it could never have been
  general.

  The one case worth keeping from it - shooting an enemy who is only
  partly behind cover - **is the occlusion feature, not this one**. A
  depth-buffer test answers "which point on this target is exposed", so
  the aim point can move onto the visible part silently: no slots, no UI,
  no decision for the player to make, and nothing new to balance.

  Claude proposed this feature twice from the FO76 framing without first
  checking whether Starfield has the mechanics underneath it. That is the
  same class of mistake as the three aim-point models: reasoning from a
  model instead of measuring the thing.
- **Ship-combat VATS** — see the `starfield-vats-mod-design` memory.


## Persistent memory

`starfield-vats-mod-design`, `starfield-vats-ui-hook`,
`commonlibsf-unmapped-ids`, `reference-starfield-vats-github`,
`feedback-commit-every-vats-build`, `feedback-model-tier-recommendations`.
`docs/CONTRIBUTIONS.md` has the detailed, honest attribution writeup —
including the misjudgments — if asked who contributed what.
