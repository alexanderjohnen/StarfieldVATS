# Contributions

This project was built as a close, ongoing collaboration between Alexander Johnen and Claude (Anthropic), across many sessions, mainly through the Claude Code agent. This document tries to give an honest, specific breakdown of who contributed what, rather than a blanket "made with AI" disclaimer that flattens the actual split. It draws on the full project history.

## Alexander Johnen

### The hitscan-to-real-projectile idea

The single biggest turning point in the project. Most Starfield weapons resolve instantly via hitscan — there's no in-flight object to redirect the way a rocket or grenade has. The project had already sunk significant, high-risk effort into the alternative (disassembling Starfield's internal ranged-attack and native aim-assist functions, with real crashes along the way and no working result) when Alexander asked a different question: rather than bending an instant hitscan raycast, what if a weapon's ammo could simply be switched to a real, simulated projectile at runtime while a target is Locked? That reframed an apparently unsolvable problem into one the project had already solved — redirecting a real, in-flight `RE::Projectile`, which the existing tracking/redirect logic already did for rockets and grenades. He then independently confirmed the idea was viable by finding a Nexus Mods weapon with working homing bullets and proposing to inspect its actual data in xEdit, which both validated the approach and supplied the exact reference values used to verify the implementation (see [`FINDINGS.md`](FINDINGS.md)). This one idea is very likely what saved the project from a much longer and much riskier disassembly effort.

### Scoping the projectile override to the lock instead of the shot

The fix that finally made shots land. The hitscan-to-projectile override was being engaged at the start of each trigger pull, on a thread spawned from the mouse hook — which meant every single shot raced the engine, and a round that left the barrel before the flip landed stayed a real hitscan with no projectile to redirect at all. Considerable effort had gone into narrowing that race from inside the shot path (including one attempt that stalled the system-wide input path badly enough to break firing entirely). Alexander asked why the override was tied to the shot at all, rather than simply being engaged when the targeting mode starts and released when it ends. It is the obvious question in hindsight and nobody had asked it: the original per-shot scoping existed only to keep a cosmetic side-effect window small, and that caution had quietly been prioritized over correctness. Moving it to lock scope removed the race outright rather than narrowing it, and in the first test afterwards every registered hit was a real hit. He also anticipated the follow-up question in the same breath, noting that delaying the restore after the mode ends would be unnoticeable to the player — correct, and the reason no additional grace handling was needed.

### Ending the lock on ADS instead of fighting the engine for it

Blocking aim-down-sights while a target is Locked had been attempted four separate ways — swallowing the input at OS level (detected reliably, no effect on the engine), polling `PlayerCamera` state (the iron-sights state never triggered for a real ADS), registering for `PlayerIronSightsStartEvent` (crashed on an unmapped Address Library ID), and `USER_EVENT_FLAG::Fighting` (holstered the weapon entirely, far broader than its name suggests). All failed or caused collateral damage. Alexander stopped the pattern rather than proposing a fifth variant: if the engine will not let the mod suppress ADS, then don't suppress it — end the VATS lock the instant ADS begins. That reuses the one piece that had always worked (detection via the low-level mouse hook; only the swallowing ever failed) and needs nothing risky at all. Same shape as the hitscan reframe: replacing a problem the engine defends with one it doesn't.

### The scanner-close keypress

