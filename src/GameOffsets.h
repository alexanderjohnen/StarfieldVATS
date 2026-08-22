#pragma once

// Centralized, in-game-verified struct offsets and constants shared between
// targeting and UI. Everything here has either been empirically probed
// (CellProbe / diagnostic logging) or proven by repeated successful use on
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
	inline constexpr std::uint32_t kActorDeadBit = 1u << 11;  // RE::Actor::BOOL_BITS::kDead

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

	// Single aim point, world units above an actor's ref-origin (feet) —
	// roughly chest height. The Suit/Helmet/Pack body-part system (and the
	// `data.angle`-based "behind the spine" pack offset it needed) was
	// removed 2026-08-22 — deliberately simplified stand-in for full
	// skeletal targeting, never built, and Alexander wants it dropped for
	// now while a projectile-redirect approach is explored instead. May
	// come back later; see starfield-vats-mod-design memory.
	inline constexpr float kAimPointChestZ = 1.2f;
}
