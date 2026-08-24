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
		void ForceOff();

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

	private:
		void Advance();  // game thread only

		std::atomic<VATSMode>    m_mode{ VATSMode::kOff };
		std::mutex               m_targetLock;
		RE::NiPointer<RE::Actor> m_target;

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
	};
}