Proposed simulating a real keypress (Win32 `SendInput`) to close the in-game hand scanner the moment a target locks, instead of either alternative actually on the table: hiding the scanner's menu directly (which yanks it away with no transition at all — no animation, no sound, an obviously artificial cut), or reaching into Starfield's own internal button-press/input-device machinery to replay a "real" press natively (unmapped in CommonLibSF, in the same high-risk category as the engine calls that have caused this project's crashes). A simulated keypress is indistinguishable from a genuine one to a single-player game with no anti-cheat, so it gets the scanner's actual close animation and sound for free, without touching a single risky internal function. Without this, the project would have had to choose between a worse-feeling UI transition and a meaningfully more dangerous implementation.

### Hiding the damage numbers through the game's own setting

While a target is Locked, Starfield's floating damage numbers clutter the mod's own overlay and need to disappear. The project's instinct was to reach into Scaleform and hide the display objects directly, which went badly: two hard failures in one session, one of them freezing the entire machine, before a decompile proved the damage-number popups were unreachable by any static path anyway (they are spawned per hit with engine-generated names). Alexander pointed out that Starfield simply *has* a "Show Damage Numbers" toggle in its own Interface settings — so rather than fighting the UI layer for the popups, flip the switch the game already provides. It worked immediately, needs zero Scaleform calls, and cannot crash in the way the previous attempts did. It is the same instinct as the scanner keypress above, applied to a different problem: prefer the mechanism the game already exposes over reaching past it.

### Reading enemy health the way the game itself does

The one that closed out a search that had been running for days. Live current health was treated as a hidden data field to be found, and it was hunted accordingly: the actor-value storage's base values (which turned out to be *max* health), its modifier array, then two rounds of blind raw-memory diff scanning across the whole Actor object. All of it failed, and one of those scans was even built so that a value falling to zero at the moment of death — the exact thing being searched for — was filtered out before it could ever be reported.

Alexander had already supplied the answer on 2026-08-24, by running `getav health` on a target, firing one shot, and running it again: 480.00, then 461.90. That is live current health, on demand, demonstrated rather than theorized. It was recorded as a *cross-check for the memory search* instead of being recognized as the answer, and the search continued past it for two more days. When he raised it again, he made the point that settles it: console `getav`, Papyrus `GetValue`, and the engine's `ActorValueOwner::GetActorValue` are all the same accessor reached from different directions — so if the console can produce the number, so can the mod.

He also supplied the independent confirmation that made it undeniable, from his own installed mods: Techrunner displays a scanned target's live health (345, then 198.53 after damage) and ships **no SFSE plugin at all**. A pure ESM with Papyrus scripts can only be going through that same accessor — proof both that the value is reachable and that no native reverse-engineering was ever required to reach it. And he noted that a mod scanning on demand shows the current value at scan time, which is the same reason polling per registered hit is enough for the resource system, rather than per frame.

Why this was missed is worth recording, because it is a failure of judgment rather than of information: this project has a hard-won rule that relocated *data reads* are safe while relocated *function calls* are the crash category, formed after two real crashes from mapped-but-wrong Address Library IDs. That rule was applied to `GetActorValue` even though it is a plain virtual call through the object's own vtable, with no Address Library lookup involved and therefore none of the failure mode the rule exists to prevent. A safety heuristic was generalized past its evidence, and it cost days.

The same day, the fix immediately settled a second problem that had also been open since 2026-08-22 and had resisted its own investigation: the lock not ending when a target dies. The dead-bit check written for it never once fired — a locked target's `boolBits` reads identically whether it is alive or lying dead on the floor. With live health working, death is simply `current <= 0`, confirmed against a real kill logging 1.12 and then -13.93 on overkill.

### Diagnosing the crit marker from the sound

A small observation that resolved a problem two rounds of reasoning had got wrong. Starfield's hit, kill and crit indicators were supposed to be hidden while a target is locked, and crit markers kept appearing anyway. Two explanations were produced and both were wrong — that the crit visual must be a different display object, then that Flash keyframes were resetting the visibility flag — and both rested on the same unexamined assumption: that the markers were appearing *during* the lock.

Alexander noticed he kept **hearing** the crit sound with nothing on screen, and only saw the marker at the moment the enemy died and the mode ended. That places the event exactly: the hiding worked all along, and what was visible was the *restore*, which fired the instant a kill ended the lock and un-hid an animation mid-play. The fix followed immediately — defer the restore until the indicator parks itself. The general lesson is worth keeping: establish *when* something happens before explaining *why*, and an audio cue can pin down a timing question that a screenshot cannot.

He also set the policy once it became clear the game raises some crit events seconds after a kill: suppress outright and rely on the sound rather than relocating the visual, since suppressing until those events stop would mean suppressing indefinitely.

### Reading the design questions off the measurements

Two cases where he was right about something the code was actively obscuring.

He asked repeatedly whether the target box sat differently on different characters, and specifically whether male and female NPCs behaved differently. The aim point was being lifted by a multiplier on how high the bounding sphere's centre sat above the actor's feet — and measurement showed that quantity varying 20% between two standing humanoids (0.895 against 0.748), which a multiplier *amplifies* rather than absorbs. Switching the lift to a fraction of the sphere's radius cut the spread by roughly a third, because an actor whose centre sits lower generally has the larger sphere. He had also, earlier in the same conversation, flagged the case that a radius-based lift would break — a body on the ground keeps a large radius while its centre drops — which is why the final version caps the lift by pose. Both halves of that design came from him.

He then spotted that the target box appeared to grow with distance, asking whether it was really doing that or just looked that way. It was the latter: a fixed pixel size against a shrinking target. The first attempt to fix it overshot badly — the bounding sphere encloses the whole body, so using its radius directly produced a box several times too large — and he rejected it immediately and precisely, saying he wanted it to look the way it did up close in the previous version. That is what it now does, capped at the old size rather than approximately calibrated to it.

### Seeing the size curve that the code was producing

Over one afternoon he reported three separate things about the target marker's size, each precise enough to point straight at its cause, and none of them a matter of taste.

First, that walking backwards made it grow before it shrank. The size was being derived by projecting a second point one bounding-sphere radius above the aim point and measuring the pixel gap — a method chosen so the box would inherit the projection's aspect handling for free. It also inherited a dependency on where on screen the target sat and on the camera's pitch, which suppresses the size at close range and releases it as you back off: exactly the shape he described. Computing the angular size directly removed it.

Then, that it "got smaller again" right up close. It did not shrink; it stopped. The ceiling was the previous fixed size, and it bound from roughly 5–7m inward, so across the close half of a fight the marker sat frozen while the target itself kept doubling. He then chose the replacement by looking at a target and naming the size he wanted — which is why the cap became a distance rather than a pixel count, and so scales with the creature and the display.

Finally, after watching Fallout 4 and 76 VATS footage, that those displays do not scale at all and never even read as changing size. That observation retired the whole scaling mechanism. The important half was his framing: their displays never claim to *enclose* the target, and a fixed size only looks wrong while something claims that. The marker was shrunk to match the claim rather than the other way round.

### Spotting the aim-point spread from screenshots, before the numbers were looked at

He reported that the marker sat at visibly different heights on two pirates in the same room, same pose, both male. The log agreed: their bounding-sphere radii differed by 1% while the heights of their sphere centres differed by 17%, and the factor applied to that centre turned a 13cm difference into 19.5cm on screen. Because the radii were identical, body size and pose were both ruled out — which is what isolated equipment inside the bounding sphere as the remaining suspect, and what settles that no factor applied to that centre can remove it.

The same instinct produced the flying-creature finding. He locked one, noticed the marker moving with its wingbeat, and said to watch the log live rather than describing it. The sphere's centre height was swinging 2.04m in time with the wings across nearly six thousand samples, and the radius between 3.09 and 4.50. That is not a flying-creature special case — a human guard's centre swings 0.09 and the ground-huggers 0.32 — so it was a missing piece under every aim-point model the project had tried, not a patch for one creature.

### Designing the HUD off the game's own scanner

The target marker's current form is his: a thin ring like the scanner's inner circle, the distance set beside it the way the scanner sets its range rather than written across the target, and no caption at all — his observation that the word "TARGET" was carrying nothing. He also spotted, comparing the two side by side in a screenshot, that the hint text looked dingy and angular next to the game's own lettering, which turned out to be three compounding faults on our side: a bitmap font being upscaled rather than rendered at size, text dimmed by transparency over a dark shadow that then showed through it, and an eight-direction halo closing the counters of every glyph.

### Calling time on per-body-part targeting

Proposed twice by Claude, from the Fallout 76 framing, and closed by Alexander with the strongest kind of evidence: a three-slot version had already been built and played, and it was not fun. He then supplied the reason it could not have been — Starfield has no crippling, no dismemberment and no limb reactions, so a body part is a damage multiplier and nothing else. He also identified the one case worth keeping from it, shooting an enemy who is only partly behind cover, and correctly placed it elsewhere: that is the depth-buffer occlusion feature, where it needs no slots, no interface and no balancing.

### Catching the settings that could not be tuned


After a fix appeared not to work, he asked whether the INI had been kept in sync. It had not: twelve settings the code reads had never been added to the template, so they ran on hardcoded defaults and were untunable by anyone — including one he needed at that moment. An earlier drift check existed but compared the deployed file against the template, and so was structurally incapable of noticing settings missing from the template itself. The check now runs against the code, and found a thirteenth on its first run. A warning that stays silent about the one thing going wrong is worse than no warning, and it took him asking to expose that.

### Calling time on the segmented health bar

Starfield's boss/legendary enemies show a segmented health display, and the overlay had a speculative implementation of it resting on an unverified guess about which actor value drives the segment count. It never worked — the pip row stayed rigid regardless of target. Rather than let it become the next multi-day offset hunt on the heels of one that had just ended, Alexander's call was to simply drop the feature. Removed rather than left in as dead decoration. Knowing which unknowns are worth chasing is a real contribution, and this project has more than once needed someone to say when one isn't.

### Calling time on auto-advance, too

The same judgement applied to a feature he had asked for himself. Auto-advancing to the next enemy on a kill worked mechanically but kept selecting targets behind walls and on other floors, because this project has no line-of-sight test and three successive filters had each turned out not to be one. Rather than accept a fourth approximation, he proposed parking the feature until real occlusion exists. That converted an accumulating pile of heuristics into a single, clearly-stated dependency — and it is the right trade, since the depth-buffer work that unblocks it also serves hit-chance and per-body-part targeting later.

He also asked, when that idea came up, that it be written down properly rather than left in the conversation, noting it had not previously been considered for enemies behind cover — only for body-part visibility. That reframing is what makes it worth doing: the same work now unblocks three features instead of one.

### Routing targeting through the scanner

Identified the specific reason to select targets through Starfield's own scanner/activation system rather than a self-computed camera-cone "nearest actor" pick: it gives pixel-accurate targeting under the actual cursor, and — because the same system already has to decide "can the player actually see and reach this" for the vanilla "hold E to interact" prompt — it hands the project real, occlusion-correct line-of-sight for free. This is what led to finding `commandTarget`, and meant the project never had to build (or risk crashing on) its own physics-based line-of-sight raycast.

### Flagging that Starfield Engine Fixes already solved a blocking concern

Pointed out that Starfield Engine Fixes (already a dependency of this project, used for its crash logging) also patches the game to allow firing a weapon while the scanner UI is open — resolving an open concern about whether scanner-based targeting would even be compatible with actually being able to shoot afterward, without the project needing to work around it itself.

### Noticing that the whole machine slowed down, and then measuring it properly

Reported that his entire PC became sluggish after a few minutes with Starfield running — including while the game sat in the background — and recovered the instant he quit it. That framing is what made the problem findable: a report of "the mod stutters" would have sent the investigation into the draw path, whereas "everything slows down, even outside the game" points at memory pressure and nothing else.

He then did the measurement work that turned a symptom into a location. Same scene and same activity every run (sitting in the star map, recording in OBS), a fixed ladder of builds, and — the part that mattered most — reading the **private** byte count rather than the working set the Task Manager shows, because Windows trims the working set on its own and that hides a leak completely. Five runs isolated the growth to a single call: with the overlay's draw call present the process gained roughly 2 GB per minute, and with it absent it gained nothing.

The star-map choice deserves its own mention. In that menu the overlay's own draw function returns before producing a single vertex, so the leak was proven to be independent of anything being drawn — which is what pointed at buffer renaming rather than at draw volume, and made the eventual root cause the obvious candidate rather than one of many.

### Asking why two gauges could ever appear together

The companion shield arc and the VATS resource arc were overlapping on screen. The fix that had been applied was to move the shield arc onto its own radius — treating the overlap as a layout problem. Alexander asked instead why the two were appearing at the same time at all, since one belongs to a combat lock and the other to a support session and the two modes are mutually exclusive by design. They were not supposed to coexist, and the resource arc was simply missing its mode check. His question found the actual bug; the layout change had been dressing a symptom.

### Diagnosing a suppressed key from inside the game

The plan to put the support action on Starfield's own activate key (E) rested on the mod's low-level keyboard hook swallowing that press so the game would not also act on it. Alexander settled whether it did from the game itself, without any instrumentation: he could still start a conversation with the companion, so the key was plainly reaching Starfield. That one observation killed the approach immediately, and it generalised further than the feature — the same hook backs the back-key exit, which is therefore unreliable too, and hold-to-exit is the exit that does not depend on it.

### Turning a timer into a status line

The companion shield readout was drawn in the middle of the screen whenever a shield was running. He first asked for it to be moved into the scanner view and onto the ring's lower edge, which fixed the clutter. Then came the better idea: the line should lead with the companion's **health**, and show the shield only as an addition when one is active. The reasoning is the useful part — the shield timer answers "how long is left" and is only meaningful once you already know you granted one, whereas the question you actually raise the scanner to ask is whether anyone needs help at all. The readout now answers the question the player has rather than the one the code happened to have data for.

### Taking the Starborn powers out of the project

The one thing blocking a whole class of features was a broken CommonLibSF type that puts Starfield's Papyrus VM out of reach, and the Starborn powers hung on it entirely. Rather than treating that as a problem to solve, Alexander re-scoped it: the powers become a separate Creation Kit mod, without SFSE, where the calls in question are ordinary Papyrus and the problem simply does not exist. That reframing is the same move as the hitscan idea at the top of this document — replacing a problem the engine defends with one it does not — and it demoted the project's top open item to near the bottom in a single decision.

### UI/overlay behavior

Beyond visual preferences (legibility, a gray-to-white lock transition), directly shaped *when* the HUD should behave differently: which menus and transitions should end an active lock outright rather than just hide the overlay (dialogue, the star map, other blocking menus), and, later, catching a case the project had missed entirely: the VATS overlay drawing directly over a native Starfield tutorial popup, a real bug that led to a fix distinguishing "end the lock" from "just don't draw this specific frame."

### Design and direction, throughout

The core concept — a real-time, Fallout 76-style VATS for Starfield, no time-slow, no menu pause — and essentially every subsequent design call: the lock-on flow (redesigned twice before settling on a simple Off/Locked toggle), gating activation on the hand scanner being open, dropping body-part targeting to unblock the projectile-redirect approach. He also **rejected a screen-space "aim precisely at the crosshair" hit-chance model**, pointing to Fallout 4/76 VATS reference screenshots showing a high hit chance despite an imprecise reticle — the project's actual distance-based chance formula follows directly from that correction.

### Diagnosing the hitscan-redirect bugs

Beyond the original idea, drove the debugging of its rollout: the observation that "the faster I click, the more shots go straight" led directly to finding a thread-handling bug in the click logic; repeated, specific skepticism ("that's not always the explanation") pushed several more fixes to actually get found instead of an issue being called solved too early; and a deliberate point-blank-range test correctly ruled out a distance-based theory and redirected attention to the real cause. The same precision showed up again later and mattered just as much: asked whether redirected shots were landing slightly off, he answered that they were not drifting near the target at all but flying "perfectly straight, as if never touched." That distinction — between a redirect that is mis-aimed and one that never takes effect — is what ruled out aim-point calibration as a theory and pointed the investigation at timing, where the actual cause was.

### Isolating one unknown at a time

With both the hit-chance system and the underlying redirect mechanic unverified at the same time, a "miss" could mean either a rolled miss or a broken redirect, and every test result was ambiguous. Alexander cut the knot by removing the dice entirely — every shot hits unless something is physically in the way, and the chance system comes back only once the mechanic underneath it is known to work. That single call is what made the following test session interpretable at all, and it is what the eventual root cause was found against.

### Rejecting approaches with hidden costs

Repeatedly caught proposals whose downsides hadn't been thought through. Editing `hudmenu.gfx` directly to suppress the native hit marker was killed on two grounds Claude had not weighed: the edit would apply permanently rather than only while VATS is active, and the file belongs to another mod, so the change would have to be redone on every one of its updates. Elsewhere he asked directly whether a proposed background thread rewriting an engine field every 5ms would cause unwanted side effects (it would, and the answer shaped a narrower design), and asked what a newly added projectile-speed override would mean for high-rate-of-fire weapons before it was ever tested — the kind of question that gets asked after a bad surprise, not before, unless someone insists.

### Falsifying explanations rather than accepting them

When Claude explained a bug by arguing that a bullet's entire flight lasted a single frame, Alexander immediately pushed back: by that logic the redirect should fail *completely*, not intermittently — so why had it ever appeared to work? The challenge was correct and the explanation as stated was not consistent with the evidence. Working through it produced the actual conclusion: the redirect had essentially never worked for guns at all, and the hits that made it look functional had been his own aim landing on target anyway — which is precisely the thing VATS is supposed to make unnecessary. Several of this project's real root causes have come out of that habit rather than out of the first plausible story.

### Keeping the record straight

Claude repeatedly reconstructed project history wrongly — asserting a custom health bar was working when it had never worked, and describing an earlier test session as showing successful hits when it showed none. Each time Alexander corrected the record instead of letting the wrong premise stand, and pushed the verification back where it belonged: read the handoff document, read the logs directly, check what actually happened rather than inferring it. Several sections of this project's documentation exist because of that insistence, and at least two proposed "fixes" were abandoned once the premise behind them turned out to be false.

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
- A crosshair-hiding attempt whose guessed setting names all silently resolved to nothing, which Alexander's first in-game test exposed — fixed by dropping the guesswork and pulling the real names (`bCrosshairEnabled` under `[GamePlay]`, `fCrosshairAlphaPercent` under `[Interface]`) from a community-maintained dump of Starfield's INI settings, then trying both setting collections since which one holds the resolved object at runtime was unconfirmed.
- A cell-reference scan capped at 4096 entries that silently dropped a dynamically-added companion actor from targeting in any larger cell — found by ruling out a camera-angle theory first (no actor was within range at all, at any angle, for the whole window), then tracing it to the scan cutoff and raising the cap with a warning against ever silently truncating again.

### Why the redirect never worked on guns, and the heap corruption underneath it

Two findings that came out of instrumenting rather than guessing, after the redirect had been "working" in the log and failing on screen for a long time.

The first was arithmetic on the project's own telemetry. Every projectile in a session's log appeared at exactly one of two ages — `0.000` (created, velocity still zero) or `0.011` (one game frame later, already moving) — and never again. With world units established as metres and a standard round travelling at 500 m/s, that first frame is 5.5 metres of flight, and a logged engagement distance of 5.8 metres meant the round covered the entire distance to its target inside a single frame. The engine updates a projectile once per frame, so there was no in-flight moment in which its trajectory could be rewritten at all, and no polling rate could manufacture one. That also explained the project's oldest unexplained asymmetry — why the redirect had always worked on rockets and grenades, which are slow enough to live many frames. The fix followed directly: force a much lower projectile speed for the duration of a lock, using the same already-proven-writable field family as the type byte, which converted a zero-frame window into roughly six.

The second was found by adding a write-verification readback — logging, one tick later, whether a written value had survived — because the project had never actually confirmed that its own writes were kept, only that they were performed. The writes held. But the same logs showed a single memory address being picked up as a brand-new projectile twenty-three separate times, which exposed a bug latent since the tracker was written: tracked rounds are keyed by raw pointer, freed projectile memory gets recycled into unrelated objects, and `SafeRead` cannot detect that because the heap page stays mapped and simply returns someone else's data. Nothing ever failed, so the tracker kept writing into recycled allocations for up to 1.5 seconds — textbook heap corruption, surfacing much later as an access violation in the allocator with no trace of this project in the backtrace. The speed override had amplified it roughly sixfold by making rounds live longer. Fixed by re-validating the object's form type and ownership before every write, which makes writing into a recycled slot structurally impossible rather than merely unlikely.

### Locating the ActorValueOwner sub-object through RTTI

Once the decision was made to call the engine's health accessor (Alexander's, see above), one genuine obstacle remained: CommonLibSF's `Actor.h` does not list `ActorValueOwner` among Actor's base classes, so nothing in the headers says where that sub-object sits inside an Actor, and guessing an offset in this codebase has a poor track record. The approach that worked was to make the binary answer the question instead: read the MSVC RTTI data the compiler already emitted for every polymorphic class, walk Actor's class hierarchy descriptor, and take the base whose type descriptor literally spells `ActorValueOwner` along with the member displacement recorded next to it. That is `Actor+0x070`, read rather than guessed, and every step of it is a guarded plain data read — so the virtual call only ever happens on a sub-object that has identified itself, re-verified on each call.

