# StarfieldVATS — Handoff (2026-08-25)

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
  directions (template vs code, deployed vs template) and warns.

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
- **The box still does not sit reliably centred on the target — pick this
  up first.** Alexander's report after the radius change: better, but
  still off on some characters. Switching the vertical lift from a
  height-multiplier to a fraction of the bounding sphere's *radius*
  (capped by pose) reduced the drift but did not remove it, so the
  remaining error is probably not in the lift at all.

  What has *not* been checked, and should be before anything else is
  adjusted:
  - **Whether the error is vertical or horizontal.** Everything so far has
    assumed vertical. `bound.center`'s x/y are used unmodified, so a
    horizontal offset would have a completely different cause - and
    `BoneProbe` did log small non-zero x/y deltas between the root bone
    and the sphere centre.
  - **Whether the projection is the culprit rather than the aim point.**
    `CameraProject` self-computes a pinhole projection using
    `iCameraFovDegrees`, which must match the game's actual FOV setting.
    If Alexander's in-game FOV is not 90, every box is systematically off
    and no aim-point tuning will ever fix it.
  - **Whether `worldBound` is simply off-centre for some skeletons**, e.g.
    when a weapon or backpack is enclosed in the sphere. That would
    explain per-character variation better than any factor does.

  The diagnostic already in place logs `feetZ / centreZ / centreAboveFeet
  / radius / lift`. Extending it with the projected screen position and
  the actor's x/y would settle direction and cause in one test. Do not
  adjust `fAimPointRadiusFactor` again before that - it has now been
  guessed at three times.

  Tunables meanwhile: `fAimPointRadiusFactor`, `fAimPointMaxLiftFraction`.
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
