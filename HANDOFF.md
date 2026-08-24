# StarfieldVATS — Handoff (2026-08-24, end of session)

Read this first in a new chat. Point-in-time snapshot — verify against actual
code/log before trusting anything here. Full blow-by-blow history (a *lot*
happened today, including 3 hard crashes) is in git log, not repeated here —
`git log --oneline` and read commit bodies if you need the "why" behind
something. Public repo: https://github.com/alexanderjohnen/StarfieldVATS.

## What this is

SFSE mod for Starfield: real-time FO76-style VATS. Hotkey locks a target
while the game keeps running; a hit-chance roll decides hit/miss; a real
player-fired round gets redirected in-flight toward the target (no
camera/weapon snap). `C:\Dev\StarfieldVATS`, game v1.16.244. Alexander tests
in-game; Claude cannot — always read the SFSE log yourself
(`Documents\My Games\Starfield\SFSE\Logs\StarfieldVATS.log`) rather than
guessing. **Commit after every deployed build.**

Build/deploy (fails if Starfield is running):
```
cd C:\Dev\StarfieldVATS && powershell -ExecutionPolicy Bypass -File deploy.ps1
```

## Confirmed working

- **Real-time lock** (hotkey while hand scanner open), hit-chance roll,
  projectile redirect for the core "curve a real bullet toward the target"
  mechanic.
- **ADS ends the lock** (`AdsBlocker.cpp`) — detects the ADS mouse button via
  a `WH_MOUSE_LL` hook, calls `Controller::ForceOff()`. Confirmed in-game.
- **Damage numbers hidden while Locked** (`DamageNumbersVisibility.cpp`) —
  toggles Starfield's real Interface setting `bDamageNumbersEnabled` (found
  via a real `StarfieldPrefs.ini` sample). Confirmed working. Far safer than
  fighting Scaleform for the popups, which turned out to be structurally
  unreachable anyway (see below).
- Crosshair hiding while Locked (`CrosshairVisibility.cpp`) — pre-existing,
  unchanged.

## Open: hit marker / kill marker not hiding

`CombatHudVisibility.cpp` — one-shot toggle at Lock (like the two above),
targeting the real container instance name **`HitAndKillIndicator_mc`**
(confirmed 2026-08-24 by Alexander directly in JPEXS's timeline view for
`hudmenu.gfx` frame 1 — `PlaceObject2 chid:275 dpt:127 nm:
"HitAndKillIndicator_mc"`, placed directly on `root1`, no nesting). Despite
using the confirmed name, it still didn't resolve in the last test
(`combat-hud: none of the candidate paths resolved`). A safe diagnostic was
just added (`IsAvailable()` on the bare container path, logged) — **next
step: read the log after a fresh test, see whether `container available=`
logs true or false** to know if the break is the container itself or its
children (`HitIndicator_mc`/`KillIndicator_mc`).

Floating damage numbers (not the marker) are a *separate*, permanently dead
end: `docs/hudmenu-decompiled/scripts/HitDamageIndicator.as`'s
`SpawnNewClip()` does `addChild()` with no instance name — AS3
auto-generates a different name every single time, so no static path could
ever reach them. Don't retry that one.

**Do NOT make this per-frame again** (was tried, reverted) — a crash report
that session showed an unrelated engine job thread faulting inside
Scaleform's own AS3 VM; not proven to be this project's fault, but going
from one Scaleform touch per lock to ~60/sec is the one thing that changed
that session, so it was pulled back. Stay one-shot.

## Open: native health bar — blocked, needs better tooling

Goal: make Starfield's *own* enemy health bar show on the VATS target
(ours never worked and was deleted — `HealthReader.cpp`/`HealthWidgetReader.cpp`
are gone).

