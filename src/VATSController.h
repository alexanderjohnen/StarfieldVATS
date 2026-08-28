#pragma once

#include "ProjectileTypeOverride.h"

#include <chrono>
#include <mutex>

namespace VATS
{
	// Central state machine for the VATS system. A single hotkey toggles
	// Off <-> Locked. There used to be a third Aiming state in between
	// (highlight, then a second press to commit) but once activation was
	// gated on the hand scanner being open, the scanner's own highlight
	// plus the "TARGETING (N)" hint already give the "see before
	// committing" feedback the Aiming state existed for — so it was
	// dropped 2026-08-22 in favor of locking directly onto whatever's
	// under the crosshair on a single press. Locked tracks that actor's
	// position regardless of where the camera points afterward.
	enum class VATSMode
	{
		kOff,
		kLocked,

		// Same button, same scanner, same crosshair pick - but the thing
		// under it is one of the player own people. Aiming a VATS lock at a
		// companion was always going to be either a no-op or a mistake, so
		// that press means something else instead: support.
		//
		// Going through the scanner rather than a key of its own is
		// Alexander design and it pays for itself twice - the scanner
		// extends the crosshair pick well past interaction range (confirmed
		// ~17m+ through cover, 2026-08-22), and there is no second keybind
		// to explain or collide with.
		//
		// Deliberately a MODE and not a one-shot heal, even though healing is
		// currently the only action: this is the skeleton the menu grows
		// into. With one entry, pressing again simply performs it.
		kSupport,
	};

	class Controller
	{
	public:
		// Snapshot of what the HUD overlay needs, safe to copy across
		// threads (Advance runs on an SFSE task thread, Draw in the
		// swapchain Present hook — not guaranteed to be the same thread).
		struct OverlayState
		{
			VATSMode                 mode{ VATSMode::kOff };
			RE::NiPointer<RE::Actor> actor;
		};

		[[nodiscard]] static Controller& Get();

		// Thread-safe: queues the actual state advance onto the game thread.
		void RequestAdvance();

		[[nodiscard]] VATSMode GetMode() const { return m_mode.load(std::memory_order_relaxed); }

		[[nodiscard]] OverlayState GetOverlayState();

		// Unconditionally resets to Off. Unlike RequestAdvance(), this
		// doesn't route through the SFSE task queue — it only touches our
		// own atomic/mutex-guarded state, no engine calls, so it's safe to
		// call directly from any thread. Called from Overlay::Draw() (render
		// thread) when a menu/transition that should kill an active lock is
		// detected (pause, star map, data menu, dialogue, a cell-transition
		// loading screen, ...) — see the comment in Overlay.cpp. No-op if
		// already Off.
		// a_reason is logged verbatim. It is a parameter because the line
		// used to be a hardcoded "forced - blocking menu or transition",
		// which every caller shared: a combat run on 2026-08-26 ended 16
		// locks and the log claimed that reason all 16 times, while 8 were
		// kills and 3 were the VATS budget running dry. A diagnostic that
		// names a cause it cannot know is worse than one that says nothing.
		void ForceOff(const char* a_reason = "unspecified");


		// Records the outcome of the most recent aim-assist roll (Alexander's
		// request: visible feedback for the roll itself, not just the
		// numeric hit-chance readout) - set from AimAssist's SteeringLoop, a
		// background thread, as soon as the roll happens. Two plain atomics
		// rather than a mutex - both are simple scalars, and a torn read
		// (stale hit paired with a fresh timestamp or vice versa) degrades
		// to at worst one wrong-colored frame right at the edge of the
		// display window, not a crash.
		void RecordShotResult(bool a_hit);

		struct ShotResult
		{
			bool                                   hit{ false };
			std::chrono::steady_clock::time_point  time{};
			bool                                   valid{ false };  // false until the first RecordShotResult() call
		};
		[[nodiscard]] ShotResult GetLastShotResult();

