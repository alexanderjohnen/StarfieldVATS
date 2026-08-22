#pragma once

#include <unordered_set>

namespace VATS
{
	// Bends a freshly-fired player projectile's own in-flight trajectory
	// toward (hit) or away from (miss) a_target, instead of nudging the
	// camera/weapon before the shot ever leaves the barrel. Real vanilla
	// ballistics/damage/hit-reactions/death still apply from there — this
	// only ever rewrites the round's own velocity/movementDirection a beat
	// after it spawns. Replaces the earlier camera-nudge AimAssist design
	// (SendMouseMove-based steering) 2026-08-22 per Alexander: the weapon
	// should never visibly snap onto the target at all, the round should
	// just curve onto it mid-flight.
	//
	// Call repeatedly (e.g. every AimAssist tick) for the duration of one
	// held trigger — a burst fires multiple rounds over time, not all at
	// once. a_handled is owned by the caller and should start as a fresh
	// empty set at the beginning of each hold; passing the same set back
	// in on every call lets each new round in the burst be found and
	// redirected independently, while guaranteeing the same round is never
	// redirected twice.
	//
	// Matches player-fired projectiles via RE::Projectile::shooterHandle
	// == 0x14 — the player reference's own persistent formID, which for a
	// persistent (non-dynamically-created) reference IS its
	// TESPointerHandle value directly, no resolution needed. Deliberately
	// avoids this fork's only mapped handle-resolution function
	// (BSPointerHandleManagerInterface::GetSmartPointer) — that one is
	// already on this project's own list of "mapped but crashed anyway"
	// engine calls (see commonlibsf-unmapped-ids memory), so a plain-data
	// comparison against the well-known player formID is strictly safer.
	//
	// The actual struct offsets (movementDirection@0x158, velocity@0x164,
	// shooterHandle@0x180, age@0x220) match
	// lib/commonlibsf/include/RE/P/Projectile.h exactly, which carries a
	// static_assert on the whole class's size (0x250) — much higher
	// confidence than a lone offsetof() claim (see
	// commonlibsf-unmapped-ids memory on why that distinction matters).
	//
	// Writes are NOT synchronized against the game's own simulation
	// thread (no BSSpinLock acquired — see the .cpp for why that tradeoff
	// was deliberate). All reads/writes go through SafeRead/SafeWrite
	// regardless, so a projectile despawning mid-operation degrades to
	// "skipped", never a crash.
	class ProjectileTracker
	{
	public:
		static void RedirectFreshProjectiles(RE::Actor* a_target, bool a_hit, std::unordered_set<std::uint64_t>& a_handled);
	};
}