- **Reading the health value directly: proven impossible**, not just hard.
  `docs/hudmenu-decompiled/scripts/EnemyHealthMeter.as` shows the value
  only ever arrives as a parameter to a native push-event callback
  (`BSUIDataManager.Subscribe("HudEnemyData", ...)`) — nothing is ever
  stored at a readable path. Don't re-attempt Scaleform probing for this;
  it also caused 2 of today's 3 crashes (a bare-clip destructor crash, then
  a full PC freeze from named-property probing) before being proven moot.
- **Forcing the native trigger** (`RE::Actor::currentCombatTarget`,
  `Actor.h` offset 0x298 — confirmed correlated with the health bar's
  real trigger condition via a removed read-only probe): tried 3 ways
  (one-shot write, per-render-frame rewrite, 5ms background-thread
  rewrite) — **all failed**. The engine recomputes this field at least
  once per frame from a live aim-assist/target-detection computation, not
  a value a mod can just hold in place. `CombatTargetOverride.cpp`'s
  `Engage()`/`Disengage()` (one write per lock) are still wired in,
  harmless, but don't fix anything on their own.
- **Real fix would need**: finding and hooking whatever native function
  computes "is X under my aim-assist cone" — likely the same
  `RangedAimAssistImpl`/`IAimAssistImpl` function already identified (RTTI
  name only, no header, no known address) during the bullet-bending
  investigation (`AimAssistProbe.cpp`). This needs a real disassembler
  (IDA/Ghidra) or a published mod that's already solved it — neither
  available right now. **Don't attempt more blind field-write guessing.**
  If Alexander gets a disassembler or finds a reference mod, this is where
  to resume.

## Open: shots still not landing reliably — mid-investigation

Fixed today: each redirected round used to be aimed **once** and left alone
for the rest of its flight. `ProjectileTracker.cpp` now homes it
continuously (re-aims every poll tick toward the target's current position,
up to 1.5s) via a new `TrackedState` map instead of a one-shot set — deployed,
not yet confirmed to have fixed anything.

**New finding, NOT YET ACTED ON**: Alexander reports shots going
"perfectly straight, as if never touched" with a scoped/semi-auto weapon,
across multiple targets — logged as `HIT` and `redirect: HIT` every time,
but visually unaffected. Leading theory: `ProjectileTypeOverride::Engage()`
(the hitscan→real-projectile flip that makes redirect possible at all) only
runs inside `AimAssist.cpp`'s `SteeringLoop`, which only starts once a **new
OS thread** spawned from the mouse-hook callback gets scheduled — for a
single/semi-auto shot, the game's own native hitscan resolution may well
finish before that thread even starts, so the flip lands too late to affect
that shot at all. Automatic weapons wouldn't show this (later rounds in the
burst fire after the override is already active), which would explain why
earlier sessions looked fine.

**Candidate fix, not implemented**: call `ProjectileTypeOverride::Engage()`
synchronously inside the `WM_LBUTTONDOWN` handler itself (`AimAssist.cpp`'s
`HookProc`), before spawning the thread, to close the race window. Must NOT
call `REX::INFO` (blocking file I/O) from inside the low-level hook
callback — this project already hit that exact problem once
(`BackKeyInterceptor.cpp`'s comment explains why) and had to defer logging
to a detached thread. So: move the SafeRead/SafeWrite part of `Engage()`
into the hook synchronously, keep the logging on `SteeringLoop`'s own
thread by passing the resulting `Token` into it instead of having
`SteeringLoop` call `Engage()` itself.

Also confirmed (again) this session: `BGSProjectileData`'s `hitScan` flag
bit reads `false` on literally every sample ever seen, including
known-real projectiles — it's dead/unreliable, ignore it. The `type` byte
(`+0x84`, 0x02=hitscan/0x00=real, "9 samples zero exceptions") is still the
only trusted signal.

## Persistent memory

`starfield-vats-mod-design`, `starfield-vats-ui-hook`,
`commonlibsf-unmapped-ids`, `feedback-commit-every-vats-build` — see those
for anything older than today not covered above.
