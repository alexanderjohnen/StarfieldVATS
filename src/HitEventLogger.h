#pragma once

namespace VATS
{
	// Ground-truth hit confirmation (2026-08-23) - everything in the
	// hitscan/type-override investigation so far has only confirmed that
	// we WROTE a plausible redirect to a projectile, never that it
	// actually connected. Alexander reported a shot that visually went
	// dead straight (not the jittered near-miss shape a rolled MISS
	// produces) despite the redirect log looking normal - several
	// candidate causes have already been found and fixed (the decorative
	// kPBEA laser-sight beam, dropped fast-click follow-ups, the post-
	// release grace window), but "some shots still just go straight" is
	// still happening, so guessing at more one-off causes isn't
	// productive anymore. This sinks RE::TESHitEvent - the game's own
	// native, already-mapped "a hit was just registered" event, carrying
	// the real target/aggressor/limb/material - and logs any hit against
	// whatever actor is currently Locked, so redirect attempts and actual
	// confirmed hits can be cross-referenced by timestamp instead of by
	// eye.
	//
	// First real engine function call in this project that isn't a raw
	// hand-cast REL::ID (RegisterSink/UnregisterSink, via CommonLibSF's
	// properly-typed BSTEventSource<T> wrapper) - a step up from every
	// prior plain-data probe/write. Flagged honestly per this project's
	// "mapped != safe" rule, but this specific call is about as battle-
	// tested as anything in the whole SKSE/F4SE/SFSE ecosystem - it's the
	// standard, universal way virtually every gameplay mod learns "did I
	// just hit something," unlike the exotic detection/raycast calls that
	// caused every crash this project has had so far.
	class HitEventLogger
	{
	public:
		static void Start();
	};
}