The first attempt at this was wrong in an instructive way: it scanned the object for base-class vtable pointers and read each one's RTTI name, which found all 25 vtables but reported `Actor` for every single one, because a complete object locator names the complete class rather than the base its vtable serves. The base names and offsets live one level further in. The same run also produced Actor's full base-class map with offsets as a side effect, which is layout information the headers do not carry.

### The leak in a call that drew nothing

Alexander's measurements had narrowed the 2 GB-per-minute growth to a single line — the overlay's ImGui draw call — and the obvious reading was that drawing leaked. The evidence said otherwise, and the star-map runs were the reason: the leak ran at full rate in a menu where the draw path returns before producing one vertex.

That left buffer *renaming* rather than drawing. ImGui's D3D11 backend refills its vertex, index and constant buffers each frame with `Map(D3D11_MAP_WRITE_DISCARD)`, which asks the driver for a freshly renamed allocation so the CPU never waits on the GPU. On an ordinary D3D11 device that is exactly right, because the pool of renamed allocations is reclaimed at `Present`. This overlay never calls `Present`: it draws on a D3D11On12 device whose work is submitted by a `Flush()` from the game's *D3D12* present hook, so the D3D11 side never sees a frame boundary and the pool grew without bound. The fix converts those three buffers to `USAGE_DEFAULT` filled with `UpdateSubresource`, addressed through a `D3D11_BOX` so each command list writes at its own offset. Re-measured against the same two scenes, both went from gigabytes of growth to none.

