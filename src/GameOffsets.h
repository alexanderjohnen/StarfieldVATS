#pragma once

// Centralized, in-game-verified struct offsets and constants shared between
// targeting and UI. Everything here has either been empirically probed
// (diagnostic probes, since removed - see docs/FINDINGS.md) or proven by repeated successful use on
// game 1.16.244. Do not add offsets here on header trust alone — see the
// commonlibsf-unmapped-ids notes: CommonLibSF's layouts have been wrong
// before (cell references off by 8), and a mapped/compiling offset is not
// a working offset.
namespace VATS::GameOffsets
{
	// TESObjectCELL: {u32 size, u32 capacity, TESObjectREFR** data} array.
	// Header's offsetof says 0x88; empirically probed to 0x80 (two cells,
	// 4/4 parentCell backpointer matches each).
	inline constexpr std::size_t kCellReferences = 0x80;

	// Verified via probe dumps (sane formIDs/types) and stage-1 testing.
	inline constexpr auto kFormType = offsetof(RE::TESForm, formType);
	inline constexpr auto kLocation = offsetof(RE::TESObjectREFR, data.location);
	inline constexpr auto kBoolBits = offsetof(RE::Actor, boolBits);

	inline constexpr std::uint8_t  kFormTypeACHR = 75;   // matches crash logger's independent RTTI labeling
	// UNUSED for death detection since 2026-08-25 - this bit reads
	// identically on a living actor and one lying dead on the floor, so it
	// never distinguished anything. Death is taken from health instead
	// (HealthReader.h). Kept only so the old value stays documented.
	inline constexpr std::uint32_t kActorDeadBit = 1u << 11;  // RE::Actor::BOOL_BITS::kDead

	// VERIFIED BY MEASUREMENT 2026-08-25, unlike kActorDeadBit above. A
	// companion and an enemy probed back to back read boolBits 0x162021A2
	// and 0x122021A2 - differing in exactly this one bit, set on the
	// teammate and clear on the enemy, which is also the bit the header
	// names kPlayerTeammate.
	inline constexpr std::uint32_t kActorPlayerTeammateBit = 1u << 26;  // RE::Actor::BOOL_BITS::kPlayerTeammate

	// The player's own reference handle, as it appears in other actors'
	// currentCombatTarget. Empirical: an enemy actively fighting the player
	// read 1 there while the player's form ID is 0x14, so the field holds a
	// handle rather than a form ID - consistent with this project's
	// projectile logs, where player-fired rounds carry shooterHandle=1.
	inline constexpr std::uint32_t kPlayerHandle = 1;

	// UNVERIFIED — candidate for a real occlusion-respecting target source
	// (2026-08-22 investigation), not yet trusted. CommonLibSF's
	// PlayerCharacter.h names this `commandTarget` ("0F90 - crosshair
	// target") but every surrounding field is an unmapped `unk0Fxx` — same
	// "compiling but unverified" risk as every other header offset in this
	// project. If it holds up in-game, it would give us the actor currently
	// under the crosshair as already resolved by the game's own activation
	// raycast (same mechanism as the "TALK E" prompt), which respects real
	// geometry for free — no Havok/NiPick raycasting needed. See
	// starfield-vats-mod-design memory for why that matters (LOS-gated
	// targeting, hit-chance visibility factor). Probe via Overlay.cpp before
	// wiring into Targeting.cpp.
	inline constexpr std::size_t kPlayerCommandTarget = 0x0F90;

