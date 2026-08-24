#include "ProjectileTracker.h"

#include "GameOffsets.h"
#include "SafeMem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <unordered_map>

namespace VATS
{
	namespace
	{
		template <class T>
		[[nodiscard]] bool Read(const void* a_base, std::size_t a_off, T& a_out)
		{
			return SafeRead(static_cast<const std::byte*>(a_base) + a_off, &a_out, sizeof(T));
		}

		template <class T>
		[[nodiscard]] bool Write(void* a_base, std::size_t a_off, const T& a_val)
		{
			return SafeWrite(static_cast<std::byte*>(a_base) + a_off, &a_val, sizeof(T));
		}

		// Live projectile *reference* form types (world instances), not
		// RE::FormType::kPROJ (the base ammo-type record) — same range
		// used by the original diagnostic probe this file replaces.
		constexpr std::uint8_t kFormTypeProjectileMin = 0x4C;  // kPMIS
		constexpr std::uint8_t kFormTypeProjectileMax = 0x54;  // kPEMI

		// Excluded 2026-08-23: kPBEA (BeamProjectile, 0x4F) sits inside the
		// range above but isn't the actual fired round for most weapons -
		// it's a weapon's decorative laser-sight beam attachment (already
		// flagged as a false lead in this project's history, see
		// starfield-vats-mod-design memory's "Finding #1"). Confirmed
		// 2026-08-23 via ProjectileTypeOverride testing: a shot that
		// visibly went straight (didn't curve) correlated with this
		// tracker having redirected a kPBEA entry instead of the real
		// kPMIS round that shot actually used - the cosmetic beam got
		// bent, the real bullet never did. A genuine beam-type weapon
		// (Type::kBeam) would be missed by this exclusion too, but no
		// such weapon has been tested yet - revisit if one shows up.
		constexpr std::uint8_t kFormTypeBEAM = 0x4F;  // kPBEA

		// Corrected 2026-08-23 via in-game raw-memory diffing (see the dump
		// diagnostic below) — CommonLibSF's Projectile.h offsets were
		// wrong by a uniform -0x10 (16 bytes) for this build. Verified
		// against three independent numeric invariants, not just "it
		// looks plausible":
		//   - movementDirection (now 0x148, header claimed 0x158): read
		//     vector had length 1.00 - a normalized direction vector,
		//     exactly what this field must be.
		//   - velocity (now 0x154, header claimed 0x164): read vector had
		//     length ~120 - a plausible missile speed, not noise.
		//   - age (now 0x210, header claimed 0x220): read 0.000 at first
		//     sighting, 0.056 ~40ms later at the tracker's 2ms poll
		//     cadence - a real elapsed-time field, finally advancing (the
		//     header offset read a constant 0.000 on 100% of 618
		//     candidate log lines in the prior test).
		// shooterHandle (now 0x170, header claimed 0x180) does NOT fit the
		// same pattern as cleanly: it reads a constant 1, not a
		// TESPointerHandle-shaped value matching the player's formID
		// (0x14). Read as a plain bool ("was this fired by the player")
		// instead of a handle - see kShooterIsPlayer below. If a future
		// test shows enemy-fired projectiles also read 1 here, this guess
		// is wrong and needs revisiting.
		constexpr std::size_t kMovementDirection = 0x148;
		constexpr std::size_t kVelocity = 0x154;
		constexpr std::size_t kShooterHandle = 0x170;
		constexpr std::size_t kDesiredTargetHandle = 0x174;
		constexpr std::size_t kAge = 0x210;

		constexpr std::uint32_t kShooterIsPlayer = 1;

		// Only ever *pick up* a projectile within this age window: old
		// enough that its velocity is already the real post-launch value
		// (not a same-frame default of zero), young enough that it's
		// almost certainly the round just fired rather than an earlier one
		// still in flight from a previous tick of this same held burst.
		// Once picked up, a round is homed continuously regardless of age
		// (see kMaxHomingDuration) - this window only gates which rounds
		// get picked up as fresh in the first place.
		constexpr float kMaxRedirectAgeSeconds = 0.15f;

