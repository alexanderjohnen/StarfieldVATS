#pragma once

namespace VATS
{
	struct HealthReading
	{
		float current{ 0.0f };
		float max{ 0.0f };
	};

	// Reads a target's health for the HUD bar and the death check.
	//
	// `max` comes from Actor::avStorage.baseValues, read raw at a 16-byte
	// stride with an 8-byte ActorValueInfo* key rather than through the
	// header's BSTTuple<uint32_t, float> (whose claimed 4-byte key produces
	// an obviously corrupt sequence in memory). Note it really is MAX, not
	// current: it reads correct at full health and then never moves again
	// for the rest of a fight, which masqueraded as a working "current"
	// read for days precisely because the two are equal at full HP - which
	// is exactly when it kept getting cross-checked.
	//
	// `current` comes from ActorValueOwner::GetActorValue - the same engine
	// accessor behind the console's `getav` and Papyrus' Actor.GetValue.
	// See ActorValueProbe.h for why that is the right source, how the
	// sub-object is located, and why it does not fall into this project's
	// REL::ID crash category. Confirmed in-game 2026-08-25 tracking a real
	// kill: 345.00 -> 218.84 -> ... -> 1.12 -> -13.93 (negative on overkill,
	// which is what the death check keys off).
	//
	// Returns false if the health ActorValueInfo can't be resolved or the
	// actor has no baseValues entry for it. If only the live call fails,
	// this still succeeds with current == max, so the bar degrades to
	// "always full" rather than vanishing.
	[[nodiscard]] bool GetActorHealth(RE::Actor* a_actor, HealthReading& a_out);
}
