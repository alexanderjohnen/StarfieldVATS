#include "ProjectileFlagProbe.h"

#include "GameOffsets.h"
#include "SafeMem.h"

#include <cstdio>
#include <string>
#include <unordered_set>

namespace VATS
{
	namespace
	{
		template <class T>
		[[nodiscard]] bool Read(const void* a_base, std::size_t a_off, T& a_out)
		{
			return SafeRead(static_cast<const std::byte*>(a_base) + a_off, &a_out, sizeof(T));
		}

		constexpr std::uint8_t  kFormTypePROJ = 0x3A;  // RE::FormType::kPROJ (58)
		// Corrected 2026-08-23 via raw-memory decoding (hand-verified
		// IEEE-754 floats: flags/gravity/speed/range read as
		// 0x208/0.0/500.0/400.0 and 0x820C/0.0/1000.0/500.0 for two real
		// weapons - fully plausible ballistics, unlike the all-zero reads
		// at the header's claimed +0x128). The header's data offset was
		// wrong by a uniform +0x8 for this build - same "trust the data,
		// not the header" pattern as ProjectileTracker.cpp's -0x10 fix.
		constexpr std::size_t   kProjectileData = 0x130;  // RE::BGSProjectile::data
		constexpr std::size_t   kProjectileDataFlags = 0x48;  // RE::BGSProjectileData::flags (within data)
		constexpr std::size_t   kProjectileDataGravity = 0x4C;
		constexpr std::size_t   kProjectileDataSpeed = 0x50;
		constexpr std::size_t   kProjectileDataRange = 0x54;
		constexpr std::size_t   kProjectileDataType = 0x84;  // RE::BGSProjectileData::Type (1 byte)
		constexpr std::uint32_t kFlagHitScan = 1u << 0;

		// Sweeps every 8-byte-aligned qword in [0, a_size) from a_base,
		// treating each as a candidate pointer and checking whether it
		// dereferences to something with formType == kPROJ - the same
		// "sweep + cross-check formType" technique that found the real
		// BGSAimAssistModel offset in AimAssistProbe.cpp. Used below once
		// the header's claimed AMMO_DATA::projectile offset turned out to
		// hold an unresolved FormID (upper 4 bytes exactly zero, unlike
		// every real pointer seen elsewhere in this process) instead of a
		// live pointer.
		void SweepForProjectile(const char* a_label, std::uint64_t a_base, std::size_t a_size)
		{
			for (std::size_t off = 0; off < a_size; off += 8) {
				std::uint64_t candidate = 0;
				if (!Read(reinterpret_cast<const void*>(a_base), off, candidate) || candidate < 0x10000) {
					continue;  // null/tiny - not a plausible heap or module pointer
				}
				std::uint8_t formType = 0;
				if (Read(reinterpret_cast<const void*>(candidate), GameOffsets::kFormType, formType) && formType == kFormTypePROJ) {
					REX::INFO("[VATS] projflag sweep: {}+0x{:X} = 0x{:X} -> formType=0x3A (kPROJ) - CANDIDATE MATCH",
						a_label, off, candidate);
				}
			}
		}

