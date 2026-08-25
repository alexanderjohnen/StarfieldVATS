#pragma once

namespace VATS
{
	// The VATS resource bar: a budget that VATS spends while a lock is
	// active, and that refills once the lock ends.
	//
	// DESIGN (Alexander's, 2026-08-25) - two player stats, two distinct
	// roles, rather than one blended number:
	//
	//   * The player's FULL health sets the bar's CAPACITY (how much
	//     damage can be dealt across a lock before VATS runs dry).
	//   * The player's FULL oxygen sets the REFILL RATE (how quickly the
	//     bar comes back once VATS is off).
	//
	// Deliberately keyed to the player's maximum values, not their current
	// ones. Current values would create a death spiral - hurt means less
	// VATS means more hurt - and would make the bar's own scale jump around
	// mid-fight. Maximums stay stable while still letting anything that
	// raises the player's health or oxygen (perks, gear, food) indirectly
	// upgrade VATS, which was the point of tying it to these two stats at
	// all rather than inventing a standalone number.
	//
	// SPENDING is per point of damage dealt, not per shot fired. Alexander's
	// reasoning: an automatic weapon lands far more hits than a semi-auto
	// for the same effect, so charging per hit would tax the weapon's fire
	// rate rather than what VATS actually did for the player. Damage is
	// measured as the locked target's own health dropping, which
	// automatically accounts for armour and resistances - a shot absorbed
	// by heavy armour genuinely costs less than one that lands.
	//
	// This is an entirely self-contained value. It never writes to the
	// player's real health or oxygen, per Alexander's explicit call to
	// avoid unforeseen consequences (and it keeps VATS from competing with
	// oxygen's real uses, like sprinting).
	class VatsResource
	{
	public:
		struct State
		{
			float current{ 0.0f };
			float capacity{ 0.0f };
			bool  valid{ false };  // false until the player's stats could be read
		};

		[[nodiscard]] static VatsResource& Get();

		// Called when a lock begins. Refreshes capacity/refill rate from the
		// player's current maximums (so perk and gear changes are picked up)
		// and starts damage accounting for this lock.
		void OnLockStart();

		// Called every frame while Locked, with the current target. Spends
		// budget equal to any drop in that target's health since the last
		// call. Returns false once the budget is exhausted, which the caller
		// treats as "end the lock".
		//
		// Damage dealt by anyone else (a companion, another NPC) is counted
		// too - the target's health is all this can see. Accepted as a
		// simplification rather than solved, since attributing damage would
		// need a hit event this project has no safe binding for.
		[[nodiscard]] bool TickLocked(RE::Actor* a_target);

		// Called every frame while Off. Refills the budget at the rate set
		// by the player's full oxygen.
		void TickIdle();

		[[nodiscard]] State GetState() const;

	private:
		void RefreshFromPlayer();

		float m_current{ 0.0f };
		float m_capacity{ 0.0f };
		float m_refillPerSecond{ 0.0f };
		bool  m_valid{ false };

		// Target health at the previous TickLocked, to turn an absolute
		// reading into a per-frame delta. Reset on every lock so a target
		// that was already damaged before being locked doesn't get billed
		// retroactively.
		float m_lastTargetHealth{ 0.0f };
		bool  m_haveLastTargetHealth{ false };
		std::uint32_t m_lastTargetFormID{ 0 };

		std::chrono::steady_clock::time_point m_lastTick{};
		bool                                  m_haveLastTick{ false };
	};
}