### Two independent bugs in the same three lines

The HUD font had failed to load since the first day, with the log reporting "no TTF found" throughout. The paths were written with single backslashes, so `\W` and `\F` were unknown escapes and `\b` was a literal backspace character — a real bug, and fixing it changed nothing, which is what made the case interesting. Underneath it sat `IMGUI_DISABLE_FILE_FUNCTIONS` in `imconfig.h`, inherited verbatim from BetterConsole, which replaces ImGui's file-open function with a stub that returns null unconditionally. The font loader goes through that function, so it could never have succeeded regardless of the path. For BetterConsole the define is correct — it uses ImGui's built-in bitmap font and reads no files — which is precisely why it survived being copied. Two unrelated faults in three lines, where fixing the visible one leaves the symptom in place and makes the fix look wrong.

### Regressions and misjudgments along the way

Stated plainly, because the sections above would otherwise read as if the diagnosis went in a straight line. Over the same stretch of work Claude introduced three separate regressions, all caught by Alexander in testing and all reverted within the same session: moving the type override into the low-level mouse-hook callback, which stalled Windows' system-wide input delivery and broke firing outright; writing to a projectile before the engine had finished initialising it, which put float data where a pointer belonged and crashed on an engine worker thread; and leaving a speculative write of a wrong-typed handle value in place long after it had stopped being justifiable. Two of the three were attempts to fix the very timing problem that the frame-rate analysis above eventually explained properly — which is the actual lesson: measuring first would have been cheaper than three attempts at tightening a race that turned out not to be the cause.

