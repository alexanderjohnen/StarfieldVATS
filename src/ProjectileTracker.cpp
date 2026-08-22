#include "ProjectileTracker.h"

#include "GameOffsets.h"
#include "SafeMem.h"

#include <algorithm>
#include <cmath>
#include <random>

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

		// The player reference's own persistent formID — see this file's
		// header comment for why this is a safe stand-in for a resolved
		// TESPointerHandle here.
		constexpr std::uint32_t kPlayerRefHandle = 0x14;

		// RE::Projectile field offsets — see Projectile.h in this
		// project's vendored CommonLibSF for the authoritative layout.
		constexpr std::size_t kMovementDirection = 0x158;
		constexpr std::size_t kVelocity = 0x164;
		constexpr std::size_t kShooterHandle = 0x180;
		constexpr std::size_t kAge = 0x220;

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

			std::uint32_t shooterHandle = 0;
			if (!Read(reinterpret_cast<const void*>(entry), kShooterHandle, shooterHandle) ||
				shooterHandle != kPlayerRefHandle) {
				continue;
			}

			float age = -1.0f;
			if (!Read(reinterpret_cast<const void*>(entry), kAge, age) ||
				age < 0.0f || age > kMaxRedirectAgeSeconds) {
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

			REX::INFO("[VATS] projectile redirect: {} entry=0x{:X} age={:.3f} speed={:.1f} dir=({:.2f},{:.2f},{:.2f})",
				a_hit ? "HIT" : "MISS", entry, age, speed, dir.x, dir.y, dir.z);
		}
	}
}