		// Safety net for continuous homing (2026-08-24) - stop re-aiming a
		// tracked round after this long regardless of what it's doing, so a
		// round whose reference somehow never fails to read (e.g. despawn
		// detection missed it) doesn't get homed forever. Generous relative
		// to every hold/redirect duration seen in testing so far (real
		// flights resolve well under a second at speed 1000).
		constexpr std::chrono::milliseconds kMaxHomingDuration{ 1500 };

		// World-unit jitter applied to the redirect target on a rolled
		// miss, chosen once per round (see TrackedState::missOffset) so a
		// continuously-homed miss stays one consistent near-miss instead of
		// jittering to a new random point every tick. Mirrors the old
		// mouse-steering miss offset, applied to the redirect target
		// instead of the crosshair. Eyeballed, same spirit/precision as the
		// removed body-part offsets; tune in-game.
		constexpr float kMissOffsetWorld = 0.7f;

		// Target's chest-height aim point plus this round's own fixed
		// miss-offset (zero on a hit) - recomputed from the target's
		// CURRENT position every call, which is what lets continuous
		// homing correct for target movement, not just our own earlier aim
		// error.
		[[nodiscard]] RE::NiPoint3 ResolveAimPoint(const RE::NiPoint3& a_targetPos, const RE::NiPoint3& a_missOffset)
		{
			RE::NiPoint3 out = a_targetPos;
			out.z += GameOffsets::kAimPointChestZ;
			out.x += a_missOffset.x;
			out.y += a_missOffset.y;
			out.z += a_missOffset.z;
			return out;
		}