	// UNVERIFIED — candidate native field for "who Starfield's own combat/
	// HUD systems currently consider the actor's target" (2026-08-24,
	// Alexander's idea): if this drives EnemyHealthMeter.as's
	// uTargetUnderCrosshairID (confirmed via the JPEXS decompile to be the
	// key the native health-bar widget matches against — see
	// docs/hudmenu-decompiled/scripts/EnemyHealthMeter.as), writing to it
	// while VATS is Locked could make Starfield's own native enemy health
	// bar show up on our target for free — no Scaleform involved at all. A
	// proper named field in CommonLibSF's Actor.h (not a raw hex guess like
	// kPlayerCommandTarget above), but the field existing doesn't mean the
	// theory is right — read-only probe first (Overlay.cpp), confirm
	// against a real ADS test before ever writing to it. Resolving the
	// TESPointerHandle via BSPointerHandleManagerInterface::GetSmartPointer
	// is a CONFIRMED CRASH in this exact project (commonlibsf-unmapped-ids
	// memory) — do not call it. Compare the raw value directly against a
	// known actor's GetFormID() instead (FO4/Starfield-era handles are
	// commonly understood to just be the FormID for persistent-form
	// references — unconfirmed for 1.16.244, that's what the probe is for).
	inline constexpr auto kCurrentCombatTarget = offsetof(RE::Actor, currentCombatTarget);

	// UNVERIFIED — candidate offset chain for a pose- and creature-type-
	// agnostic aim point (2026-08-25, Alexander's requirement: must find an
	// actor's real center regardless of whether it's standing, kneeling,
	// prone, or not even humanoid - robots, alien creatures). Every field
	// below is `offsetof` on a plain, public struct member (not a hex
	// guess), but that only proves it compiles against CommonLibSF's
	// claimed layout, not that the layout is right for 1.16.244 - this
	// project has been burned by a "compiles fine, wrong at runtime" header
	// offset before (avStorage's stride bug). Read-only probe first
	// (WorldBoundProbe.h), same discipline as every other offset here.
	//
	// The idea: RE::NiAVObject::worldBound is a generic engine feature (a
	// culling bounding sphere, present on every renderable 3D object since
	// Oblivion-era Gamebryo/NetImmerse) recomputed every frame from the
	// object's ACTUAL current geometry - not a fixed offset, not a named
	// bone, so it should track crouching/prone poses for free and apply
	// identically to any skeleton (human, robot, alien) since it carries no
	// creature-specific assumption at all.
	//
	// Chain: Actor + kActorLoadedData -> LOADED_REF_DATA* (the raw pointer
	// value of a BSGuarded<LOADED_REF_DATA*, BSReadWriteLock>, assumed at
	// its own offset 0 - CommonLibSF marks BSGuarded's own member offsets
	// "??", i.e. also unconfirmed) -> + kLoadedRefData3D -> NiAVObject*
	// (the raw pointer of a NiPointer<NiAVObject>, same offset-0
	// assumption, standard for this style of intrusive smart pointer) ->
	// + kNiAVObjectWorldBound -> NiBound {NiPoint3 center; float radius}.
	inline constexpr auto kActorLoadedData = offsetof(RE::TESObjectREFR, loadedData);
	inline constexpr auto kLoadedRefData3D = offsetof(RE::LOADED_REF_DATA, data3D);
	// NiAVObject::parent, header-documented at 0x038. Used only as a
	// back-reference when probing for the children array offset
	// (the removed BoneProbe, see docs/FINDINGS.md): a candidate array is accepted only if its first child
	// points back at the node it came from. If this is wrong the probe
	// finds nothing - it can never make it accept something bad.
	inline constexpr std::size_t kNiAVObjectParent = 0x038;

	inline constexpr auto kNiAVObjectWorldBound = offsetof(RE::NiAVObject, worldBound);

	// Single aim point, world units above an actor's ref-origin (feet) —
	// roughly chest height. The Suit/Helmet/Pack body-part system (and the
	// `data.angle`-based "behind the spine" pack offset it needed) was
	// removed 2026-08-22 — deliberately simplified stand-in for full
	// skeletal targeting, never built, and Alexander wants it dropped for
	// now while a projectile-redirect approach is explored instead. May
	// come back later; see starfield-vats-mod-design memory.
	inline constexpr float kAimPointChestZ = 1.2f;
}