		// Logs flags/gravity/speed/range for one resolved BGSProjectile
		// candidate. Needed because the sweep (2026-08-23) found THREE
		// distinct live BGSProjectile pointers reachable from the equipped
		// weapon (weaponAmmoData+0x20, weaponAmmoData+0x100, ammo+0x200),
		// not the single one CommonLibSF's header claims (AMMO_DATA::
		// projectile at ammo+0x1F8, which turned out to hold an unresolved
		// FormID, not a pointer - see LogCurrentWeaponProjectileFlags's raw
		// dump). weaponAmmoData+0x20 sits right after WeaponAmmoData::ammo
		// (+0x18) - the header calls that span "pad_0028", almost
		// certainly wrong, same pattern as the ammo offset itself.
		// weaponAmmoData+0x100 is a second, different pointer - possibly a
		// distinct "VATS variant" slot, given BGSProjectileData already has
		// its own vatsProjectile field (BGSProjectile.h) - unconfirmed,
		// logging both to compare their actual flags rather than assuming
		// which one the engine actually fires with.
		void LogProjectileCandidate(const char* a_label, std::uint64_t a_projectile)
		{
			std::uint8_t formType = 0;
			if (!Read(reinterpret_cast<const void*>(a_projectile), GameOffsets::kFormType, formType) || formType != kFormTypePROJ) {
				REX::INFO("[VATS] projflag candidate {}: 0x{:X} formType=0x{:02X} (not kPROJ, skipping)", a_label, a_projectile, formType);
				return;
			}
			std::uint32_t rawFlags = 0;
			float         gravity = 0.0f, speed = 0.0f, range = 0.0f;
			const bool    flagsRead = Read(reinterpret_cast<const void*>(a_projectile), kProjectileData + kProjectileDataFlags, rawFlags);
			(void)Read(reinterpret_cast<const void*>(a_projectile), kProjectileData + kProjectileDataGravity, gravity);
			(void)Read(reinterpret_cast<const void*>(a_projectile), kProjectileData + kProjectileDataSpeed, speed);
			(void)Read(reinterpret_cast<const void*>(a_projectile), kProjectileData + kProjectileDataRange, range);

			// Alexander's hypothesis (2026-08-23): with kHitScan/kSeeksTarget
			// showing up unset on literally every sample so far - including
			// an already-confirmed-real projectile (flags=0xA, speed=120,
			// range=15000, matching the rocket ProjectileTracker already
			// redirects) - flags may not be the real switch at all. Type
			// (relative +0x84, one byte - RE::BGSProjectileData::Type enum:
			// kMissile=1, kGrenade=2, kBeam=4, kFlamethrower=8, kCone=16,
			// kBarrier=32, kArrow=64) and the Shingen mod's "Seek Strength"
			// (xEdit-confirmed real field, 1.0 on the homing weapon - not
			// yet mapped to one of the header's four unnamed trailing
			// floats unk88/8C/90/94) are the next candidates. Logging Type
			// and all four unk floats so a future sample with kSeeksTarget
			// actually set (or a clearer real-vs-hitscan pair) can pin down
			// which one is Seek Strength.
			std::uint8_t type = 0;
			float        unk88 = 0.0f, unk8C = 0.0f, unk90 = 0.0f, unk94 = 0.0f;
			(void)Read(reinterpret_cast<const void*>(a_projectile), kProjectileData + kProjectileDataType, type);
			(void)Read(reinterpret_cast<const void*>(a_projectile), kProjectileData + 0x88, unk88);
			(void)Read(reinterpret_cast<const void*>(a_projectile), kProjectileData + 0x8C, unk8C);
			(void)Read(reinterpret_cast<const void*>(a_projectile), kProjectileData + 0x90, unk90);
			(void)Read(reinterpret_cast<const void*>(a_projectile), kProjectileData + 0x94, unk94);

			// Diagnostic (2026-08-23): flags/gravity/speed/range all read as
			// exactly zero for every candidate seen so far - four
			// independent fields all being simultaneously zero looks like
			// the same "wrong offset" signature as the earlier
			// AMMO_DATA::projectile mixup, not genuine game data (even a
			// real hitscan weapon's PROJ record should have a nonzero
			// range - it affects damage falloff). Dumps a wider raw range
			// around the header's claimed BGSProjectile::data (+0x128)
			// once per unique projectile pointer so the real offsets can
			// be found the same way movementDirection/velocity/age were
			// in ProjectileTracker.cpp.
			{
				static std::unordered_set<std::uint64_t> s_projDumped;
				if (s_projDumped.insert(a_projectile).second) {
					std::string hex;
					char        word[24];
					for (std::size_t off = 0x100; off < 0x1C0; off += 4) {
						std::uint32_t v = 0;
						if (Read(reinterpret_cast<const void*>(a_projectile), off, v)) {
							std::snprintf(word, sizeof(word), "%03zX:%08X ", off, v);
							hex += word;
						}
					}
					REX::INFO("[VATS] projflag proj raw dump {} projectile=0x{:X}: {}", a_label, a_projectile, hex);
				}
			}

			REX::INFO("[VATS] projflag candidate {}: projectile=0x{:X} flags=0x{:08X} (read={}) hitScan={} gravity={:.2f} speed={:.1f} range={:.1f} type=0x{:02X} unk88={:.3f} unk8C={:.3f} unk90={:.3f} unk94={:.3f}",
				a_label, a_projectile, rawFlags, flagsRead, flagsRead && (rawFlags & kFlagHitScan) != 0, gravity, speed, range, type, unk88, unk8C, unk90, unk94);
		}

		// Same equipped-weapon resolution as AimAssistProbe.cpp (see that
		// file for how each offset here was confirmed live 2026-08-22) -
		// duplicated rather than shared, matching this project's existing
		// per-file style (ProjectileTracker.cpp also keeps its own local
		// Read/Write helpers rather than a shared header).
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

