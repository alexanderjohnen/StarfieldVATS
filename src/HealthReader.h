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
	// Returns false if the health ActorValueInfo couldn't be resolved, or
	// the actor has no baseValues entry for it (avStorage is a sparse map -
	// only actor values actually touched for this actor ever appear in it,
	// though health should always have been touched by the time an actor is
	// alive in the world).
	[[nodiscard]] bool GetActorHealth(RE::Actor* a_actor, HealthReading& a_out);
}
