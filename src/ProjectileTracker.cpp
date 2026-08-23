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

		// Only ever touch a projectile within this age window: old enough
		// that its velocity is already the real post-launch value (not a
		// same-frame default of zero), young enough that it's almost
		// certainly the round just fired rather than an earlier one still
		// in flight from a previous tick of this same held burst.
		constexpr float kMaxRedirectAgeSeconds = 0.15f;

		// World-unit jitter applied to the redirect target on a rolled
		// miss, so a shot that already had good real aim still visibly
		// misses instead of hitting despite the roll — mirrors the old
		// mouse-steering miss offset, applied to the redirect target
		// instead of the crosshair. Eyeballed, same spirit/precision as
		// the removed body-part offsets; tune in-game.
		constexpr float kMissOffsetWorld = 0.7f;
	}

	void ProjectileTracker::RedirectFreshProjectiles(RE::Actor* a_target, bool a_hit, std::unordered_set<std::uint64_t>& a_handled)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !a_target) {
			return;
		}
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

		RE::NiPoint3 targetPos{};
		if (!Read(a_target, GameOffsets::kLocation, targetPos)) {
			return;
		}
		targetPos.z += GameOffsets::kAimPointChestZ;

		if (!a_hit) {
			static thread_local std::mt19937           rng{ std::random_device{}() };
			std::uniform_real_distribution<float>       jitter(-1.0f, 1.0f);
			targetPos.x += jitter(rng) * kMissOffsetWorld;
			targetPos.y += jitter(rng) * kMissOffsetWorld;
			targetPos.z += jitter(rng) * kMissOffsetWorld * 0.5f;
		}

		const std::uint32_t scanCount = std::min<std::uint32_t>(size, 32768);

		for (std::uint32_t i = 0; i < scanCount; ++i) {
			std::uint64_t entry = 0;
			if (!Read(reinterpret_cast<const void*>(data), 8ull * i, entry) || !entry) {
				continue;
			}
			if (a_handled.contains(entry)) {
				continue;
			}

			std::uint8_t formType = 0;
			if (!Read(reinterpret_cast<const void*>(entry), GameOffsets::kFormType, formType) ||
				formType < kFormTypeProjectileMin || formType > kFormTypeProjectileMax) {
				continue;
			}

			// Diagnostic (2026-08-22): zero redirects fired in Alexander's
			// first in-game test despite plenty of logged HIT rolls - this
			// logs every candidate that reaches the projectile-formType
			// range regardless of what filters it out next, so the next
			// test session's log says exactly which check is rejecting
			// everything (shooterHandle mismatch vs. age window vs.
			// nothing ever reaching here at all). Remove once the real
			// cause is confirmed.
			std::uint32_t shooterHandle = 0;
			const bool    shooterHandleRead = Read(reinterpret_cast<const void*>(entry), kShooterHandle, shooterHandle);
			float         age = -1.0f;
			const bool    ageRead = Read(reinterpret_cast<const void*>(entry), kAge, age);
			REX::INFO("[VATS] projectile candidate: entry=0x{:X} formType=0x{:02X} shooterHandle={} (read={}) age={:.3f} (read={})",
				entry, formType, shooterHandle, shooterHandleRead, age, ageRead);

			// Diagnostic (2026-08-23): confirmed via a real test session that
			// shooterHandle read 0 and age read 0.000 on literally every one
			// of 618 candidate log lines above, across 8 distinct rocket
			// entries and dozens of consecutive frames each - a constant
			// value that never once changes is the signature of a wrong
			// offset, not a legitimately-unset field (same class of bug as
			// TESObjectCELL::references being off by 8 from its own
			// offsetof() - see commonlibsf-unmapped-ids memory). The
			// static_assert on RE::Projectile's total size (Projectile.h)
			// only proves the class is the right SIZE, not that every field
			// inside it sits at the offset the header claims. This dumps a
			// wider raw range around both suspect fields, twice per entry
			// (first sighting and ~40ms later, given the 2ms fast-poll
			// interval), so the two dumps can be diffed by hand: whichever
			// dword actually increases between dump #1 and #20 is the real
			// age, and whichever dword equals 0x14 is the real
			// shooterHandle. Remove once GameOffsets/kShooterHandle/kAge
			// above are corrected and redirects are confirmed firing.
			{
				static std::unordered_map<std::uint64_t, int> s_dumpCount;
				const int                                      n = ++s_dumpCount[entry];
				if (n == 1 || n == 20) {
					std::string hex;
					char        word[24];
					for (std::size_t off = 0x140; off < 0x240; off += 4) {
						std::uint32_t v = 0;
						if (Read(reinterpret_cast<const void*>(entry), off, v)) {
							std::snprintf(word, sizeof(word), "%03zX:%08X ", off, v);
							hex += word;
						}
					}
					REX::INFO("[VATS] projectile raw dump #{} entry=0x{:X}: {}", n, entry, hex);
				}
			}

			if (!shooterHandleRead || shooterHandle != kShooterIsPlayer) {
				continue;
			}
			if (!ageRead || age < 0.0f || age > kMaxRedirectAgeSeconds) {
				continue;
			}

			// A fresh, not-yet-handled, player-fired round. Mark it
			// handled unconditionally from here on, even if a read/write
			// below fails — otherwise a dead-end entry would be retried
			// every tick for the rest of the hold.
			a_handled.insert(entry);

			RE::NiPoint3 projPos{};
			RE::NiPoint3 oldVelocity{};
			if (!Read(reinterpret_cast<const void*>(entry), GameOffsets::kLocation, projPos) ||
				!Read(reinterpret_cast<const void*>(entry), kVelocity, oldVelocity)) {
				continue;
			}

			const float speed = std::sqrt(oldVelocity.x * oldVelocity.x + oldVelocity.y * oldVelocity.y + oldVelocity.z * oldVelocity.z);
			if (speed < 1.0e-3f) {
				continue;  // stationary/degenerate — leave alone rather than risk writing a NaN direction
			}

			RE::NiPoint3 dir{ targetPos.x - projPos.x, targetPos.y - projPos.y, targetPos.z - projPos.z };
			const float  dirLen = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
			if (dirLen < 1.0e-3f) {
				continue;
			}
			dir.x /= dirLen;
			dir.y /= dirLen;
			dir.z /= dirLen;

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
			(void)Write(reinterpret_cast<void*>(entry), kMovementDirection, dir);
			(void)Write(reinterpret_cast<void*>(entry), kVelocity, newVelocity);

			// On a hit, also point the round's own desiredTargetHandle at
			// the target (2026-08-22, Alexander's observation: ship-combat
			// missile lock-on already does real-time homing toward a
			// locked target natively - this is presumably the field that
			// drives it). Uses the target's formID directly as the handle
			// value, same "persistent ref" assumption as kPlayerRefHandle
			// above - unlike the player, an arbitrary combat NPC is NOT
			// guaranteed persistent (some are dynamically spawned, whose
			// real handle differs from their formID). If wrong, this
			// degrades gracefully: a handle that resolves to nothing or
			// the wrong object just means no extra native homing this
			// frame, not a crash - handle resolution failure is a normal,
			// expected case this engine is built to tolerate, and our own
			// direct velocity/movementDirection write above already
			// guarantees this frame's redirect regardless of whether this
			// extra hint does anything. Only on hit - a missed shot should
			// not gain native homing assistance toward the target either.
			if (a_hit) {
				const std::uint32_t targetHandle = a_target->GetFormID();
				(void)Write(reinterpret_cast<void*>(entry), kDesiredTargetHandle, targetHandle);
			}

			REX::INFO("[VATS] projectile redirect: {} entry=0x{:X} age={:.3f} speed={:.1f} dir=({:.2f},{:.2f},{:.2f})",
				a_hit ? "HIT" : "MISS", entry, age, speed, dir.x, dir.y, dir.z);
		}
	}
}
