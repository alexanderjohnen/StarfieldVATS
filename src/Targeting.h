#pragma once

namespace VATS
{
	struct TargetPick
	{
		RE::NiPointer<RE::Actor> actor;
		float                    worldDistance{ 0.0f };  // camera to actor, world units
		float                    angleDeg{ 0.0f };       // angle off the camera view axis
	};

	// Scans the player's parent cell for actors and returns the one closest
	// to the camera's view axis, if any fall within a_maxRange world units
	// and a_maxConeDeg degrees of the view direction.
	//
	// History (four crashes before this worked, see feedback memory
	// commonlibsf-unmapped-ids and the starfield-vats-mod-design project
	// notes for 2026-08-22):
	//  1. Screen-projection via RE::Main::GetWorldRootCamera() — unmapped ID.
	//  2. ProcessLists actor handles + BSPointerHandle::get() — mapped ID,
	//     crashed anyway (engine-side null deref).
	//  3. TESObjectCELL::ForEachReference — its internal LockRead() is also
	//     REL::ID-backed, mapped, crashed anyway.
	//  4. Direct read of CommonLibSF's `TESObjectCELL::references` field —
	//     the header's offsetof() computed 0x88 for this field on the
	//     current (2026-08-19) library snapshot, but empirical probing
	//     (a diagnostic probe, since removed - it matched every entry against the proven-correct parentCell
	//     backpointer) found the real array at 0x80 on game 1.16.244,
	//     confirmed independently in two unrelated cells (ship interior +
	//     exterior settlement, both 4/4 backpointer matches).
	//
	// No crash after that, but zero targets found for two more rounds:
	//  5. `RE::Actor::BOOL_BITS::kDead` is `1 << 11`, not `1 << 3` — a plain
	//     copy mistake — so every actor was misread as dead and rejected.
	//  6. The camera-forward axis was assumed to be row 0 of the rotation
	//     matrix (matching NiCamera::WorldToScreen's depth-term
	//     convention), but empirically gave ~90 deg to a dead-center
	//     target. Row 1 turned out to be forward (dot ~0.99 against the
	//     measured direction to target, in two independent tests); row 0
	//     behaves like a lateral axis here (zero Z component). Whatever
	//     convention WorldToScreen relies on does not carry over to this
	//     rotation matrix the way it was assumed to.
	// Lesson applied here: no struct-offset trust without either (a) an
	// offset that's been exercised successfully many times already
	// (PlayerCharacter/PlayerCamera singletons, TESObjectREFR::data.location,
	// TESObjectREFR::parentCell — the last one cross-validated by the crash
	// logger's own independent RTTI annotations) or (b) empirical probing
	// like above. And even then: every read of memory reached through an
	// array entry pointer goes through SafeRead (SEH-guarded), so a still-
	// wrong offset degrades to "no target found", never a crash.
	//
	// Known limitation: only the player's own cell is scanned, so in
	// exteriors actors in neighboring grid cells are not yet considered.
	// a_exclude is skipped entirely, and a_requireAlive additionally
	// demands a real health reading above zero. Both exist for
	// auto-advancing to the next enemy when a locked target dies with VATS
	// capacity left.
	//
	// a_requireAlive is not redundant with the scan's own dead filter -
	// that filter is a confirmed no-op. It tests Actor::boolBits against
	// CommonLibSF's BOOL_BITS::kDead, and that bit reads identically for
	// living and long-dead actors in this game (2026-08-25, see the death
	// check in Overlay.cpp). Left to it, an auto-advance would almost
	// always re-lock the corpse the player just made, that being by
	// definition the thing nearest their crosshair. Real health is only
	// checkable at all since live health started reading correctly
	// (HealthReader.h), and it is read only for a candidate that has
	// already beaten every previous one on angle - a handful of actors per
	// scan, not every reference in the cell.
	// a_requireEngagedWithPlayer additionally demands that the actor's own
	// currentCombatTarget is the player. Used only by auto-advance, as a
	// stand-in for the line-of-sight test this project does not have: it is
	// not real occlusion, but an actor actively shooting at the player is
	// rarely behind a wall, and one in the next room who has not noticed
	// them is excluded - which is the case that made the unrestricted scan
	// unusable. Must never be applied to ordinary acquisition, or opening a
	// fight from stealth becomes impossible. See the depth-buffer proposal
	// in HANDOFF.md for the real fix.
	[[nodiscard]] std::optional<TargetPick> FindNearestActorToCrosshair(
		float       a_maxRange,
		float       a_maxConeDeg,
		RE::Actor*  a_exclude = nullptr,
		bool        a_requireAlive = false,
		bool        a_requireEngagedWithPlayer = false,
		bool        a_requireOnScreen = false);

	// Reads PlayerCharacter's crosshair-activation target directly (the
	// same value that drives vanilla "TALK E"/loot prompts) instead of
	// scanning the cell and picking by angle. Investigated + probed
	// 2026-08-22 (see starfield-vats-ui-hook memory): a plain data read
	// (GameOffsets::kPlayerCommandTarget), no raycasting of our own.
	// Confirmed via in-game telemetry to populate for both talkable NPCs
	// and hostile combat actors, and to correctly clear to null when
	// nothing is directly under the crosshair — which as a side effect
	// respects real geometry occlusion for free (the game won't set an
	// activation target through a wall), unlike the cone scan above.
	// Returns null if nothing is targeted, if the target isn't a living
	// ACHR actor, or if any read fails (SafeRead-guarded throughout).
	[[nodiscard]] RE::NiPointer<RE::Actor> GetCrosshairActivationTarget();

	// The mirror image: the same engine-computed crosshair pick, but
	// returning ONLY a live player teammate - the actor the combat path
	// deliberately discards. This is what the support actions target.
	//
	// Independent of bIgnoreFriendlyActors and bRequireHostileTarget on
	// purpose: whether someone is your companion is not a combat-tuning
	// preference.
	[[nodiscard]] RE::NiPointer<RE::Actor> GetCrosshairTeammate();

	// Live line-of-sight check between two actors, using the game's own
	// AI/stealth detection LOS system (the same one that decides whether
	// an NPC can see the player, just usable in either direction). Solves
	// the "is my Locked target currently behind a wall" problem that
	// GetCrosshairActivationTarget can't (that one's tied to the
	// crosshair; once Locked, the target may not be under it at all
	// anymore) — see starfield-vats-mod-design memory, 2026-08-22.
	//
	// A raw, hand-cast function-pointer call at a bare Address Library ID
	// (REL::ID(170456), no CommonLibSF header wrapper) rather than
	// anything from CommonLibSF's own IDs.h — sourced from Cassiopeia
	// Papyrus Extender's published, working source
	// (github.com/D7ry/CassiopeiaSF or similar; local copy under
	// CassiopeiaSource/ at the time this was written), which uses the
	// exact same CommonLibSF/SFSE base as this project (confirmed by
	// checking its PCH). Real-world validated by an actively-used mod,
	// unlike every other engine-function lead this project has chased —
	// still worth an in-game sanity check (does it actually flip false
	// behind real geometry?) before leaning on it for anything beyond
	// hit-chance cosmetics.
	[[nodiscard]] bool HasDetectionLOS(RE::Actor* a_source, RE::Actor* a_target);
}
