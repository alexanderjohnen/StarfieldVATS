# CommonLibSF Findings

Struct-offset errors, unmapped Address Library IDs, and dead ends found while building StarfieldVATS against Starfield's Projectile/Weapon/Ammo data. Every entry below was reproduced in a running game (Starfield **v1.16.244.0**), not inferred from source alone.

Offsets are specific to this game version and will drift with future patches. This document keeps confirmed facts strictly separate from anything not yet independently proven — see the last section.

## Confirmed wrong offsets

Each of these was cross-checked against an independent ground truth: either a physically-required invariant (a unit-length direction vector, elapsed time that must strictly increase) or a real weapon's data authored in xEdit and compared byte-for-byte.

### `RE::Projectile` (`lib/commonlibsf/include/RE/P/Projectile.h`)

| Field | Header claims | Actual |
|---|---|---|
| `movementDirection` | `0x158` | `0x148` |
| `velocity` | `0x164` | `0x154` |
| `age` | `0x220` | `0x210` |

All three are shifted by a uniform `-0x10` from the header's claimed offsets, on a live, class-instantiated missile projectile.

**Confirmed via:** `movementDirection` read as a unit-length vector (magnitude 1.00); `velocity` read at a plausible missile speed (magnitude ≈120); `age` read `0.000` then `0.056` on the same object ~40ms later. A header-offset field that never changes across samples is the signature of a wrong offset, not a stalled counter.

### `RE::Projectile::shooterHandle`

The header types this as a `TESPointerHandle` at `0x180` (also shifted `-0x10` to `0x170` per the correction above). At the corrected offset the field is **not** a resolved handle matching the shooter's formID — it reads a constant `1` for every player-fired round observed, regardless of the player's actual formID.

