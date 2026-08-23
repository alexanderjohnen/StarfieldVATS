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

### `RE::AimAssistData::aimAssistEnabled`

Force-writing this bool to `true` on the equipped weapon's live `BGSAimAssistModel` is **not**, by itself, enough to make hitscan shots curve toward a target.

**Confirmed via:** the write held across many subsequent reads (not silently reset by the engine), but produced zero observed change in shot behavior across repeated in-game tests.

## Observed correlation — mechanism unconfirmed

> **Not a confirmed causal fact.** The offsets and the measured values below are hard data. That this specific byte is *what actually decides* hitscan-vs-real-projectile firing is a hypothesis with strong supporting correlation, not an independently proven mechanism.

### Byte at `BGSProjectile::data + 0x84`

| Weapon | Confirmed behavior | Byte value |
|---|---|---|
| Rocket launcher | real flying projectile | `0x00` |
| "Shingen" (Nexus homing-bullet mod) | real flying projectile | `0x00` |
| 7 different hitscan weapons | instant hitscan | `0x02` (every sample) |

Zero exceptions across all samples collected. Writing `0x02 → 0x00` on a normally-hitscan weapon's live projectile was followed by that weapon's shots becoming findable and redirectable as real, in-flight `RE::Projectile` objects — but this hasn't been isolated from every other variable in the firing path, so it's reported as a strong lead, not a settled mechanism.