Two more from the companion-support work, both small and both the same shape — a number or a symptom taken at face value. A logging limit of 250 entries, written to keep the inventory dump readable, was later carried along when the loop was extracted and quietly became a *search* limit: Alexander's inventory holds 417 entries and every buff item sat past the cutoff. Healing worked anyway, which is the part worth noticing — the Med Pack happens to lie within the first 250, so the bug hid behind a feature that appeared to work. And when the shield and resource arcs overlapped on screen, the response was to move one of them to a different radius; Alexander's question about why they could appear together at all found the actual cause, a missing mode check. In both cases the code produced a plausible-looking result and the plausibility was accepted.

The costliest error was not a regression but a misjudgment, and it ran for days rather than one session. Alexander's `getav` demonstration (see his section above) was logged as supporting evidence for a memory search rather than recognized as the solution. Three features were characterized as dead ends around the same stretch — hiding the damage numbers, the enemy health bar, and hiding the hit marker — and two of those were subsequently solved, one by Alexander pointing at a setting the game already exposes and one by simply calling the accessor he had already demonstrated. The proximate cause was over-applying this project's own safety rule about engine calls to a case it does not cover. The more useful lesson is procedural: when a rule rules out an approach the user has already demonstrated working, that is a reason to re-examine the rule, not the demonstration.