		// Re-aims one already-tracked (or brand new) projectile toward
		// a_aimPoint. Deliberately does NOT log - called on every homing
		// tick for every tracked round, so logging here would reintroduce
		// the same per-frame log-spam problem this project hit and backed
		// out of earlier tonight (see CombatTargetOverride.h). Callers log
		// once at pickup instead.
		//
		// Returns false if this entry should be dropped from tracking
		// (unreadable/despawned, or degenerate - already at the aim
		// point); true otherwise, including the harmless "too early, its
		// velocity isn't assigned yet" case (kept tracked, retried next
		// tick).
		[[nodiscard]] bool HomeProjectile(std::uint64_t a_entry, const RE::NiPoint3& a_aimPoint, bool a_hit, RE::Actor* a_target, ProjectileTracker::TrackedState& a_state)
		{
			RE::NiPoint3 projPos{};
			RE::NiPoint3 oldVelocity{};
			if (!Read(reinterpret_cast<const void*>(a_entry), GameOffsets::kLocation, projPos) ||
				!Read(reinterpret_cast<const void*>(a_entry), kVelocity, oldVelocity)) {
				return false;  // despawned/unreadable - stop tracking
			}

			// Write verification (2026-08-25), logged once per round. Reads
			// back the direction we wrote on the PREVIOUS tick and reports
			// whether it survived. This is the question the whole
			// "redirect: HIT but the round flew straight anyway" problem
			// hinges on and that no test has ever actually answered: a
			// matching readback means the engine accepts our writes and the
			// bug is geometry/timing; a reverted one means the engine (or
			// Havok) owns these fields and the whole velocity-write
			// approach can't work as-is.
			const float speed = std::sqrt(oldVelocity.x * oldVelocity.x + oldVelocity.y * oldVelocity.y + oldVelocity.z * oldVelocity.z);
			const bool  inFlight = speed >= 1.0e-3f;

			// Logs at most twice per round: once right after the pre-launch
			// write, and once more after the round is actually moving -
			// the pre-launch readback alone turned out to prove very little
			// (2026-08-25: it reported "WRITE STUCK" while the round was
			// still sitting at age=0.000 with zero velocity, i.e. it only
			// showed nothing had overwritten us within ~3ms, not that the
			// value survived the engine's own launch).
			if (a_state.haveWritten && (a_state.readbackCount == 0 || (a_state.readbackCount == 1 && inFlight))) {
				++a_state.readbackCount;
				RE::NiPoint3 curDir{};
				float        curAge = -1.0f;
				const bool   dirRead = Read(reinterpret_cast<const void*>(a_entry), kMovementDirection, curDir);
				(void)Read(reinterpret_cast<const void*>(a_entry), kAge, curAge);
				const RE::NiPoint3& w = a_state.lastWrittenDir;
				const float         drift = dirRead ?
					std::sqrt((curDir.x - w.x) * (curDir.x - w.x) + (curDir.y - w.y) * (curDir.y - w.y) + (curDir.z - w.z) * (curDir.z - w.z)) :
					-1.0f;
				REX::INFO("[VATS] redirect readback: entry=0x{:X} age={:.3f} wroteDir=({:.3f},{:.3f},{:.3f}) nowDir=({:.3f},{:.3f},{:.3f}) drift={:.3f} -> {} | vel=({:.1f},{:.1f},{:.1f})",
					a_entry, curAge, w.x, w.y, w.z, curDir.x, curDir.y, curDir.z, drift,
					drift >= 0.0f && drift < 0.01f ? "WRITE STUCK" : "OVERWRITTEN BY ENGINE",
					oldVelocity.x, oldVelocity.y, oldVelocity.z);
			}

			RE::NiPoint3 dir{ a_aimPoint.x - projPos.x, a_aimPoint.y - projPos.y, a_aimPoint.z - projPos.z };
			const float  dirLen = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
			if (dirLen < 1.0e-3f) {
				return false;  // effectively arrived / degenerate - stop tracking
			}
			dir.x /= dirLen;
			dir.y /= dirLen;
			dir.z /= dirLen;

			a_state.lastWrittenDir = dir;
			a_state.haveWritten = true;

			// Pre-launch frame (2026-08-25): velocity still reads zero, i.e.
			// the engine has created the round but not yet launched it. This
			// used to bail out here and wait for the next tick - which was
			// the core bug behind "the bullet just goes where I was
			// looking". Measured from a real session's log: a round is only
			// ever seen at age=0.000 (velocity 0) or age=0.011 (velocity
			// already assigned, i.e. one full game frame later), and is gone
			// from the scan entirely after that. At speed 500 that first
			// frame is 5.5 METRES of travel in the weapon's original
			// direction, with roughly one frame of life left afterward -
			// so by the time the old code was willing to touch the round,
			// its trajectory was effectively already decided. Polling
			// faster can't fix that (the engine only updates the round once
			// per frame; a 2ms poll just re-reads the same state five
			// times) - the only usable window is THIS one, before launch.
			// So: write the direction now, and let the engine launch the
			// round already pointed at the target. Velocity is deliberately
			// left alone here (writing a speed we'd have to invent could
			// fight the engine's own launch computation); if the engine
			// derives launch velocity from movementDirection this is exactly
			// right, and if it ignores it we've lost nothing - the normal
			// post-launch homing below still runs on later ticks.
			if (speed < 1.0e-3f) {
				(void)Write(reinterpret_cast<void*>(a_entry), kMovementDirection, dir);
				if (a_hit) {
					const std::uint32_t targetHandle = a_target->GetFormID();
					(void)Write(reinterpret_cast<void*>(a_entry), kDesiredTargetHandle, targetHandle);
				}
				return true;  // keep tracking - post-launch homing continues next tick
			}

			const RE::NiPoint3 newVelocity{ dir.x * speed, dir.y * speed, dir.z * speed };

			// Deliberately unsynchronized: no BSSpinLock acquired around
			// this write (see this file's header comment / SafeMem.h).
			// Both fields are plain floats (no pointers/handles), so a
			// torn concurrent write from the game's own simulation thread
			// degrades to one visually-off frame at worst, not a crash —
			// judged lower overall risk than calling BSSpinLock::Lock/
			// Unlock, a REL::ID-backed engine call never exercised by
			// this project before, to guard a write that's cheap to make
			// safe-by-construction instead.
			(void)Write(reinterpret_cast<void*>(a_entry), kMovementDirection, dir);
			(void)Write(reinterpret_cast<void*>(a_entry), kVelocity, newVelocity);

			// On a hit, also point the round's own desiredTargetHandle at
			// the target (2026-08-22, Alexander's observation: ship-combat
			// missile lock-on already does real-time homing toward a
			// locked target natively - this is presumably the field that
			// drives it). Uses the target's formID directly as the handle
			// value, same "persistent ref" assumption as elsewhere in this
			// project - unlike the player, an arbitrary combat NPC is NOT
			// guaranteed persistent (some are dynamically spawned, whose
			// real handle differs from their formID). If wrong, this
			// degrades gracefully: a handle that resolves to nothing or
			// the wrong object just means no extra native homing this
			// tick, not a crash. Re-written every homing tick, not just
			// once, in case the engine itself clears it between ticks the
			// same way currentCombatTarget turned out to (unconfirmed, but
			// cheap to keep refreshing regardless).
			if (a_hit) {
				const std::uint32_t targetHandle = a_target->GetFormID();
				(void)Write(reinterpret_cast<void*>(a_entry), kDesiredTargetHandle, targetHandle);
			}

			return true;
		}
	}

