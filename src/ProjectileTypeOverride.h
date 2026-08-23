#pragma once

namespace VATS
{
	// First actual behavior-changing write in the hitscan/native-aim-assist
	// investigation (see HANDOFF.md's "THE BIG OPEN PROBLEM") - everything
	// before this (AimAssistProbe, ProjectileFlagProbe) only read and
	// logged data.
	//
	// Confirmed 2026-08-23 via ProjectileFlagProbe: a byte at
	// BGSProjectile::data+0x84 (the header calls it "Type", though the
	// runtime meaning may not match the header's Type enum 1:1 - see
	// below) reads 0x00 on both confirmed-real-projectile weapons tested
	// (the rocket launcher, and a Nexus mod's homing-bullet weapon
	// "Shingen", inspected via xEdit) and reads 0x02 on all seven tested
	// hitscan weapons, no exceptions. Not what Alexander's original
	// "Type=Missile" hypothesis predicted (that would be 0x01), but the
	// empirical correlation is clean regardless of what the byte is
	// officially called.
	//
	// This class force-writes 0x00 into that byte on the currently
	// equipped weapon's live, resolved projectile (WeaponAmmoData+0x20 -
	// see ProjectileFlagProbe.h for the full confirmed chain) for the
	// duration of one held trigger, then restores the original value.
	// Untested whether this alone is sufficient to make the engine
	// actually spawn a real RE::Projectile for a normally-hitscan weapon -
	// that's exactly what this test is for. If it works, ProjectileTracker
	// (already confirmed working for rockets/grenades) should be able to
	// find and redirect the newly-real round with no further changes.
	//
	// The underlying BGSProjectile is a shared, global form - not per-
	// actor - so this write affects every actor using the same
	// weapon+ammo combination for as long as it's engaged, including
	// NPCs. Scoped as tightly as possible (engaged only for the duration
	// of one held trigger, disengaged the instant the hold ends) to
	// minimize that window, same tradeoff already accepted for
	// AimAssistProbe's aimAssistEnabled write.
	class ProjectileTypeOverride
	{
	public:
		struct Token
		{
			std::uint64_t projectile = 0;
			std::uint8_t  originalType = 0;
			bool          active = false;
		};

		// Call once at the start of a held trigger (before the burst's
		// first shot). Returns an inactive Token if nothing needed
		// changing (weapon already real, chain didn't resolve, etc.) -
		// always safe to pass the result to Disengage regardless.
		static Token Engage(RE::Actor* a_actor);

		// Call once when the hold ends, with the Token Engage returned.
		// No-op if the token isn't active.
		static void Disengage(const Token& a_token);
	};
}
