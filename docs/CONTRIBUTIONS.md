# Contributions

This project was built as a close, ongoing collaboration between Alexander Johnen and Claude (Anthropic), across many sessions, mainly through the Claude Code agent. This document tries to give an honest, specific breakdown of who contributed what, rather than a blanket "made with AI" disclaimer that flattens the actual split. It draws on the full project history.

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

Beyond visual preferences (legibility, a gray-to-white lock transition), directly shaped *when* the HUD should behave differently: which menus and transitions should end an active lock outright rather than just hide the overlay (dialogue, the star map, other blocking menus), and, later, catching a case the project had missed entirely: the VATS overlay drawing directly over a native Starfield tutorial popup, a real bug that led to a fix distinguishing "end the lock" from "just don't draw this specific frame."

### Design and direction, throughout

The core concept — a real-time, Fallout 76-style VATS for Starfield, no time-slow, no menu pause — and essentially every subsequent design call: the lock-on flow (redesigned twice before settling on a simple Off/Locked toggle), gating activation on the hand scanner being open, dropping body-part targeting to unblock the projectile-redirect approach. He also **rejected a screen-space "aim precisely at the crosshair" hit-chance model**, pointing to Fallout 4/76 VATS reference screenshots showing a high hit chance despite an imprecise reticle — the project's actual distance-based chance formula follows directly from that correction.

### Diagnosing the hitscan-redirect bugs

Beyond the original idea, drove the debugging of its rollout: the observation that "the faster I click, the more shots go straight" led directly to finding a thread-handling bug in the click logic; repeated, specific skepticism ("that's not always the explanation") pushed several more fixes to actually get found instead of an issue being called solved too early; and a deliberate point-blank-range test correctly ruled out a distance-based theory and redirected attention to the real cause.

### Testing and verification

Claude cannot run Starfield. Every confirmed fact in this project — every corrected struct offset, every fixed bug — depended on Alexander running the game, reproducing a scenario, and reporting back precisely what happened, often down to details like exact timing, distance, or which specific shot in a sequence misbehaved. This was frequently the hardest part of the work, not incidental labor: one extended investigation (into an intermittent "does the target overlay show at the right time" report) turned out to depend on a UI lag in Starfield's own developer console that Alexander was reading as ground truth — his repeated, detailed, confident reporting across 100+ test sessions in multiple locations is what eventually surfaced that distinction, and along the way led to building a better on-screen status readout that then caught a real, separate visibility bug.

## Claude (Anthropic)

### The struct-offset reverse-engineering behind the hitscan fix

Once Alexander's type-override idea set the direction, actually finding and proving the byte offsets it depended on was Claude's work, done through iterative diagnostic builds rather than any documented reference. `RE::Projectile`'s movementDirection/velocity/age fields turned out to be uniformly off by `-0x10` and `BGSProjectile::data`'s base by `+0x8` in CommonLibSF's headers — recognizing that a cluster of "wrong" fields was actually one constant shift, rather than treating each as an independent bad guess, is what turned a long field-by-field hunt into a single correction. A related trap: `AMMO_DATA`'s `static_assert` on total struct size passed even though its first field was actually an unresolved `TESFormID` + padding rather than the `BGSProjectile*` pointer the header claimed — an 8-byte pointer and a 4-byte FormID plus 4 bytes of padding are indistinguishable by size alone, so the assert gave false confidence. Also caught a misread of `FormTypes.h`'s inline comments as decimal when they're actually hex without a `0x` prefix, which had made a genuinely correct pointer chain look broken for a full test cycle before the misreading was found. The `BGSProjectile::data + 0x84` type-byte finding was then cross-verified byte-for-byte against the Nexus Mods weapon Alexander proposed, confirming `flags`/`gravity`/`speed`/`range` all matched its independently-authored xEdit data exactly — see [`FINDINGS.md`](FINDINGS.md) for the full corrected offset chain.

### The hitscan-to-real-projectile implementation

Built `ProjectileFlagProbe.cpp` (the read-only diagnostic that confirmed the type-byte correlation across nine weapons) and `ProjectileTypeOverride.cpp` (the actual write: force the type byte to `0x00` on the equipped weapon's live projectile for the duration of a held trigger, restore it after). Designed this as reference-counted per projectile pointer rather than a single on/off token once overlapping trigger holds turned out to be a normal case, not a rare race — see the click-handling fix below.

### Root-causing bugs from log data and in-game reports

Most confirmed bugs in this project were diagnosed from SFSE log output and Alexander's in-game reports, then fixed and re-tested against his next report, including:

- A decorative laser-sight beam (`kPBEA`/`BeamProjectile`) being redirected instead of the actual fired round — excluded it from `ProjectileTracker`'s candidate scan.
- A thread-handling bug where a post-release grace period (added to catch shots whose spawn lags behind the click) held the single-steering-thread gate open long enough to silently drop fast follow-up clicks — traced from Alexander's observation that faster clicking produced more straight-flying shots, then fixed by releasing the gate as soon as the button lifts rather than after the whole grace period.
- A timing gap where `ProjectileTracker` marked a projectile "handled" as soon as its gating fields matched, before checking that `velocity` had actually become non-zero (it reads zero for roughly the first 10ms after spawn) — meaning the round's only redirect attempt could land inside that dead window. Fixed by only marking a projectile handled once a write is about to actually happen.
- A present-hook race against BetterConsole, where that mod's own swapchain-vtable replacement made `MinHook`'s hook installation fail nondeterministically depending on load order, which then caused a null-ImGui-context crash the first time a shot fired while Locked — fixed with a vtable-patch fallback when `MinHook` fails, and by moving the HUD's display-size lookup off ImGui entirely onto a thread-safe atomic captured at swapchain creation.
- An intermittent "target box shows inverted" report that survived a full telemetry audit with zero exceptions in the code — building an always-on, unconditional status readout directly in the HUD (separate from the target box itself) isolated the actual cause: Starfield's own dev console overlay lags about one line behind its `Log()` calls, and Alexander had been reading that lagging console text, not the box, as ground truth.
- A cell-reference scan capped at 4096 entries that silently dropped a dynamically-added companion actor from targeting in any larger cell — found by ruling out a camera-angle theory first (no actor was within range at all, at any angle, for the whole window), then tracing it to the scan cutoff and raising the cap with a warning against ever silently truncating again.

### The render/UI overlay hook

Ported the D3D12 present-hook chain (IAT hook on `CreateDXGIFactory2` → MinHook on `CreateSwapChainForHwnd` → hook `Present`/`ResizeBuffers`) from the public-domain Starfield-Console-Replacer project, adapting it from a menu-only overlay into an always-on HUD and stripping its input-handling entirely, which surfaced a load-bearing `ImGui_ImplWin32_Init` call that had been co-located with the removed code and needed to be re-homed. Spent roughly ten debugging rounds trying to locate the engine's own camera object in memory to reuse its real view-projection matrix, proved via vtable-identity matching that the method was sound but the object was genuinely unreachable within any practical search range, then abandoned that direction in favor of self-computing a standard pinhole projection from data the project already had reliably — the approach actually shipped.

### Diagnostic tooling and documentation

Designed the in-game diagnostic probes (`ProjectileFlagProbe`, `AimAssistProbe`) and the funnel/telemetry logging used to localize several of the bugs above, interpreted the resulting raw memory dumps and log output, and wrote [`FINDINGS.md`](FINDINGS.md) and this document.

## How this is tracked

Every commit in this repository's history that Claude authored or co-authored carries a `Co-Authored-By: Claude` trailer — `git log` is the detailed, timestamped record behind the summary above.
