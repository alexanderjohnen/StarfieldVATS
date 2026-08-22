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

	// UNVERIFIED (2026-08-22) — needed for the boostpack aim point, which
	// has to sit "behind" the target's spine and therefore needs a facing
	// direction, unlike every other aim point so far (all were plain
	// vertical offsets from a fixed position). `data.angle` sits alongside
	// `data.location` in the same TESObjectREFR::ObjectRefData struct that
	// kLocation already reads reliably — same struct family, but the
	// specific convention (which axis is yaw, sign/rotation direction) has
	// not been confirmed in-game the way location has. Treat any pack-
	// targeting result as suspect until Alexander confirms the aim point
	// actually lands behind (not in front of/beside) a test target.
	inline constexpr auto kAngle = offsetof(RE::TESObjectREFR, data.angle);

	// Body-part aim-point offsets (2026-08-22) — a deliberately simplified
	// 3-point stand-in for full skeletal body-part targeting (never built).
	// All eyeballed for a roughly humanoid actor, same spirit/precision as
	// the original single chest point this generalizes. World units.
	inline constexpr float kBodyPartSuitZ = 1.2f;    // == the original single chest-point offset
	inline constexpr float kBodyPartHelmetZ = 1.7f;  // head height
	inline constexpr float kBodyPartPackZ = 1.3f;    // upper-back height
	inline constexpr float kBodyPartPackBackDistance = 0.35f;  // world units behind the spine, along -forward
}