A quieter version of the same failure sat next to it. Ending the lock on a target's death was never called a dead end — it was assumed to be working, on the strength of the code existing, for three days. It had in fact never once fired, and nobody could have told from the log, because the only line it produced was one shared with every other trigger. Assuming an untested path works is not a smaller error than wrongly calling one impossible; it just takes longer to notice.

### Four aim-point models, and what kept going wrong

Worth recording as a unit, because the individual steps look reasonable and the pattern does not. The aim point on a target was rebuilt four times in two days: a multiplier on the bounding sphere's centre height, then a lift sized by its radius, then a height anchored at the actor's feet and scaled by radius, then back to a multiplier on the centre height. Each change was a real improvement on the case in front of it, and each was undone by the next case.

Two errors ran underneath all four. The first was generalising from too small a sample: the feet-and-radius model was chosen from three humanoids whose centre-to-radius ratios read 0.741, 0.778 and 0.778, and a seventeen-actor dungeon run then measured that same ratio spanning 0.47 to 0.86 — with the pose cap firing on ten of the seventeen, meaning the safety net rather than the model was producing the aim point for most targets. The second was building on humanoids only. The first tests against creatures killed the radius outright: it measures longest extent rather than height, and a ground-hugger 30cm tall reads a radius of 2.16.

What eventually helped was not a fifth model but two things that are not models at all — low-pass filtering the animation out of the sphere, and Alexander's diagnostic runs against varied body plans. The remaining ~20cm spread between characters is documented as accepted rather than solved.

