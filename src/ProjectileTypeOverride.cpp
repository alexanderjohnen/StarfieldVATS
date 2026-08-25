#include "ProjectileTypeOverride.h"

#include "GameOffsets.h"
#include "SafeMem.h"
#include "Settings.h"

#include <mutex>
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

		// Same equipped-weapon resolution as AimAssistProbe.cpp/
		// ProjectileFlagProbe.cpp - duplicated rather than shared, matching
		// this project's existing per-file style.
		constexpr std::size_t   kInventoryList = 0xA0;
		constexpr std::size_t   kInventoryListData = 0x28;
		constexpr std::size_t   kArraySize = 0x00;
		constexpr std::size_t   kArrayCapacity = 0x04;
		constexpr std::size_t   kArrayData = 0x08;
		constexpr std::size_t   kItemStride = 0x28;
		constexpr std::size_t   kItemObject = 0x00;
		constexpr std::size_t   kItemInstanceData = 0x08;
		constexpr std::size_t   kItemFlags = 0x20;
		constexpr std::uint32_t kSlotMask = 0x7;
		constexpr std::uint8_t  kFormTypeWEAP = 0x30;
		constexpr std::size_t   kInstanceDataWeaponAmmoData = 0x20;
		constexpr std::size_t   kWeaponAmmoDataProjectile = 0x20;  // sweep-confirmed 2026-08-23, see ProjectileFlagProbe
		constexpr std::uint8_t  kFormTypePROJ = 0x3A;

		// See ProjectileFlagProbe.cpp for how these were found/confirmed
		// (raw-memory decoding, cross-checked against xEdit's own values
		// for a real mod weapon).
		constexpr std::size_t kProjectileData = 0x130;
		constexpr std::size_t kProjectileDataType = 0x84;
		// Same struct, same already-proven-writable memory as the type byte
		// above - offset taken from ProjectileFlagProbe.cpp, whose logged
		// value for this field has matched the weapon's real speed on every
		// sample (500.0 for standard ballistics, 120.0 for the rocket).
		constexpr std::size_t kProjectileDataSpeed = 0x50;

		// The value seen on both confirmed-real-projectile weapons tested
		// (rocket launcher, Shingen homing mod). Every tested hitscan
		// weapon read 0x02 instead.
		constexpr std::uint8_t kRealProjectileTypeValue = 0x00;

	}

	std::uint64_t ProjectileTypeOverride::ResolveEquippedProjectile(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return 0;
		}

		std::uint64_t invList = 0;
		if (!Read(a_actor, kInventoryList, invList) || !invList) {
			return 0;
		}

		std::uint32_t arraySize = 0;
		std::uint32_t arrayCapacity = 0;
		std::uint64_t arrayData = 0;
		if (!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArraySize, arraySize) ||
			!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArrayCapacity, arrayCapacity) ||
			!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArrayData, arrayData) ||
			arraySize == 0 || arrayCapacity < arraySize || !arrayData) {
			return 0;
		}

		std::uint64_t instanceData = 0;
		for (std::uint32_t i = 0; i < arraySize; ++i) {
			const std::uint64_t itemBase = arrayData + static_cast<std::uint64_t>(i) * kItemStride;

			std::uint64_t object = 0;
			if (!Read(reinterpret_cast<const void*>(itemBase), kItemObject, object) || !object) {
				continue;
			}
			std::uint8_t formType = 0;
			if (!Read(reinterpret_cast<const void*>(object), GameOffsets::kFormType, formType) || formType != kFormTypeWEAP) {
				continue;
			}
			std::uint32_t flags = 0;
			if (!Read(reinterpret_cast<const void*>(itemBase), kItemFlags, flags) || (flags & kSlotMask) == 0) {
				continue;  // not equipped
			}
			std::uint64_t itemInstanceData = 0;
			if (!Read(reinterpret_cast<const void*>(itemBase), kItemInstanceData, itemInstanceData) || !itemInstanceData) {
				continue;
			}
			instanceData = itemInstanceData;
			break;
		}

		if (!instanceData) {
			return 0;
		}

		std::uint64_t weaponAmmoData = 0;
		if (!Read(reinterpret_cast<const void*>(instanceData), kInstanceDataWeaponAmmoData, weaponAmmoData) || !weaponAmmoData) {
			return 0;
		}

		std::uint64_t projectile = 0;
		if (!Read(reinterpret_cast<const void*>(weaponAmmoData), kWeaponAmmoDataProjectile, projectile) || !projectile) {
			return 0;
		}

		std::uint8_t formType = 0;
		if (!Read(reinterpret_cast<const void*>(projectile), GameOffsets::kFormType, formType) || formType != kFormTypePROJ) {
			return 0;
		}

		return projectile;
	}

	namespace
	{
		// Reference-counted per projectile (2026-08-23) - AimAssist.cpp's
		// SteeringLoop now releases its "one steering thread at a time"
		// gate (s_steering) as soon as the button is released rather than
		// after its full post-release grace period, specifically so a
		// fast follow-up click isn't silently dropped (see AimAssist.cpp's
		// comment on kPostReleaseGrace). That means two SteeringLoop
		// instances can legitimately overlap in time now - the previous
		// hold's grace-period tail still running while a new hold has
		// already started. A plain single-token Engage/Disengage would
		// have the older thread's Disengage stomp the type back to
		// hitscan while the newer thread's hold is still relying on it
		// being real. Tracking a refcount per projectile pointer instead:
		// only the first Engage actually writes, only the Engage/Disengage
		// pair that brings the count back to zero restores the original
		// value.
		struct OverrideState
		{
			int          count{ 0 };
			std::uint8_t originalType{ 0 };
			float        originalSpeed{ 0.0f };
			bool         speedOverridden{ false };
		};

		std::mutex                                       s_mutex;
		std::unordered_map<std::uint64_t, OverrideState> s_refCounts;
	}

	ProjectileTypeOverride::Token ProjectileTypeOverride::Engage(RE::Actor* a_actor)
	{
		Token token;

		const std::uint64_t projectile = ResolveEquippedProjectile(a_actor);
		if (!projectile) {
			return token;
		}

		std::lock_guard<std::mutex> lock(s_mutex);

		auto it = s_refCounts.find(projectile);
		if (it == s_refCounts.end()) {
			std::uint8_t currentType = 0;
			if (!Read(reinterpret_cast<const void*>(projectile), kProjectileData + kProjectileDataType, currentType)) {
				return token;
			}
			if (currentType == kRealProjectileTypeValue) {
				// Already a real projectile (rocket/grenade/Shingen-like) -
				// ProjectileTracker already handles these, nothing to do,
				// and nothing to track (an untracked projectile is simply
				// never found by Disengage - see below). Deliberately also
				// skips the speed override below: these are the weapons the
				// redirect has always worked on, precisely because they're
				// already slow enough to home. Don't touch what works.
				return token;
			}
			if (!Write(reinterpret_cast<void*>(projectile), kProjectileData + kProjectileDataType, kRealProjectileTypeValue)) {
				return token;
			}

			OverrideState st;
			st.originalType = currentType;

			// Speed override (2026-08-25) - the fix for the real root cause
			// behind "the bullet just goes where I was looking". Measured
			// from a live session: a standard round travels at 500 m/s and
			// a typical engagement distance is ~6m, so the round covers the
			// whole distance within a SINGLE game frame. Since the engine
			// only updates a projectile once per frame, there is literally
			// no in-flight moment at which its trajectory can be rewritten,
			// no matter how fast this mod polls - which is exactly why the
			// redirect always worked on (slow) rockets and never on guns.
			// Forcing a much lower speed for the duration of the hold gives
			// the round real flight time to be homed. Restored on
			// Disengage together with the type byte.
			const float desiredSpeed = Settings::Get().lockedProjectileSpeed;
			float       currentSpeed = 0.0f;
			if (desiredSpeed > 0.0f &&
				Read(reinterpret_cast<const void*>(projectile), kProjectileData + kProjectileDataSpeed, currentSpeed) &&
				currentSpeed > desiredSpeed &&
				Write(reinterpret_cast<void*>(projectile), kProjectileData + kProjectileDataSpeed, desiredSpeed)) {
				st.originalSpeed = currentSpeed;
				st.speedOverridden = true;
			}

			it = s_refCounts.emplace(projectile, st).first;
			REX::INFO("[VATS] projtype: engaged, projectile=0x{:X} type 0x{:02X} -> 0x{:02X}, speed {:.1f} -> {:.1f} (overridden={})",
				projectile, currentType, kRealProjectileTypeValue, st.originalSpeed, desiredSpeed, st.speedOverridden);
		}

		++it->second.count;
		token.projectile = projectile;
		token.active = true;
		return token;
	}

	void ProjectileTypeOverride::Disengage(const Token& a_token)
	{
		if (!a_token.active || !a_token.projectile) {
			return;
		}

		std::lock_guard<std::mutex> lock(s_mutex);
		auto                        it = s_refCounts.find(a_token.projectile);
		if (it == s_refCounts.end()) {
			return;  // shouldn't happen, but nothing to restore if it does
		}
		if (--it->second.count > 0) {
			return;  // another overlapping hold still needs this to stay real
		}
		const OverrideState st = it->second;
		s_refCounts.erase(it);
		const bool wrote = Write(reinterpret_cast<void*>(a_token.projectile), kProjectileData + kProjectileDataType, st.originalType);
		bool       speedRestored = false;
		if (st.speedOverridden) {
			speedRestored = Write(reinterpret_cast<void*>(a_token.projectile), kProjectileData + kProjectileDataSpeed, st.originalSpeed);
		}
		REX::INFO("[VATS] projtype: disengaged, projectile=0x{:X} type -> 0x{:02X} (ok={}), speed -> {:.1f} (restored={})",
			a_token.projectile, st.originalType, wrote, st.originalSpeed, speedRestored);
	}
}
