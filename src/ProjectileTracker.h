#pragma once

#include <chrono>
#include <unordered_map>

namespace VATS
{
	// Bends a freshly-fired player projectile's own in-flight trajectory
	// toward (hit) or away from (miss) a_target, instead of nudging the
	// camera/weapon before the shot ever leaves the barrel. Real vanilla
	// ballistics/damage/hit-reactions/death still apply from there — this
	// only ever rewrites the round's own velocity/movementDirection a beat
	// after it spawns. Replaces the earlier camera-nudge AimAssist design
	// (SendMouseMove-based steering) 2026-08-22 per Alexander: the weapon
	// should never visibly snap onto the target at all, the round should
	// just curve onto it mid-flight.
	//
	// Call repeatedly (e.g. every AimAssist tick) for the duration of one
	// held trigger — a burst fires multiple rounds over time, not all at
	// once. a_tracked is owned by the caller and should start as a fresh
	// empty map at the beginning of each hold; passing the same map back in
	// on every call lets each new round in the burst be found and picked up
	// independently.
	//
	// 2026-08-24: previously redirected each round exactly once (the
	// instant it was found) and never touched it again — a single aim-point
	// error or the target moving mid-flight meant the round then flew
	// straight on that one, potentially-wrong heading for the rest of its
	// life. That's the leading suspected cause of "HIT logged, no damage
	// landed": the roll and the redirect both happened correctly, but a
	// static feet+chest-height aim point isn't always inside the target's
	// actual hitbox, and nothing corrected for it afterward. Now re-aims
	// every already-tracked round toward the target's CURRENT position on
	// every call (continuous homing, not one-shot), for up to
	// kMaxHomingDuration — corrects for target movement and residual aim
	// error over the round's whole flight instead of only the instant it
	// was found. A missed shot keeps its own fixed random near-miss offset
	// (chosen once at pickup, stored in TrackedState) rather than getting a
	// fresh random jitter every tick, so it still reads as one consistent
	// near-miss rather than an erratically dancing round.
	//
	// Matches player-fired projectiles via a field at Projectile+0x170
	// reading exactly 1 — NOT the TESPointerHandle-style formID match
	// this originally assumed. See ProjectileTracker.cpp's comment for
	// the full story: CommonLibSF's Projectile.h offsets
	// (movementDirection@0x158, velocity@0x164, shooterHandle@0x180,
	// age@0x220 — despite a static_assert on the whole class's size,
	// which only proves the SIZE is right, not every field's offset)
	// were wrong by a uniform -0x10 for this build, discovered
	// 2026-08-23 via in-game raw-memory diffing after the offsets-as-
	// documented produced zero working redirects across 13 real test
	// shots. Corrected offsets: movementDirection@0x148, velocity@0x154,
	// shooterHandle@0x170 (read as a bool, not a handle — see
	// kShooterIsPlayer), age@0x210.
	//
	// Writes are NOT synchronized against the game's own simulation
	// thread (no BSSpinLock acquired — see the .cpp for why that tradeoff
	// was deliberate). All reads/writes go through SafeRead/SafeWrite
	// regardless, so a projectile despawning mid-operation degrades to
	// "skipped", never a crash.
	class ProjectileTracker
	{
	public:
		struct TrackedState
		{
			std::chrono::steady_clock::time_point firstSeen;
			RE::NiPoint3                          missOffset{};  // fixed for this round's whole life; zero on a hit

			// Write-verification state (2026-08-25). This project has never
			// actually confirmed that writing movementDirection/velocity
			// changes anything - "redirect: HIT" only ever meant "we
			// performed the write", never "the engine kept it". These record
			// what was last written so the NEXT tick can read the same
			// fields back and log whether our value survived or the engine
			// overwrote it. Logged at most once per round (readbackLogged)
			// - a per-tick log here would reintroduce the log-spam problem
			// this project already backed out of once.
			RE::NiPoint3 lastWrittenDir{};
			bool         haveWritten{ false };
			int          readbackCount{ 0 };  // max 2 lines per round: once pre-launch, once in flight
		};

		static void RedirectFreshProjectiles(RE::Actor* a_target, bool a_hit, std::unordered_map<std::uint64_t, TrackedState>& a_tracked);
	};
}
