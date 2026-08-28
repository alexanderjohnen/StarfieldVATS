#pragma once

#include <mutex>

#include "RE/N/NiSmartPointer.h"

namespace RE
{
	class Actor;
}

namespace VATS
{
	// The temporary damage-resistance shield a companion carries after
	// being handed a buff item.
	//
	// One number, not three: every aid item grants the SAME resistance and
	// differs only in how long it lasts (see AidItems.h for the DR x
	// seconds conversion). Per-item resistance would need a rule for what
	// happens when you top up with a different item - higher wins? average?
	// replace? - and no bar can show that. Duration is the one axis that
	// stacks cleanly.
	//
	// Applying it uses the engine's own temporary-modifier mechanism
	// (ActorValueOwner::ModActorValue with kTemporary), raising the value
	// on the way in and lowering it by the same amount on the way out. No
	// magic effect, no Papyrus, no record - and confirmed to actually move
	// the number on 2026-08-28, which was not a given: the one vanilla
	// script that changes damage resistance does it with AddPerk, so there
	// was every chance the value was derived and a write would be ignored.
	// Twenty-five probe runs said otherwise.
	class CompanionShield
	{
	public:
		static CompanionShield& Get();

		// Adds a_seconds of shield to a_actor, up to the configured cap,
		// and switches the resistance on if it was not already. Called from
		// the game thread.
		//
		// Switching targets replaces the shield rather than running two:
		// the resistance is stored per actor and there is exactly one bar.
		// The previous holder gets their bonus taken off first, so nobody
		// keeps a resistance the display no longer accounts for.
		void Add(RE::Actor* a_actor, float a_seconds);

		// Counts down. Called once per rendered frame from Overlay::Draw,
		// in EVERY mode - the whole point of a shield is that it runs while
		// the player is off fighting, not only while the support session
		// that granted it is open.
		void Tick(float a_deltaSeconds);

		// Remaining seconds and the cap, for the gauge. Zero remaining
		// means no shield and nothing is drawn.
		struct State
		{
			RE::NiPointer<RE::Actor> actor;
			float                    remaining;
			float                    capacity;
		};
		[[nodiscard]] State GetState() const;

	private:
		void ApplyResistance(RE::Actor* a_actor, float a_delta);

		mutable std::mutex       m_lock;
		RE::NiPointer<RE::Actor> m_actor;
		float                    m_remaining{ 0.0f };
		bool                     m_applied{ false };
	};
}