**Confirmed via:** comparing against `1` (not the player's formID `0x14`) correctly gated real player-fired rounds across a full multi-weapon test session; comparing against the formID never matched a single one.

### `RE::WeaponAmmoData::ammo` (`TESObjectWEAP.h`)

The header's inline comment claims offset `0x20`; its own `static_assert(offsetof(WeaponAmmoData, ammo) == 0x18)` says `0x18`.

**Confirmed via:** the `static_assert` is a compile-time check against the struct layout the header itself declares — the inline comment is simply stale.

### `RE::AMMO_DATA::projectile` (`TESAmmo.h`)

The header types this (at `TESAmmo + 0x1F8`) as a resolved `BGSProjectile*`. It is not — the 8 bytes at this offset are an **unresolved `TESFormID`**, not a pointer: the upper 4 bytes read exactly zero on every sample, where every genuine 64-bit pointer observed elsewhere in the same process has a non-zero, ASLR-consistent upper half.

**Confirmed via:** byte-for-byte comparison against real heap pointers read moments earlier in the same memory region.

### `RE::WeaponAmmoData + 0x20` (undocumented)

Not in the header at all — that region is labeled padding. This is the live, resolved `BGSProjectile*` for the equipped weapon's currently-effective ammo.

**Confirmed via:** found by sweeping every 8-byte-aligned offset in the struct for a pointer whose target has `formType == kPROJ (0x3A)`; the resolved projectile's `flags`/`gravity`/`speed`/`range` then matched a real Nexus weapon mod's xEdit-authored `PROJ` record exactly (flags `0x2208` = Muzzle Flash + Pass Through Small Transparent + Seeks Target, bit-for-bit; speed `12.0`; range `1000.0`; gravity `0.0`).

### `RE::BGSProjectile::data` (`BGSProjectile.h`)

| | Header claims | Actual |
|---|---|---|
| `BGSProjectileData` base | `BGSProjectile + 0x128` | `BGSProjectile + 0x130` |

All relative offsets inside the struct (`flags` @ `data+0x48`, `gravity` @ `+0x4C`, `speed` @ `+0x50`, `range` @ `+0x54`, `Type` @ `+0x84`) are unaffected once the base is corrected.

**Confirmed via:** at the corrected base, `flags`/`gravity`/`speed`/`range` for a real mod weapon ("Shingen", a homing-bullet Nexus mod) matched its xEdit-authored values exactly: flags `0x2208`, gravity `0.0`, speed `12.0`, range `1000.0`.

## Confirmed wrong enum values

Distinct from wrong offsets, and easier to miss: the field is at the right address and reads a real value, but the *names* CommonLibSF gives the bits do not match this game. Nothing crashes; the check simply never fires.

### `RE::Actor::BOOL_BITS::kDead` (`Actor.h`)

Declared `1 << 11`. **Inert.** A locked target reads the same `boolBits` value whether it is alive or lying dead on the floor — verified by logging the raw field continuously across real kills, on several actors. A raw dump of the surrounding memory shows the struct layout itself is fine (`currentProcess` at `+0x228` reads as a well-formed heap pointer, `avStorage` at `+0x260` parses as a sane `{size, capacity, data}` array), so this is the enum, not the offset. `kSetOnDeath` (`1 << 23`) is no better — it is set on *living* actors.

Consequence for anyone relying on it: `IsDead()` and any hand-rolled `boolBits & kDead` check silently pass for corpses. In StarfieldVATS this meant dead actors stayed targetable and a "lock ends when the target dies" check never once fired in three days of use. **Use a health reading instead** (see below); current health goes to zero, and negative on overkill.

### `RE::Actor::BOOL_BITS::kPlayerTeammate` — this one holds

`1 << 26`, confirmed by measurement rather than by trusting the header: a companion and a hostile probed back to back read `0x162021A2` and `0x122021A2`, differing in exactly this bit. Recorded here because it shows the enum is not uniformly wrong — individual values need checking individually.

## Live actor values are reachable, and not through memory

`avStorage.baseValues` holds **maximum** health, not current. It reads correctly at full health and then never changes again for the rest of a fight, which makes it look like a working "current" read precisely when it is cross-checked — the two are equal at full HP. `avStorage.modifiers` has no health entry at all. Blind raw-memory diff scanning over the whole `Actor` object (twice, with different heuristics) turned up only countdown timers.

Live current health comes from **`ActorValueOwner::GetActorValue`** — the same accessor behind the console's `getav` and Papyrus' `Actor.GetValue`. There is no hidden data field to find.

The obstacle is that CommonLibSF's `Actor.h` does not list `ActorValueOwner` among `Actor`'s base classes, so nothing in the headers says where that sub-object sits. It can be read rather than guessed, entirely through data:

1. Take the vtable at the object's start, then the complete object locator at `vtable[-1]`.
2. Follow its `classDescriptor` (image-relative) to the class hierarchy descriptor.
3. Walk the base class array. Each entry carries a type descriptor **and** an `mdisp` — the base's offset within the complete object.
4. Match the descriptor name against `ActorValueOwner` and take its `mdisp`.

For 1.16.244.0 that yields **`Actor + 0x070`**. Dispatching through it (slot 01) returns live health, confirmed across a real kill: 345.00 → 218.84 → … → 1.12 → −13.93.

A note on why this is worth stating explicitly: a **virtual call through an object's own vtable performs no Address Library lookup**, so it does not carry the risk that unmapped or wrong `REL::ID` calls do (see the section below). Conflating the two is easy and, in this project, cost several days of searching for a memory field that does not exist.

One thing that does *not* work, recorded so nobody repeats it: scanning the object for base-class vtable pointers and reading each one's RTTI name. All 25 of `Actor`'s vtables report `.?AVActor@@`, because a complete object locator names the *complete* class, not the base its vtable serves. The base names live one level in, in the hierarchy descriptor.

## Crash-causing gaps

Not offset errors — these compile cleanly against the header but fail at runtime because the underlying Address Library ID or calling convention was never independently verified against this specific game build.

### `RE::TESHitEvent::GetEventSource()`

Calling this — or the generic `RE::BSTEventSource<T>::RegisterSink` it depends on — hard-crashes on every game launch.

**Observed:** a clean CommonLibSF-level abort, not a wild-pointer crash: `REL/IDDB.cpp(417): Failed to find offset for Address Library ID! Invalid ID: 0`. At least one required `REL::ID` is absent from the Address Library database for v1.16.244.0, despite the header compiling without error.

### A hand-cast `REL::ID(170456)`, used as "HasDetectionLOS"

Calling shape (two leading dummy `(0, 0)` args) copied from another tool's usage; never independently confirmed for this call site. Produced a hard crash with no crash log and no Windows Event Log entry — consistent with a fault deep enough inside engine code to skip the crash logger entirely.

**Observed:** isolated across 5 other ruled-out hypotheses by cross-referencing the last known-good build against when this specific call was introduced; confirmed by stubbing the call out and getting a crash-free session.

## Confirmed dead ends

Fields and single-flag writes that looked like the right lever but demonstrably aren't.

### `Actor::currentProcess->middleHigh->lastBoundWeapon` (offset `0x450`)

Despite the name, this is **not** the actor's currently-equipped weapon.

**Confirmed via:** read as null on every single shot across a full test session, despite `currentProcess`/`middleHigh` both resolving to valid, non-null pointers.

### `_visible` on Starfield's HUD (`hudmenu.gfx`)

Not a CommonLibSF issue, but the same shape of problem and it cost as much time. Starfield's HUD is **ActionScript 3** — the decompiled classes open with `package` and extend `flash.display.MovieClip` — so the visibility property is `visible`. `_visible` is the AS2 spelling and silently resolves to nothing.

The failure mode is what made it expensive: `IsAvailable()` on the *container* succeeded while every leaf path under it failed, which reads as a path problem and sent the search after progressively more exotic path guesses (slash notation, alternative roots, alternative container names). The container had been reachable the whole time. If a Scaleform path resolves at one level but not the next, check the property name before the path.

### `RE::AimAssistData::aimAssistEnabled`

Force-writing this bool to `true` on the equipped weapon's live `BGSAimAssistModel` is **not**, by itself, enough to make hitscan shots curve toward a target.

**Confirmed via:** the write held across many subsequent reads (not silently reset by the engine), but produced zero observed change in shot behavior across repeated in-game tests.

### Walking an actor's 3D node tree via `NiNode::children`

Reaching an actor's skeleton joints — to read a real chest/spine bone instead of deriving an aim point from a bounding volume — was not achievable through the header's `NiNode` layout. Recorded as a **negative result with the easy explanations ruled out**, not as a proven header error.

`NiAVObject` is `static_assert`ed at `sizeof == 0x130` and `NiNode::children` (a `BSTArray<NiPointer<NiAVObject>>`, layout `{size@00, capacity@04, data@08}`) follows immediately. Reading that triple off the actor's 3D root never yielded a plausible array on any actor.

What makes this worth writing down is that the same pointer is demonstrably good, and the header layout is demonstrably right most of the way to that offset: `worldBound` at `NiAVObject + 0x100`, reached by the chain `Actor + loadedData → LoadedRefData + 3D → NiAVObject`, returns sane sphere centres and radii on every actor tested, humanoid and creature alike. So the object is a real `NiAVObject` and the header is not simply shifted.

Ruled out:

- **Bone naming.** The obvious suspicion — that Starfield does not name joints `COM`/`Spine`/`Chest` the way earlier titles did — is wrong, or at least untested, because the walk never descended at all. Dumping every visited node reported `1 nodes visited` on every actor: only the root, never a child. The root's own name reads fine (`HumanExportRoot`, `MantidA_mrRigRoot`, `HopperA_mrRigRoot`), so name reading through the `BSStringPool` chain works on these objects.
- **A too-narrow search window.** Sweeping every 8-byte-aligned offset from `0x0F0` to `0x220` found no candidate either.
- **A bad second guess in the validator.** The first version accepted a candidate only if the first child's `parent` (`NiAVObject + 0x038`) pointed back at the node — itself a header claim that could have rejected a correct hit. Replacing that with "the first child must resolve to a readable name" changed nothing.

Anyone attempting this should treat the children array as unlocated for v1.16.244.0 and start from something other than the header offset. Note also that rig roots differ per creature family, so a named-bone lookup would never have been general anyway.

## Bounding-volume behaviour, measured

Not offset errors — these are properties of the data itself, recorded because anything positioning a HUD element or an aim point on an actor will run into them. Measured across humanoids and three creature types.

`NiAVObject::worldBound` is a **sphere**, and there is no axis-aligned alternative reachable from it. `BSBound` exists in the RTTI tables but has no CommonLibSF binding and would sit behind `NiObjectNET + 0x020`, typed only as `void* // NiExtraDataContainer*`. That has two practical consequences.

**The radius measures longest extent, not height.** It is a reasonable size proxy on a roughly upright humanoid and badly misleading on anything else:

| Actor | radius | centre above ref origin |
|---|---|---|
| Human | 1.03–1.15 | 0.77–0.94 |
| Mantid (tall, spidery) | 2.96 | 1.40 |
| Hopper (low, sprawling) | 2.16 | 0.30 |

A ground-hugging creature 30cm tall reads a radius of 2.16. Any aim point scaled from the radius aims metres into the air above it.

**The sphere breathes with the animation, and on some creatures it does far more than breathe.** Sampled per frame on a locked target:

| Actor | radius range | centre-height range |
|---|---|---|
| Human guard | 0.98–1.03 | 0.82–0.91 |
| Ground-hugger | 2.08–2.20 | 0.19–0.53 |
| **Flying creature** | **3.09–4.50** | **0.14–2.18** (5938 samples) |

The flyer's centre height swings over two metres in time with its wingbeat. A HUD element placed from the raw value follows it. Low-pass filtering the offset from the actor's ref origin — rather than its world position, so real movement still tracks with zero lag — is what makes it usable.

**The ref origin is not the lowest point.** `TESObjectREFR::data.location` is the reference's position, which coincides with a standing biped's feet only because that is where the model's origin sits. It does not track pose: a collapsing actor's sphere centre was measured *below* it (−0.008), and a flying creature's origin sat over two metres beneath its body.

## Observed correlation — mechanism unconfirmed


> **Not a confirmed causal fact.** The offsets and the measured values below are hard data. That this specific byte is *what actually decides* hitscan-vs-real-projectile firing is a hypothesis with strong supporting correlation, not an independently proven mechanism.

### Byte at `BGSProjectile::data + 0x84`

| Weapon | Confirmed behavior | Byte value |
|---|---|---|
| Rocket launcher | real flying projectile | `0x00` |
| "Shingen" (Nexus homing-bullet mod) | real flying projectile | `0x00` |
| 7 different hitscan weapons | instant hitscan | `0x02` (every sample) |

Zero exceptions across all samples collected. Writing `0x02 → 0x00` on a normally-hitscan weapon's live projectile was followed by that weapon's shots becoming findable and redirectable as real, in-flight `RE::Projectile` objects — but this hasn't been isolated from every other variable in the firing path, so it's reported as a strong lead, not a settled mechanism.

## The Papyrus VM is reachable, its argument type is not

`IVirtualMachine::DispatchMethodCall` (vtable slot 30) and
`DispatchStaticCall` (2F) are plain pure virtuals — no Address Library
lookup — and `GameVM::GetSingleton()->GetVM()` reaches them. That makes
Papyrus look like an attractive route to functions with no native binding
in CommonLibSF, and Starfield has several this project wants:

| Papyrus | on | why we want it |
|---|---|---|
| `DoCombatSpellApply(Spell, ObjectReference)` | `Actor` | apply a spell **to another actor** — the only exposed way to put buffs or Starborn powers on a companion |
| `RemoveItem(Form, int, bool, ObjectReference)` | `ObjectReference` | consume an aid item from the player's inventory |
| `RestoreValue(ActorValue, float)` | `ObjectReference` | heal — but this one also exists natively, see below |

**The blocker.** All three dispatch entry points take their arguments as

```cpp
const BSTThreadScrapFunction<bool(BSScrapArray<Variable>&)>& a_arguments
```

and CommonLibSF defines that type, in `RE/I/IVirtualMachine.h`, as:

```cpp
template <class F>
using BSTThreadScrapFunction = std::function<F>;
```

That is a **placeholder, not a binding**. Bethesda's real
`BSTThreadScrapFunction` is a scrap-heap-allocated callable with its own
layout; `std::function` has a different size and different internals.
Calling through it hands the engine a structure of the wrong shape — the
same class of failure as the wrong struct offsets recorded above, except
that this one is a call rather than a read, so it cannot degrade
gracefully the way `SafeRead` does.

`BSScrapArray` itself is fine (`BSTArray` with a scrap allocator,
`RE/B/BSTArray.h`), as is `IObjectHandlePolicy::GetHandleForObject`
(slot 07) for obtaining an object handle. The argument functor is the
single missing piece.

**Correction 2026-08-28.** An earlier version of this note said Starfield
has no `AddSpell`. It does - on `ObjectReference.psc` (line 191), which
`Actor` inherits from, alongside `RemoveSpell` on `Actor.psc`. The mistake
came from checking only an `Actor` API summary. So if the VM call is ever
solved there are TWO routes for buffs, not one: `DoCombatSpellApply` to
apply a spell to another actor, or `AddSpell` to give the companion the
spell so a Self-delivery effect lands on them.

Also checked, from the authoritative script sources in
`Data/scripts/source`: `Potion.psc` exposes only `IsHostile()`. An aid
item magnitude and duration are unreachable from Papyrus too, so the
fixed table in `AidItems.h` was not merely the safer option - it was the
only one.

**Consequence.** Anything that only exists in Papyrus is parked until
someone establishes the real layout. Healing was not parked, because it
does *not* need Papyrus: `ActorValueOwner::RestoreActorValue` is vtable
slot 09 on the same RTTI-verified sub-object this project has been
reading live health through since 2026-08-25. Same object, same
verification, one slot further along, no Address Library and no
hand-built struct.

## Confirmed constants and layouts (support feature)

### `TESForm::formType` for ALCH (aid items) is **54**

Measured 2026-08-28 by walking the player's inventory with all seven
base-game aid items carried. Every one of these came back as type 54:

| Item | FormID |
|---|---|
| Med Pack | `0x0000ABF9` |
| Trauma Pack | `0x0029A847` |
| Emergency Kit | `0x002A9DE8` |
| Alien Genetic Material | `0x000C1F57` |
| Hypergiant Heart | `0x00122E9C` |
| Heart+ | `0x0029CAD9` |
| Red Amp | `0x001F3E86` |

Thirteen entries in that inventory carried type 54, consistent with the
aid items present. (`ACHR` = 75 was already confirmed independently.)

### The inventory is reachable as plain data

`TESObjectREFR::inventoryList` (0xA0) is a `BSGuarded<BGSInventoryList*>`
whose pointer sits first; `BGSInventoryList::data` (0x28) is a `BSTArray`
with this project's usual `{u32 size, u32 capacity, T* data}` shape, and
`BGSInventoryItem` is 0x28 bytes with the bound object at offset 0.

Unusually for this project the offsets came from the CommonLibSF headers
rather than from probing, which was justified because these particular
types carry `static_assert`s on their own size - and then confirmed in
one run: 65 entries read, all seven expected form IDs present. Reading an
inventory therefore needs no engine call at all.

Note `stacks` reads the NUMBER OF STACKS, not the item count; the count
lives inside `BGSInventoryItem::Stack` and has not been verified yet.

### `ActorValueOwner::ModActorValue` - the three-argument overload faults

Confirmed by crash, 2026-08-29. Detailed Crash Logger named the line:

```
EXCEPTION_ACCESS_VIOLATION   Starfield.exe + 0x1977526
StarfieldVATS.dll  [VATS::TryModTemporary]   ActorValueProbe.cpp:280
```

with the actor (`ACHR 6C000806`) and the `ActorValueInfo`
(`AVIF 000002E3 "Damage Resistance"`) both valid in registers - so the
arguments were fine and the dispatch itself was not.

CommonLibSF declares two overloads back to back:

```cpp
virtual void ModActorValue(ACTOR_VALUE_MODIFIER, const ActorValueInfo&, float, TESObjectREFR*);  // 06 - new
virtual void ModActorValue(ACTOR_VALUE_MODIFIER, const ActorValueInfo&, float);                  // 07
```

Slots 01 (`GetActorValue`) and 09 (`RestoreActorValue`) on this same
object are proven correct in production, so the entries between them
exist - but nothing proves their ORDER. If it is reversed, a
three-argument call lands in the function that expects four and the
callee reads an uninitialised register as a pointer, which is exactly
what this fault looks like.

**Call the four-argument form.** It is safe under both orderings: if slot
06 really takes four, the call is correct; if the order is reversed and
it takes three, the extra register is ignored by the x64 convention.
Nothing is dereferenced blind either way. Pass a real reference rather
than `nullptr` - the actor itself will do, since its RTTI was verified
moments earlier.

The call was SEH-guarded while that reasoning was only reasoning, so a
wrong slot would have cost a disabled shield rather than a crash to
desktop. **That armour came off on 2026-08-29**, once a run applied the
shield three times over a real companion (0s to 30s to 57s to 86s, item
count 10 to 7, then expiry) with no fault and no warning anywhere in the
log. Slot 06 is the four-argument overload, confirmed in play.

## How other VATS implementations land their shots

Observed in play rather than read out of any code, so these are behavioural findings, not implementation facts. They are recorded because each one closed or reopened a question this project had been carrying.

### Fallout 76, and the VATS76 mod for Fallout 4

Both take the camera over. The player keeps WASD movement but not the view, aiming down sights ends the mode, and the view is held toward the locked target.

The interesting part is what happens on top of that: **the shots are still visibly steered.** The view does not sit exactly on the target, and rounds can be seen travelling diagonally toward it. Spread does not explain that - spread scatters around the aim axis, it does not converge on a point from an off-axis muzzle.

So the reference implementation is **two stages**: coarse orientation by camera, fine correction in flight. That matters here in three ways.

It confirms the mechanism this project chose. In-flight redirection is not a workaround for lacking camera control; the game being emulated does it too.

It explains why the camera is allowed to be imprecise. Once a fine stage guarantees the hit, the coarse stage is free to be optimised for how it looks instead of how accurate it is - and it visibly is, since an exact follow would jitter with every step the target takes. This project learned the same lesson one level down, on the aim point: the bounding sphere of a flying creature swings 2.04m with its wingbeat, the marker followed it exactly, and the fix was damping (`fAimPointSmoothingSeconds`).

And it locates this project's real gap. **We have the fine stage and no coarse stage at all.** Nothing constrains where the player looks, so the redirect must be able to cover any angle up to a full reversal, which is the entire reason projectiles are slowed to 80 m/s (`fLockedProjectileSpeed`). That slowdown is a consequence of a missing stage, not a property of the mechanic.

Camera takeover itself stays rejected - it contradicts this project's founding constraint that the weapon and camera must never visibly snap. What is adopted is the two-stage shape, with the player as the coarse stage.

### Starfield's own ship combat

Two separate mechanics, and the less obvious one is the useful one.

**The lock** (a rectangle that shrinks over a "LOCKING %" period once the view is held toward a ship) is the ship-scale equivalent of this mod's own lock. Nothing new.

**The convergence circle** is the interesting one. It is not glued to the target: it lags the camera and drifts toward the target marker while the player holds roughly the right direction. Once it sits inside the target rectangle, the ship's weapons aim there on their own and hit - including automatic weapons tracking the target without the player turning further.

That is the same two-stage split as above, with the **player** supplying the coarse stage and a tolerance zone deciding when the fine stage engages. It is the model that fits this project's constraints, because nothing is taken away from the player at any point.

The mechanism does not transfer: ship weapons sit on mounts that rotate independently of the hull, and a handheld weapon points where the character points. What transfers is the rule - when does assistance apply, how strongly, and how does the player see that it is applying.

Open question, answerable only in play: does the circle keep drifting at large angles (a soft weighting) or stop entirely past some angle (a hard tolerance)? That decides which shape the equivalent rule takes here.

## Synthetic mouse input turns the view - 0.0806 deg per unit

Measured 2026-09-06 with `CameraNudgeProbe` (`bProbeCameraNudge`). A fixed 40-unit `SendInput`/`MOUSEEVENTF_MOVE` pulse during a lock, with the player's yaw read back through the same guarded path everything else here uses, then undone.

**159 samples returned +3.224 degrees - identical to five decimal places every time.** Not merely proportional: deterministic. One unit is ~0.0806 degrees, fine enough for a gradual pull rather than visible steps.

This settles a question the removed camera-steering AimAssist never answered. That code converted degrees to raw mouse units through `fMouseSensitivityScale`, a factor its own comment admits was never derived; every constant layered on top of it was guesswork on guesswork.

It also rests on an asymmetry now established three times over in this project: **injecting** input reaches this game (the scanner-close keypress is built on it and is proven), while **suppressing** input does not (four failed attempts at blocking ADS, plus the unreliable back key). Anything shaped as "add movement" is viable; anything shaped as "remove the player's movement" is not.

Two caveats belong with the number:

**It only applies while the game has focus.** The same run produced 166 samples reading `2.3008 -> 2.3008` - no movement at all - because the player had alt-tabbed and an unfocused Starfield consumes no mouse input. The pulses did not vanish; they went to the desktop and visibly shook the real cursor. Any code sending synthetic input must check that the foreground window belongs to this process, both to stay out of the player's desktop and to keep unfocused frames out of any measurement, where they are not evidence of failure but of nobody listening.

**The factor is tied to the player's own mouse sensitivity** (`fMouseHeadingSensitivity` in `StarfieldPrefs.ini`); a different setting gives a different factor. Recorded and accepted rather than solved (Alexander, 2026-09-06): this mod is not distributed beyond GitHub, so the value that is correct for his setup is the value this project needs. It belongs in the INI as a tunable default rather than baked into code - a number measured once still becomes wrong silently when the setting underneath it changes, and looks more trustworthy than a guess while doing the same damage.

One flaw in the probe, recorded because the shape recurs: its running average mixed the zeros with the real values and converged on 0.042, a figure describing nothing. The answer was the most common value, not the mean. **An average across a bimodal sample produces something that looks like a measurement and is not one.**

## Reopened: `Projectile::desiredTargetHandle` (0x174)

Not a new find - this project wrote to it once, on every homing tick, hoping to trigger the engine's own lock-on behaviour, and removed it on 2026-08-25. What is new is the evidence.

The removal reasoned that it was a guess on two counts: that the field drives that behaviour at all, and that a raw form ID is a valid `TESPointerHandle` (it is not, for a dynamically spawned combat NPC). Both were unknowns, not disproofs, and the code comment says as much - restore it from git history if native homing is ever pursued properly, with the handle type established first.

**Starfield's ship combat settles the first count.** Ship weapons demonstrably track a selected target while the player looks elsewhere, so the engine owns a homing mechanism and ships with it in use. Supporting this on the live projectile: `desiredTargetHandle` sits at 0x174, immediately after `shooterHandle` at 0x170 - an offset this project reads correctly in production, since player-fired rounds report `1` there - and the class carries a virtual `ShouldUseDesiredTarget()`, i.e. the engine asks in flight whether to use a desired target.

The second count is still open and is the actual work: obtain a real handle for the target actor rather than inventing one. There is a lead - an enemy's `combatTarget` reads `1` against the player, so that field holds handles, which makes this a read rather than a construction.

The payoff if it holds is not homing for its own sake: the engine steers inside its own simulation step, so it does not need the flight time our manual steering does, and `fLockedProjectileSpeed` could shrink or go.

**The risk is unchanged.** Writing a wrong-typed value into a handle the engine dereferences is the exact shape of both crashes of 2026-08-25. The handle type has to be established before the write, not after.
