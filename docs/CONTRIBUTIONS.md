# Contributions

This project was built as a close, ongoing collaboration between Alexander Johnen and Claude (Anthropic), across many sessions, mainly through the Claude Code agent. This document tries to give an honest, specific breakdown of who contributed what, rather than a blanket "made with AI" disclaimer that flattens the actual split. It draws on the full project history, not just the most recent session.

## Alexander Johnen

### Design and direction

- The core concept: a real-time, Fallout 76-style VATS for Starfield — no time-slow, no menu pause, hit chance shown live, the weapon/camera never visibly snapping onto a target. Every major design call in the project's history was his: the lock-on flow (redesigned twice before settling on a simple Off/Locked toggle), gating activation on the hand scanner being open, ending a lock outright on blocking menus rather than just hiding the overlay, dropping the body-part targeting system to unblock the projectile-redirect approach, UI legibility preferences (thicker elements, a gray-to-white lock transition).
- **Rejected a screen-space "aim precisely at the crosshair" hit-chance model**, pointing to Fallout 4/76 VATS reference screenshots showing a high hit chance despite an imprecise reticle — the project's actual distance-based chance formula follows directly from that correction.
- Backlog/priority calls: assessing Starfield's vanilla ship-combat targeting as not worth building on ("katastrophal gelöst"), scoping a from-scratch ship-VATS idea instead; proposing faction-based color coding for a future skill-gated feature.

### Technical hypotheses and unblocking ideas

- **Proposed simulating a real keypress (`SendInput`) to close the in-game scanner**, instead of the menu-message approach originally tried — implemented, and confirmed reliable across many sessions. When a later bug was briefly (and wrongly) blamed on this mechanism, he corrected that call from memory of its actual track record, redirecting the fix to the real cause instead of discarding working code.
- **Proposed driving target selection off Starfield's own scanner/activation system** instead of a self-computed camera-cone pick — this idea directly led to finding `commandTarget`, a plain data field that gives occlusion-correct targeting for free, without ever needing Havok/physics raycasting.
- **Supplied the published source code of a separate SFSE plugin (Cassiopeia Papyrus Extender)** as a reference at a point where CommonLibSF's own headers had no usable line-of-sight function — that source directly unblocked finding a working `HasDetectionLOS` call.
- Noticed that ship-combat missile lock-on already does native real-time homing, and proposed writing `desiredTargetHandle` on redirected projectiles for the same reason.
- **Found a Nexus Mods weapon with working homing bullets and proposed inspecting its actual data in xEdit** rather than continuing a much riskier disassembly of Starfield's internal ranged-attack functions — the single biggest turning point in solving the hitscan-to-real-projectile problem, and a large amount of risk and effort saved.
- Pointed investigation toward the projectile's `Type` field as a likely factor distinguishing hitscan from real-projectile behavior, ahead of it being empirically confirmed.
- The "the faster I click, the more shots go straight" observation — a non-obvious diagnostic correlation that led directly to finding and fixing a thread-blocking bug in the click-handling logic.
- Persistent, specific skepticism ("that's not always the explanation", "some still go straight") that repeatedly pushed the investigation past plausible-but-incomplete explanations, directly responsible for several further fixes being found rather than an issue being reported as solved prematurely.
- Designing and running the tests that falsified or confirmed hypotheses along the way — e.g., a point-blank-range test that ruled out a distance-based theory and correctly redirected attention to "first shot after locking onto a target" instead.

### Testing and verification

Claude cannot run Starfield. Every confirmed fact in this project — every corrected struct offset, every fixed bug — depended on Alexander running the game, reproducing a scenario, and reporting back precisely what happened, often down to details like exact timing, distance, or which specific shot in a sequence misbehaved. This was frequently the hardest part of the work, not incidental labor: one extended investigation (into an intermittent "does the target overlay show at the right time" report) turned out to depend on a UI lag in Starfield's own developer console that Alexander was reading as ground truth — his repeated, detailed, confident reporting across 100+ test sessions in multiple locations is what eventually surfaced that distinction, and along the way led to building a better on-screen status readout that then caught a real, separate visibility bug.

## Claude (Anthropic)

- The large majority of the C++ implementation: the targeting system, the render/UI overlay hook, the projectile redirect logic, the hitscan-to-real-projectile type override, the various in-game diagnostic probes used to find the offsets documented in [`FINDINGS.md`](FINDINGS.md).
- The in-game reverse-engineering itself: locating and cross-verifying corrected struct offsets, designing the diagnostic dumps and sweeps used to find them, and interpreting the resulting raw memory/log data.
- Root-causing bugs from log data and in-game reports (e.g., a decorative laser-sight beam being redirected instead of the real round; the click-handling thread-blocking bug; a timing gap for delayed shot spawns; a scanner-close race against another mod's hook).
- This documentation.

## How this is tracked

Every commit in this repository's history that Claude authored or co-authored carries a `Co-Authored-By: Claude` trailer — `git log` is the detailed, timestamped record behind the summary above.
