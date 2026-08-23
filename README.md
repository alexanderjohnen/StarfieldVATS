# StarfieldVATS

A real-time V.A.T.S. system for Starfield, in the spirit of Fallout 76's real-time targeting — no time-slow, no menu pause. A hotkey locks onto a target while the game keeps running, shows a live hit-chance readout, and steers the fired shot to land (or deliberately miss) according to that rolled chance, without ever visibly snapping the camera or weapon onto the target.

> **This project was built collaboratively with Claude (Anthropic), primarily through the Claude Code agent.** The large majority of the code, the in-game reverse-engineering (struct offsets, engine behavior), and this documentation were written by Claude, working from Alexander Johnen's direction, hypotheses, design decisions, and in-game testing — Claude cannot run Starfield itself. See [`docs/CONTRIBUTIONS.md`](docs/CONTRIBUTIONS.md) for a concrete breakdown of who did what. Every commit in this repository's history that Claude authored or co-authored carries a `Co-Authored-By: Claude` trailer, so the split is also traceable directly in `git log`.

## What it does

- A single hotkey locks onto whatever's under the crosshair while the in-game hand scanner is open, and tracks that actor's position afterward regardless of camera direction.
- Hit chance is shown live, based on distance and line-of-sight to the target — the same "high chance despite an imprecise reticle" feel as Fallout 4/76 VATS, not a screen-space aiming check.
- A shot's actual trajectory is bent in flight to land per the rolled chance — for weapons with real, simulated projectiles this redirects the live projectile object; for ordinarily-hitscan weapons this flips a data flag that makes Starfield spawn a real, in-flight projectile in the first place (see [`docs/FINDINGS.md`](docs/FINDINGS.md) for how that was found), which the same redirect logic then steers.
- The weapon and camera never visibly snap onto the target — only the round's own flight path changes.

## Status

Actively in development. Hitscan-to-real-projectile conversion and redirect are working for the weapons tested so far; a few known gaps remain (skeletal/pose-aware aim point, some concurrency-timing edge cases). See [`docs/FINDINGS.md`](docs/FINDINGS.md) for the technical detail behind what's confirmed working, and open issues for what's still being chased.

## Documentation

- [`docs/FINDINGS.md`](docs/FINDINGS.md) — verified CommonLibSF struct-offset corrections, crash-causing gaps, and dead ends found while building this, cross-checked in-game and (where possible) against independently-authored reference data. Hard facts only, clearly separated from anything still unconfirmed.
- [`docs/CONTRIBUTIONS.md`](docs/CONTRIBUTIONS.md) — who contributed what, human and AI.

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