		// Re-checks the player's currently equipped weapon against
		// whichever weapon's projectile the override currently applies to
		// (2026-08-25) - a no-op if unchanged. Fixes a real gap the
		// lock-scoped design above introduced: engaging once at Lock time
		// only covers whatever was equipped AT THAT MOMENT, so switching
		// weapons mid-lock left the new weapon un-flipped (still hitscan,
		// no projectile ever spawns for ProjectileTracker to find) until
		// Alexander toggled VATS off and back on - confirmed 2026-08-25
		// from a real session's log (weapon-switch mid-hold, engaged
		// projectile pointer stayed on the old weapon, zero "projectile
		// redirect" lines for the rest of that hold). No-op safe to call
		// every frame; only actually does anything (Disengage+Engage) on
		// an actual change. Call from Overlay::Draw() while Locked.
		void SyncProjectileOverride();

		// Swaps the locked target for the nearest living enemy in front of
		// the player, staying Locked. Called when the current target dies
		// with resource budget left (Overlay.cpp) - Alexander's request,
		// deliberately deferred until the resource system existed so that
		// chaining kills costs something.
		//
		// Safe from the render thread, same as ForceOff(): a target swap
		// touches only this class's own mutex-guarded state. Everything
		// the lock transition does that needs the game thread - closing the
		// scanner, hiding HUD elements, engaging the projectile override -
		// is already done and stays valid, since none of it is per-target
		// (the override is per equipped weapon, and SyncProjectileOverride
		// keeps that current on its own).
		//
		// Returns false if nothing suitable is in range, in which case the
		// caller ends the lock as before.
		[[nodiscard]] bool TryAdvanceToNextTarget();

		// Called each frame while the locked target is dead. Returns true
		// to stay Locked (either because the lock just moved to a new
		// target, or because we are still inside the grace window waiting
		// for the player to pick one), false to end the lock.
		//
		// The grace window exists because requiring the crosshair target
		// at the exact instant of death almost never fired: at that moment
		// the player is by definition still aiming at the enemy who just
		// dropped. Holding the lock open briefly lets them swing onto the
		// next one, which is both how the re-acquire naturally plays and
		// what keeps the occlusion-correct crosshair source usable - the
		// alternative was going back to a cone scan that picks targets
		// through walls.
		[[nodiscard]] bool TryAdvanceOrHold();

	private:
		void Advance();  // game thread only

		std::atomic<VATSMode>    m_mode{ VATSMode::kOff };
		std::mutex               m_targetLock;
		RE::NiPointer<RE::Actor> m_target;

		// Deadline for the post-kill grace window (see TryAdvanceOrHold).
		// Zero means "not currently waiting"; reset on every lock and on
		// every successful advance.
		std::mutex                            m_advanceLock;
		std::chrono::steady_clock::time_point m_advanceDeadline{};
		bool                                  m_advancePending{ false };

		std::atomic<bool>        m_shotHit{ false };
		std::atomic<std::int64_t> m_shotTimestamp{ 0 };  // steady_clock ticks; 0 = no shot recorded yet

		// Hitscan->real-projectile (and speed) override, held for the whole
		// duration of a lock (2026-08-25, Alexander's call). Previously
		// engaged/disengaged per trigger-hold from AimAssist's SteeringLoop,
		// which meant every single trigger pull raced the engine: the round
		// could leave the barrel before the flip landed, staying a real
		// hitscan with no projectile to redirect at all. That race is the
		// best explanation the project has for why automatic weapons always
		// behaved better than semi-auto ones - only the first round of a
		// burst can lose that race, while for single shots EVERY round is a
		// first round. Engaging once at lock time removes the race outright:
		// by the time the player can fire, the flip is long since done.
		// Guarded by m_projectileOverrideLock rather than m_targetLock -
		// ForceOff() can run from the render thread while a SteeringLoop
		// tick is mid-flight elsewhere.
		std::mutex                      m_projectileOverrideLock;
		ProjectileTypeOverride::Token   m_projectileOverride;
		// Raw projectile pointer m_projectileOverride currently corresponds
		// to, tracked separately from the Token itself (2026-08-25) - a
		// Token for an ALREADY-real weapon (rocket/grenade) is deliberately
		// left at projectile=0/active=false by Engage() (see
		// ProjectileTypeOverride.cpp), which would make SyncProjectileOverride
		// re-trigger every single check if it compared against
		// m_projectileOverride.projectile instead. 0 = nothing engaged yet.
		std::uint64_t                   m_projectileOverrideTarget{ 0 };
	};
}