### Proposing a feature without checking the game underneath it

Per-body-part targeting was recommended twice as the thing that would make the mod feel like Fallout 76's VATS, reasoning from that game's design rather than from Starfield's mechanics. Starfield has none of what makes body parts matter there. The proposal survived two rounds because it was never tested against the actual game — the same failure as the aim-point models in a different register: reasoning from a model instead of measuring the thing.

Two smaller ones from the same stretch, recorded because they are the kind that quietly distort a collaboration. Alexander's inside/outside arrangement for the two gauges was argued against in terms that claimed the idea had been Claude's to begin with; he corrected it. And a proposal to separate the marker's position from the projectile aim point was put forward on reasoning alone — the arithmetic, done afterwards, showed the two would sit 43cm apart, roughly two and a half marker radii on screen at 6m, and killed it. Doing the arithmetic first was available at no cost.

### The render/UI overlay hook


Ported the D3D12 present-hook chain (IAT hook on `CreateDXGIFactory2` → MinHook on `CreateSwapChainForHwnd` → hook `Present`/`ResizeBuffers`) from the public-domain Starfield-Console-Replacer project, adapting it from a menu-only overlay into an always-on HUD and stripping its input-handling entirely, which surfaced a load-bearing `ImGui_ImplWin32_Init` call that had been co-located with the removed code and needed to be re-homed. Spent roughly ten debugging rounds trying to locate the engine's own camera object in memory to reuse its real view-projection matrix, proved via vtable-identity matching that the method was sound but the object was genuinely unreachable within any practical search range, then abandoned that direction in favor of self-computing a standard pinhole projection from data the project already had reliably — the approach actually shipped.

### Diagnostic tooling and documentation

Designed the in-game diagnostic probes (`ProjectileFlagProbe`, `AimAssistProbe`) and the funnel/telemetry logging used to localize several of the bugs above, interpreted the resulting raw memory dumps and log output, and wrote [`FINDINGS.md`](FINDINGS.md) and this document.

## How this is tracked

Every commit in this repository's history that Claude authored or co-authored carries a `Co-Authored-By: Claude` trailer — `git log` is the detailed, timestamped record behind the summary above.
