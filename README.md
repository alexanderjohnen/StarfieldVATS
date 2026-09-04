# StarfieldVATS

A real-time V.A.T.S. system for Starfield, in the spirit of Fallout 76's real-time targeting — no time-slow, no menu pause. A hotkey locks onto a target while the game keeps running, and the fired shot is steered toward it in flight, without ever visibly snapping the camera or weapon onto the target.

> **Work in progress — not ready for regular play.** This is an active reverse-engineering/development project, not a finished mod. Expect rough edges, unfinished features, and behavior that changes between builds; see [Status](#status) below for specifics before installing it to actually play with.

> **This project was built collaboratively with Claude (Anthropic), primarily through the Claude Code agent.** The large majority of the code, the in-game reverse-engineering (struct offsets, engine behavior), and this documentation were written by Claude, working from Alexander Johnen's direction, hypotheses, design decisions, and in-game testing — Claude cannot run Starfield itself. See [`docs/CONTRIBUTIONS.md`](docs/CONTRIBUTIONS.md) for a concrete breakdown of who did what. Every commit in this repository's history that Claude authored or co-authored carries a `Co-Authored-By: Claude` trailer, so the split is also traceable directly in `git log`.

## What it does

- A single hotkey locks onto whatever's under the crosshair while the in-game hand scanner is open, and tracks that actor's position afterward regardless of camera direction. Corpses and your own companions are skipped; the lock ends by itself when the target dies.
- A shot's trajectory is bent in flight toward the target. For weapons with real, simulated projectiles this redirects the live projectile object; for ordinarily-hitscan weapons it flips a data flag that makes Starfield spawn a real, in-flight projectile in the first place (see [`docs/FINDINGS.md`](docs/FINDINGS.md) for how that was found), which the same logic then steers. The projectile is also slowed for the duration of a lock — at stock speed a round crosses a typical engagement in a single frame, leaving no frame in which to steer it.
- The weapon and camera never visibly snap onto the target — only the round's own flight path changes.
- A HUD overlay marks the locked target with its current health, sized to the target's distance, and hides Starfield's own hit/kill/crit markers while a lock is active.
- Using VATS costs a resource that scales with your character: maximum health sets how much damage you can deal through it before it runs dry, and maximum oxygen sets how quickly it refills once VATS is off.
- The same key supports a **companion**: aim it at one through the scanner and a support session opens instead of a combat lock. A tap heals them, gets them back up when they are down, or — at full health — spends a buff item to give them a temporary damage-resistance shield on a timer. Healing and shielding consume the matching aid item from your inventory.
- With the scanner raised, a line under the ring reports each companion's health, and the remaining shield time when one is running.

There is **no hit-chance roll**. An earlier version rolled dice against a displayed percentage; that was removed in favour of every shot being redirected, with real geometry the only thing that stops one.

## Status

**Work in progress.** Core targeting, the hitscan-to-real-projectile redirect, live target health and the resource system work for what has been tested so far, but this is not a polished, install-and-forget mod. Known gaps as of this writing:

- **The aim point is derived from the target's bounding sphere, not its skeleton.** It has been measured against humans, a mantid, a ground-hugging hopper and a flying alien, and tracks pose and body shape well enough on all of them; a per-character spread of roughly 20cm remains on humanoids, most likely from carried equipment shifting the sphere's centre. `fAimPointCentreFactor` in the INI trades aim height against that spread.
- **No line-of-sight test exists.** Nothing here can tell whether a wall is between you and a target. This is why automatic advancing to the next enemy after a kill is shipped disabled — it could not reliably avoid picking targets behind walls or on other floors. The intended fix is a depth-buffer visibility check; see `HANDOFF.md`.
- Only your companions are excluded from targeting, not neutral civilians. Starfield's own faction/relationship answer sits behind an unmapped engine ID.
- Starfield's crit indicator can still flash briefly a few seconds after a kill. The game raises those events late, and suppressing them indefinitely would break the base game's HUD.
- There's no in-game confirmation that a redirected shot deals damage exactly where intended, only that its trajectory was rewritten and that targets die — see [`docs/FINDINGS.md`](docs/FINDINGS.md) for the attempt at real hit confirmation, blocked on an unmapped engine ID.
- Offsets in [`docs/FINDINGS.md`](docs/FINDINGS.md) are tied to Starfield v1.16.244.0 and can drift with future game patches.

See [`docs/FINDINGS.md`](docs/FINDINGS.md) for the technical detail behind what's confirmed working.

## Documentation

- [`docs/FINDINGS.md`](docs/FINDINGS.md) — verified CommonLibSF struct-offset corrections, crash-causing gaps, and dead ends found while building this, cross-checked in-game and (where possible) against independently-authored reference data. Hard facts only, clearly separated from anything still unconfirmed.
- [`docs/CONTRIBUTIONS.md`](docs/CONTRIBUTIONS.md) — who contributed what, human and AI.

## Installing

Grab the newest archive from [Releases](https://github.com/alexanderjohnen/StarfieldVATS/releases) and extract it into your Starfield folder, so that the files land in `Data/SFSE/Plugins/`. The archive already has that structure, so extracting it over the game folder puts everything in place.

You need [SFSE](https://www.nexusmods.com/starfield/mods/106) installed, and the game version the release names — this mod reads engine structures at fixed addresses, so a different game version can behave unpredictably.

Settings live in `Data/SFSE/Plugins/StarfieldVATS.ini`, which is commented throughout. The log is written to `Documents/My Games/Starfield/SFSE/Logs/StarfieldVATS.log` and is replaced on every launch; raise `iLogLevel` there if you need to report a problem.

To remove it, delete `StarfieldVATS.dll`, `.pdb` and `.ini` from `Data/SFSE/Plugins/`. Nothing is written to your save.

## Requirements & building

- [XMake](https://xmake.io) 3.0.0+
- C++23 compiler (MSVC, Clang-CL)

```bat
git clone --recurse-submodules https://github.com/alexanderjohnen/StarfieldVATS
cd StarfieldVATS
xmake build
```

Set `XSE_SF_MODS_PATH` (a mod manager's mods folder) or `XSE_SF_GAME_PATH` (a Starfield install folder) to redirect the build output there directly. `deploy.ps1` builds and copies the plugin DLL/PDB/INI to a configured game install in one step.

Built on [CommonLibSF](https://github.com/libxse/commonlibsf) via the [commonlibsf-template](https://github.com/libxse/commonlibsf-template).

## Disclaimer

Unofficial fan project. Not affiliated with or endorsed by Bethesda Game Studios or Bethesda Softworks. Starfield is a trademark of Bethesda Softworks LLC.

## License

GPL-3.0 — see [`LICENSE`](LICENSE). This follows from CommonLibSF itself being GPL-3.0 licensed; as a derivative/combined work, this project is bound by the same terms.

GPL-3.0 permits commercial redistribution as long as source stays available under the same license — it does not, and cannot, prohibit that outright. That said: this project exists as a hobby/community effort, and the intent is for it to stay that way. Please don't sell it, or a build based on it, to other players.
