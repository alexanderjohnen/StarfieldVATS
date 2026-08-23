# Contributions

This project was built as a close, ongoing collaboration between Alexander Johnen and Claude (Anthropic), across many sessions, mainly through the Claude Code agent. This document tries to give an honest, specific breakdown of who contributed what, rather than a blanket "made with AI" disclaimer that flattens the actual split. It draws on the full project history, not just the most recent session.

## Alexander Johnen

### The hitscan-to-real-projectile idea

The single biggest turning point in the project. Most Starfield weapons resolve instantly via hitscan — there's no in-flight object to redirect the way a rocket or grenade has. The project had already sunk significant, high-risk effort into the alternative (disassembling Starfield's internal ranged-attack and native aim-assist functions, with real crashes along the way and no working result) when Alexander asked a different question: rather than bending an instant hitscan raycast, what if a weapon's ammo could simply be switched to a real, simulated projectile at runtime while a target is Locked? That reframed an apparently unsolvable problem into one the project had already solved — redirecting a real, in-flight `RE::Projectile`, which the existing tracking/redirect logic already did for rockets and grenades. He then independently confirmed the idea was viable by finding a Nexus Mods weapon with working homing bullets and proposing to inspect its actual data in xEdit, which both validated the approach and supplied the exact reference values used to verify the implementation (see [`FINDINGS.md`](FINDINGS.md)). This one idea is very likely what saved the project from a much longer and much riskier disassembly effort.

### The scanner-close keypress

Proposed simulating a real keypress (Win32 `SendInput`) to close the in-game hand scanner the moment a target locks, instead of either alternative actually on the table: hiding the scanner's menu directly (which yanks it away with no transition at all — no animation, no sound, an obviously artificial cut), or reaching into Starfield's own internal button-press/input-device machinery to replay a "real" press natively (unmapped in CommonLibSF, in the same high-risk category as the engine calls that have caused this project's crashes). A simulated keypress is indistinguishable from a genuine one to a single-player game with no anti-cheat, so it gets the scanner's actual close animation and sound for free, without touching a single risky internal function. Without this, the project would have had to choose between a worse-feeling UI transition and a meaningfully more dangerous implementation.

### Routing targeting through the scanner

Identified the specific reason to select targets through Starfield's own scanner/activation system rather than a self-computed camera-cone "nearest actor" pick: it gives pixel-accurate targeting under the actual cursor, and — because the same system already has to decide "can the player actually see and reach this" for the vanilla "hold E to interact" prompt — it hands the project real, occlusion-correct line-of-sight for free. This is what led to finding `commandTarget`, and meant the project never had to build (or risk crashing on) its own physics-based line-of-sight raycast.

### Flagging that Starfield Engine Fixes already solved a blocking concern

Pointed out that Starfield Engine Fixes (already a dependency of this project, used for its crash logging) also patches the game to allow firing a weapon while the scanner UI is open — resolving an open concern about whether scanner-based targeting would even be compatible with actually being able to shoot afterward, without the project needing to work around it itself.

### UI/overlay behavior

Beyond visual preferences (legibility, a gray-to-white lock transition), directly shaped *when* the HUD should behave differently: which menus and transitions should end an active lock outright rather than just hide the overlay (dialogue, the star map, other blocking menus), and — during the most recent session — catching a case the project had missed entirely: the VATS overlay drawing directly over a native Starfield tutorial popup, a real bug that led to a fix distinguishing "end the lock" from "just don't draw this specific frame."

### Design and direction, throughout

The core concept — a real-time, Fallout 76-style VATS for Starfield, no time-slow, no menu pause — and essentially every subsequent design call: the lock-on flow (redesigned twice before settling on a simple Off/Locked toggle), gating activation on the hand scanner being open, dropping body-part targeting to unblock the projectile-redirect approach. He also **rejected a screen-space "aim precisely at the crosshair" hit-chance model**, pointing to Fallout 4/76 VATS reference screenshots showing a high hit chance despite an imprecise reticle — the project's actual distance-based chance formula follows directly from that correction.

### Diagnosing today's hitscan-redirect bugs

Beyond the original idea, drove the debugging of its rollout: the observation that "the faster I click, the more shots go straight" led directly to finding a thread-handling bug in the click logic; repeated, specific skepticism ("that's not always the explanation") pushed several more fixes to actually get found instead of an issue being called solved too early; and a deliberate point-blank-range test correctly ruled out a distance-based theory and redirected attention to the real cause.

### Testing and verification

Claude cannot run Starfield. Every confirmed fact in this project — every corrected struct offset, every fixed bug — depended on Alexander running the game, reproducing a scenario, and reporting back precisely what happened, often down to details like exact timing, distance, or which specific shot in a sequence misbehaved. This was frequently the hardest part of the work, not incidental labor: one extended investigation (into an intermittent "does the target overlay show at the right time" report) turned out to depend on a UI lag in Starfield's own developer console that Alexander was reading as ground truth — his repeated, detailed, confident reporting across 100+ test sessions in multiple locations is what eventually surfaced that distinction, and along the way led to building a better on-screen status readout that then caught a real, separate visibility bug.

## Claude (Anthropic)

- The large majority of the C++ implementation: the targeting system, the render/UI overlay hook, the projectile redirect logic, the hitscan-to-real-projectile type override, the various in-game diagnostic probes used to find the offsets documented in [`FINDINGS.md`](FINDINGS.md).
- The in-game reverse-engineering itself: locating and cross-verifying corrected struct offsets, designing the diagnostic dumps and sweeps used to find them, and interpreting the resulting raw memory/log data.
- Root-causing bugs from log data and in-game reports (e.g., a decorative laser-sight beam being redirected instead of the real round; the click-handling thread-blocking bug; a timing gap for delayed shot spawns; a scanner-close race against another mod's hook).
- This documentation.

## How this is tracked

Every commit in this repository's history that Claude authored or co-authored carries a `Co-Authored-By: Claude` trailer — `git log` is the detailed, timestamped record behind the summary above.