		// New for this probe (2026-08-23) - see ProjectileFlagProbe.h for
		// the full chain and the WeaponAmmoData::ammo offset discrepancy.
		constexpr std::size_t  kInstanceDataWeaponAmmoData = 0x20;  // RE::TESObjectWEAPInstanceData::WeaponAmmoData
		constexpr std::size_t  kWeaponAmmoDataAmmo = 0x18;          // RE::WeaponAmmoData::ammo - static_assert-backed, NOT the header's stale "0x0020" inline comment
		// FormTypes.h's inline comments are hex without a "0x" prefix, not
		// decimal - misread once as decimal (0x1F instead of 0x31), which
		// made every cross-check fail even though the pointer chain was
		// actually resolving correctly. kAMMO's real value is 0x31.
		constexpr std::uint8_t kFormTypeAMMO = 0x31;  // RE::FormType::kAMMO

		// Sweep-confirmed 2026-08-23 (see SweepForProjectile above) - the
		// two most promising live BGSProjectile* candidates found within
		// WeaponAmmoData, replacing the broken ammo+0x1F8 (AMMO_DATA::
		// projectile, an unresolved FormID) path entirely.
		constexpr std::size_t kWeaponAmmoDataProjectile = 0x20;
		constexpr std::size_t kWeaponAmmoDataVatsProjectile = 0x100;
	}

	void ProjectileFlagProbe::LogCurrentWeaponProjectileFlags(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}

		std::uint64_t invList = 0;
		if (!Read(a_actor, kInventoryList, invList) || !invList) {
			return;
		}

		std::uint32_t arraySize = 0;
		std::uint32_t arrayCapacity = 0;
		std::uint64_t arrayData = 0;
		if (!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArraySize, arraySize) ||
			!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArrayCapacity, arrayCapacity) ||
			!Read(reinterpret_cast<const void*>(invList), kInventoryListData + kArrayData, arrayData) ||
			arraySize == 0 || arrayCapacity < arraySize || !arrayData) {
			return;
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
			return;
		}

		std::uint64_t weaponAmmoData = 0;
		if (!Read(reinterpret_cast<const void*>(instanceData), kInstanceDataWeaponAmmoData, weaponAmmoData) || !weaponAmmoData) {
			REX::INFO("[VATS] projflag: no WeaponAmmoData for equipped weapon (unarmed/melee?)");
			return;
		}

		std::uint64_t ammo = 0;
		if (!Read(reinterpret_cast<const void*>(weaponAmmoData), kWeaponAmmoDataAmmo, ammo) || !ammo) {
			REX::INFO("[VATS] projflag: WeaponAmmoData=0x{:X} but ammo pointer is null", weaponAmmoData);
			return;
		}

		std::uint8_t ammoFormType = 0;
		if (!Read(reinterpret_cast<const void*>(ammo), GameOffsets::kFormType, ammoFormType) || ammoFormType != kFormTypeAMMO) {
			REX::WARN("[VATS] projflag: cross-check failed - ammo=0x{:X} formType=0x{:02X}, expected kAMMO=0x{:02X} - chain may have shifted",
				ammo, ammoFormType, kFormTypeAMMO);
			return;
		}

		// One-time-per-ammo diagnostics (raw dump + sweep) - kept around in
		// case a future weapon/ammo type resolves differently than the two
		// candidates below, not because we still doubt those two.
		{
			static std::unordered_set<std::uint64_t> s_dumped;
			if (s_dumped.insert(ammo).second) {
				std::string hex;
				char        word[24];
				for (std::size_t off = 0x1E0; off < 0x220; off += 4) {
					std::uint32_t v = 0;
					if (Read(reinterpret_cast<const void*>(ammo), off, v)) {
						std::snprintf(word, sizeof(word), "%03zX:%08X ", off, v);
						hex += word;
					}
				}
				REX::INFO("[VATS] projflag raw dump ammo=0x{:X}: {}", ammo, hex);
				SweepForProjectile("ammo", ammo, 0x240);
				SweepForProjectile("weaponAmmoData", weaponAmmoData, 0x184);
			}
		}

		// The two sweep-confirmed candidates (2026-08-23) - logged every
		// shot so we can compare their hitScan/speed/gravity/range across
		// different equipped weapons, not just once.
		std::uint64_t normalProjectile = 0;
		std::uint64_t vatsProjectile = 0;
		if (Read(reinterpret_cast<const void*>(weaponAmmoData), kWeaponAmmoDataProjectile, normalProjectile) && normalProjectile) {
			LogProjectileCandidate("weaponAmmoData+0x20 (normal?)", normalProjectile);
		}
		if (Read(reinterpret_cast<const void*>(weaponAmmoData), kWeaponAmmoDataVatsProjectile, vatsProjectile) && vatsProjectile) {
			LogProjectileCandidate("weaponAmmoData+0x100 (vats-variant?)", vatsProjectile);
		}
	}
}
