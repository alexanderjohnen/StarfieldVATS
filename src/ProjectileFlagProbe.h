#pragma once

namespace VATS
{
	// Explores whether a weapon's hitscan-vs-real-projectile behavior can be
	// flipped at *runtime* (Alexander's idea, 2026-08-23): if a normally-
	// hitscan weapon can be forced to fire a real RE::Projectile while VATS
	// is Locked, the already-working ProjectileTracker redirect (see that
	// file) would apply to every weapon, sidestepping the entire
	// RangedAttackModule/RangedAimAssistImpl disassembly problem (see
	// HANDOFF.md's "THE BIG OPEN PROBLEM") rather than solving it.
	//
	// Unlike RangedAttackModule/RangedAimAssistImpl (RTTI-only, no header,
	// would need real disassembly), the data this needs IS fully typed in
	// CommonLibSF - a plain-data pointer chain, the project's established
	// low-risk category:
	//   Actor::inventoryList (0xA0) -> equipped weapon's live instance data
	//     (same confirmed-live resolution AimAssistProbe already uses)
	//     -> TESObjectWEAPInstanceData::WeaponAmmoData (+0x20)
	//       -> WeaponAmmoData::ammo (TESAmmo*, +0x18 - NOT the +0x20 the
	//          header's inline comment claims; the struct's own
	//          pad_0000[24] byte count and its static_assert both say
	//          0x18, the comment is simply stale/wrong - same class of
	//          header inconsistency this project has hit before)
	//         -> TESAmmo::data.projectile (BGSProjectile*, +0x1F8)
	//           -> BGSProjectile::data.flags (+0x128+0x48 = +0x170,
	//              REX::TEnumSet<BGSProjectileFlags> - kHitScan = 1<<0)
	//
	// This class is READ-ONLY - logs the resolved chain and the current
	// flags/speed/gravity/range for the equipped weapon's ammo, but writes
	// nothing. Purpose: confirm the chain resolves to sane data (formType
	// cross-checks at each hop, same technique that found the real
	// BGSAimAssistModel offset) before ever attempting to flip the
	// kHitScan bit. A write-capable follow-up only happens once this
	// probe's output is verified in-game.
	class ProjectileFlagProbe
	{
	public:
		static void LogCurrentWeaponProjectileFlags(RE::Actor* a_actor);
	};
}
