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

## 2026-08-25 (later session): live health, and what it unblocked

**Live current health finally works, and the memory search was never
going to find it.** `ActorValueOwner::GetActorValue` - the same engine
accessor behind the console's `getav` and Papyrus' `Actor.GetValue` - is
the source. `ActorValueProbe.cpp` locates the `ActorValueOwner`
sub-object inside `Actor` by walking the MSVC RTTI class hierarchy
(`Actor+0x070`, read from the base class descriptor's `mdisp`, not
guessed) and dispatches through it, re-verifying the sub-object's own
locator offset on every call. Confirmed in-game tracking a real kill:
345.00 → 218.84 → … → 1.12 → **-13.93** (negative on overkill).

Two things worth carrying forward:
- **The rule "relocated data reads are safe, relocated function calls
  are the crash category" was over-applied.** It was formed against
  `REL::ID` calls, which crashed this project twice. `GetActorValue` is a
  plain virtual call through the object's own vtable - no Address Library
  lookup, so none of the failure mode the rule exists to prevent. Virtual
  dispatch is a *different* category. (Caveat: a vtable slot index deep in
  a large class is itself reverse-engineered, so this is not a blanket
  licence - `ActorValueOwner` is a small interface where slot 01 is
  unambiguous.)
- **Alexander demonstrated the answer on 2026-08-24** by running `getav
  health` before and after a shot (480.00 → 461.90). It was filed as a
  cross-check for the memory search instead of being recognised as the
  solution, and the search ran two more days past it. When a rule rules
  out an approach the user has already shown working, re-examine the rule.

What that unblocked, all confirmed in-game:
- **Health bar** works.
- **Lock ends on target death** - `health <= 0`, replacing a dead-bit
  check that had never once fired. `Actor::boolBits & BOOL_BITS::kDead`
  reads *identically* on a living actor and one lying dead on the floor;
  a raw dump showed the struct layout is fine, so it is the enum values
  that do not match this game (`kSetOnDeath`, 1<<23, is set on living
  actors too). **Do not trust BOOL_BITS values without measuring.**
- **Corpses are no longer targetable** - same root cause, same fix.
- **VATS resource bar** (`VatsResource.cpp`), Alexander's design: full
  player health sets capacity, full player oxygen sets refill rate, and
  the budget is spent per point of damage *dealt* (measured as the
  target's health dropping, so armour is accounted for free). Keyed to
  maximums, not current values, to avoid a death spiral. Entirely
  self-contained; never writes to the player's real stats.
- **Auto-advance to the next enemy on a kill**, paid for out of that
  budget.

**Friendly actors** are filtered by measurement, not by faction. A
companion and an enemy read `boolBits` 0x162021A2 and 0x122021A2 -
differing in exactly one bit, 0x04000000, which the header names
`kPlayerTeammate`. `currentCombatTarget` reads 1 for an engaged enemy
against a player form ID of 0x14, so it holds a *handle* and 1 is the
player's (matching `shooterHandle=1` in this project's projectile logs).
A real faction/relationship check is out of reach: `IsHostileToActor` has
Address Library ID **0**, and reconstructing it means walking the actor's
faction list, the player's, and the faction-reaction records. Neutral
civilians therefore remain targetable.

**Hit/kill/crit markers** hide correctly (`visible`, the AS3 spelling -
every earlier attempt used `_visible`, which is AS2, and that alone was
the whole bug). They were never appearing *during* a lock: what was
visible was the **restore**, firing the instant a kill ended the lock and
un-hiding an animation mid-play. Alexander diagnosed that from the audio
- hearing the crit sound with nothing on screen. Restore is now deferred
until the indicator parks on its "End" frame, with a tunable ceiling
(`iHudRestoreDelayMs`, 2500ms) because the game raises some crit events
several seconds late and suppressing until they stop would mean
suppressing indefinitely.

**Still open / not confirmed:**
- **Auto-advance is PARKED** (`bAutoAdvanceOnKill=0`, Alexander's call).
  The mechanism works; picking a sensible target does not, because this
  project has no line-of-sight test. Three filters were tried and none is
  one: the crosshair activation target is occlusion-correct but only
  reaches interaction range, so it essentially never fired; "engaged with
  the player" still picks through walls, since an enemy shooting at you
  from the next room is still shooting at you; the on-screen projection
  check catches targets outside the view but not ones plainly visible
  through a doorway on another floor. Testing showed enemies behind walls
  and on other floors. Everything around it (resource cost, liveness,
  friendly filtering, wider 60° cone, on-screen check) is in place and
  correct - **re-enabling is one INI line once the depth-buffer occlusion
  below exists.** That is now the blocking dependency for this feature,
  not a nice-to-have.
- `fAimPointHeightFactor` 1.25 - 1.5 sat too high on standing targets;
  1.25 unverified.
- **Untested against non-humanoid creatures entirely.** The aim point is
  a proportional lift specifically so it scales with any body shape, but
  no alien has been fought with this on. `fAimPointHeightFactor=1.0`
  restores the old behaviour without a rebuild.
- Neutral civilians targetable (see above).

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
