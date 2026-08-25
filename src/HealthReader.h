#pragma once

namespace VATS
{
	struct HealthReading
	{
		float current{ 0.0f };
		float max{ 0.0f };
	};

	// Reads an actor's current/max health as plain data (Actor::avStorage:
	// base value + all three ACTOR_VALUE_MODIFIER entries), not through the
	// ActorValueOwner virtual interface — RE::Actor's CommonLibSF header
	// does not list ActorValueOwner as a base class, so there's no vtable
	// slot to call safely. avStorage itself and the BSTArray/BSTTuple
	// layouts it's built from are typed, static_assert-guaranteed header
	// members (RE/A/ActorValueStorage.h, RE/B/BSTArray.h) - the only
	// genuinely new-to-this-project pieces are (a) Actor's real in-memory
	// avStorage offset actually matching the header, which depends on every
	// base class ahead of it in Actor's long multiple-inheritance chain
	// being the right size (same class of risk as the BGSProjectile
	// systemic-shift bug this project already hit once), and (b)
	// RE::ActorValue::GetSingleton() being correctly mapped for 1.16.244 (a
	// REL::ID call, first use in this project, though an extremely
	// fundamental/ubiquitous one). Cross-check the logged current/max
	// against the console's `getav health` on the same actor before relying
	// on this beyond cosmetic HUD display.
	//
	// Restored 2026-08-25 (was deleted in df6797a in favor of chasing
	// Starfield's native health bar via CombatTargetOverride, which then
	// also failed - see HANDOFF.md/starfield-vats-mod-design memory). This
	// data path itself was never disproven: it matched console `getav
	// health` exactly once (2026-08-23) and reads baseValues live per shot,
	// but nobody ever confirmed on the *visible bar* whether `current`
	// genuinely changes over a real fight or stays stuck - see the
	// GetActorHealth diagnostic log line, which answers exactly that on the
	// next in-game test.
	//
	// Returns false if the health ActorValueInfo couldn't be resolved, or
	// the actor has no baseValues entry for it (avStorage is a sparse map -
	// only actor values actually touched for this actor ever appear in it,
	// though health should always have been touched by the time an actor is
	// alive in the world).
	[[nodiscard]] bool GetActorHealth(RE::Actor* a_actor, HealthReading& a_out);

	// Best-effort: legendaryRank is a guess at what drives Starfield's
	// segmented boss/legendary enemy health display (a white "currently
	// active" bar that must be fully depleted before the next of N reserve
	// bars, shown smaller/red, becomes the active one — confirmed via
	// screenshot + Alexander's description, not through any CommonLibSF
	// documentation). Each rank beyond 0 is assumed to mean one extra full
	// health pool held in reserve, refilled natively by the game each time
	// the active one drains — this function only reports the count, it
	// doesn't simulate the refill itself (the game already does that; we
	// just re-read current/max each frame same as GetActorHealth).
	//
	// Returns 0 (no extra segments) if the AV can't be resolved or isn't
	// present on this actor — degrades to "just draw the plain bar", the
	// correct behavior for the vast majority of non-legendary enemies
	// regardless of whether the guess is right.
	[[nodiscard]] std::uint32_t GetActorExtraHealthSegments(RE::Actor* a_actor);

	// Diagnostic (2026-08-25) - does NOT feed the HUD. Both prior
	// theories for live current health are now disproven with hard
	// evidence: avStorage.baseValues reads once at full health and then
	// genuinely never changes across a real fight (likely MAX health, not
	// current - both happen to be equal at full HP, which is why the
	// original getav cross-check looked like confirmation), and
	// avStorage.modifiers has no health entry at all (confirmed via a raw
	// byte dump - the 24-byte-stride parse is clean and self-consistent
	// across 20 real entries, healthInfo just never appears as a key).
	// Live current health must live somewhere else entirely, outside the
	// AV system. Scans a byte window of the Actor object itself
	// (avStorage sits at a confirmed 0x260, this covers well past it) for
	// any 4-byte float that DECREASES between two calls while staying in
	// a plausible HP range - the same raw-memory-diffing technique that
	// originally found ProjectileTracker's real offsets. Throttled
	// internally; safe/cheap to call every frame for the Locked target.
	void ScanForLiveHealthCandidates(RE::Actor* a_actor);
}