	void ProjectileTracker::RedirectFreshProjectiles(RE::Actor* a_target, bool a_hit, std::unordered_map<std::uint64_t, TrackedState>& a_tracked)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !a_target) {
			return;
		}

		RE::NiPoint3 targetPos{};
		if (!Read(a_target, GameOffsets::kLocation, targetPos)) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();

		// Pass 1: continue homing everything already tracked, using the
		// target's CURRENT position every call - see the header comment
		// for why this replaced the old redirect-once behavior. Silent
		// (no per-tick logging) - see HomeProjectile's comment.
		for (auto it = a_tracked.begin(); it != a_tracked.end();) {
			if (now - it->second.firstSeen > kMaxHomingDuration) {
				it = a_tracked.erase(it);
				continue;
			}
			const RE::NiPoint3 aimPoint = ResolveAimPoint(targetPos, it->second.missOffset);
			if (HomeProjectile(it->first, aimPoint, a_hit, a_target, it->second)) {
				++it;
			} else {
				it = a_tracked.erase(it);
			}
		}

		// Pass 2: pick up newly-fired rounds not yet tracked. Unchanged
		// fresh-pickup logic (cell scan, formType/shooterHandle/age
		// filters) from the previous one-shot design.
		auto* cell = player->parentCell;
		if (!cell) {
			return;
		}

		std::uint32_t size = 0;
		std::uint32_t capacity = 0;
		std::uint64_t data = 0;
		if (!Read(cell, GameOffsets::kCellReferences, size) ||
			!Read(cell, GameOffsets::kCellReferences + 4, capacity) ||
			!Read(cell, GameOffsets::kCellReferences + 8, data) ||
			size == 0 || capacity < size || !data) {
			return;
		}

		const std::uint32_t scanCount = std::min<std::uint32_t>(size, 32768);

		for (std::uint32_t i = 0; i < scanCount; ++i) {
			std::uint64_t entry = 0;
			if (!Read(reinterpret_cast<const void*>(data), 8ull * i, entry) || !entry) {
				continue;
			}
			if (a_tracked.contains(entry)) {
				continue;  // already being homed via Pass 1
			}

			std::uint8_t formType = 0;
			if (!Read(reinterpret_cast<const void*>(entry), GameOffsets::kFormType, formType) ||
				formType < kFormTypeProjectileMin || formType > kFormTypeProjectileMax ||
				formType == kFormTypeBEAM) {
				continue;
			}

			std::uint32_t shooterHandle = 0;
			const bool    shooterHandleRead = Read(reinterpret_cast<const void*>(entry), kShooterHandle, shooterHandle);
			float         age = -1.0f;
			const bool    ageRead = Read(reinterpret_cast<const void*>(entry), kAge, age);

			// Rejected silently as of 2026-08-25. This used to log EVERY
			// candidate reaching the projectile-formType range, before any
			// filter - a 2026-08-22 diagnostic for "which check is
			// rejecting everything", whose job is long done (the filters
			// demonstrably pass our own rounds now). The problem: a round
			// that fails this check is never tracked, so it was re-logged
			// on every single 2ms poll tick for its entire flight - i.e.
			// every NPC-fired round in an active firefight. Harmless while
			// rounds died within ~2 frames; distinctly less so now that the
			// speed override makes rounds live ~6x longer. This project has
			// already had one confirmed case of log I/O volume degrading
			// the redirect itself (see CombatTargetOverride.h), so the
			// candidate line now only fires for our own rounds, which are
			// tracked after pickup and therefore logged exactly once.
			if (!shooterHandleRead || shooterHandle != kShooterIsPlayer) {
				continue;
			}
			REX::INFO("[VATS] projectile candidate: entry=0x{:X} formType=0x{:02X} shooterHandle={} (read={}) age={:.3f} (read={})",
				entry, formType, shooterHandle, shooterHandleRead, age, ageRead);

			if (!ageRead || age < 0.0f || age > kMaxRedirectAgeSeconds) {
				continue;
			}

			// A fresh, not-yet-tracked, player-fired round. Picked up
			// IMMEDIATELY now, including in its pre-launch state
			// (velocity still zero) - this used to skip such a round
			// entirely and wait for a later tick, which meant we never
			// touched it until the engine had already launched and moved
			// it 5.5m. See HomeProjectile's pre-launch comment for the
			// measurements behind that change; it handles both states.
			RE::NiPoint3 projPos{};
			RE::NiPoint3 oldVelocity{};
			if (!Read(reinterpret_cast<const void*>(entry), GameOffsets::kLocation, projPos) ||
				!Read(reinterpret_cast<const void*>(entry), kVelocity, oldVelocity)) {
				continue;
			}
			const float speed = std::sqrt(oldVelocity.x * oldVelocity.x + oldVelocity.y * oldVelocity.y + oldVelocity.z * oldVelocity.z);

			RE::NiPoint3 missOffset{};
			if (!a_hit) {
				static thread_local std::mt19937     rng{ std::random_device{}() };
				std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
				missOffset.x = jitter(rng) * kMissOffsetWorld;
				missOffset.y = jitter(rng) * kMissOffsetWorld;
				missOffset.z = jitter(rng) * kMissOffsetWorld * 0.5f;
			}
			const RE::NiPoint3 aimPoint = ResolveAimPoint(targetPos, missOffset);

			const auto inserted = a_tracked.emplace(entry, TrackedState{ now, missOffset });
			(void)HomeProjectile(entry, aimPoint, a_hit, a_target, inserted.first->second);  // first redirect, right now

			// Distance from the round to its aim point at pickup, plus
			// whether this was the (much more useful) pre-launch catch or
			// the old already-flying one - together these say at a glance
			// how much flight time was actually left to work with.
			const float toTarget = std::sqrt(
				(aimPoint.x - projPos.x) * (aimPoint.x - projPos.x) +
				(aimPoint.y - projPos.y) * (aimPoint.y - projPos.y) +
				(aimPoint.z - projPos.z) * (aimPoint.z - projPos.z));
			REX::INFO("[VATS] projectile redirect: {} entry=0x{:X} age={:.3f} speed={:.1f} distToAim={:.1f} phase={}",
				a_hit ? "HIT" : "MISS", entry, age, speed, toTarget,
				speed < 1.0e-3f ? "PRE-LAUNCH" : "in-flight");
		}
	}
}
