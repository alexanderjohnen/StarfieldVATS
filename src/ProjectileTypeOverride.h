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
	//
	// Reference-counted per projectile pointer (2026-08-23) - two holds
	// can legitimately overlap now that AimAssist.cpp releases its
	// single-steering-thread gate as soon as the button is released
	// rather than after the post-release grace period finishes (see that
	// file). Only the first Engage for a given projectile writes; only
	// the Disengage that brings the count back to zero restores it.
	class ProjectileTypeOverride
	{
	public:
		// The value Engage() force-writes on a newly-engaged projectile -
		// exposed so a caller logging Token::newlyEngaged elsewhere (e.g.
		// AimAssist.cpp's SteeringLoop) doesn't need to hardcode it.
		static constexpr std::uint8_t kRealProjectileTypeValue = 0x00;

		struct Token
		{
			std::uint64_t projectile = 0;
			bool          active = false;

			// Set only when this Engage() call was the one that actually
			// flipped the type byte (first Engage for this projectile) -
			// not set for a call that only bumped an existing refcount, or
			// one that found nothing to do (already-real projectile,
			// resolve failure). Engage() itself does no logging (see
			// below) so a caller running somewhere logging isn't safe -
			// e.g. AimAssist.cpp's low-level mouse hook, see
			// BackKeyInterceptor.cpp for why - can defer the "engaged"
			// log line using these two fields once it's back on a normal
			// thread.
			bool          newlyEngaged = false;
			std::uint8_t  originalType = 0;
		};

		// Call once at the start of a held trigger (before the burst's
		// first shot). Returns an inactive Token if nothing needed
		// changing (weapon already real, chain didn't resolve, etc.) -
		// always safe to pass the result to Disengage regardless.
		//
		// Deliberately does no logging (restored 2026-08-25) - this is now
		// called synchronously from AimAssist.cpp's WM_LBUTTONDOWN hook
		// callback, before the SteeringLoop thread is even spawned, to
		// close a race where a fast/semi-auto shot's native hitscan
		// resolves before a freshly-spawned thread gets scheduled (see
		// HANDOFF.md's timing theory). Only SafeRead/SafeWrite + a mutex
		// lock happen here - cheap, non-blocking, same class of operation
		// BackKeyInterceptor.cpp already treats as hook-safe. REX::INFO
		// does blocking file I/O and must never run inside a low-level
		// hook (BackKeyInterceptor.cpp hit this once already) - use
		// Token::newlyEngaged/originalType to log from SteeringLoop's own
		// thread instead.
		static Token Engage(RE::Actor* a_actor);

		// Call once when the hold ends, with the Token Engage returned.
		// No-op if the token isn't active.
		static void Disengage(const Token& a_token);
	};
}
