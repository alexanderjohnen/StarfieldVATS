#pragma once

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

	// Which spacesuit component is currently the aim point, cycled via
	// mouse wheel while Locked (2026-08-22) — a deliberately simplified
	// stand-in for full skeletal body-part targeting (never built, see
	// starfield-vats-mod-design memory): most enemies wear spacesuits, and
	// these three pieces are the only ones with a real gameplay difference
	// (Suit: higher hit chance, Helmet: more damage - likely free from
	// Starfield's own existing headshot multiplier if we aim there
	// accurately, Pack: can explode from enough hits - an existing native
	// Starfield mechanic, not something we build ourselves).
	enum class BodyPart
	{
		kSuit,
		kHelmet,
		kPack,
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
			BodyPart                 bodyPart{ BodyPart::kSuit };
		};

		[[nodiscard]] static Controller& Get();

		// Thread-safe: queues the actual state advance onto the game thread.
		void RequestAdvance();

		[[nodiscard]] VATSMode GetMode() const { return m_mode.load(std::memory_order_relaxed); }

		[[nodiscard]] OverlayState GetOverlayState();

		// Cycles Suit -> Helmet -> Pack -> Suit. No-op while Off (nothing
		// to select a part of). Thread-safe, callable directly from the
		// mouse-hook thread (AimAssist) — only touches our own atomic
		// state, no engine calls.
		void CycleBodyPart();

		// Unconditionally resets to Off. Unlike RequestAdvance(), this
		// doesn't route through the SFSE task queue — it only touches our
		// own atomic/mutex-guarded state, no engine calls, so it's safe to
		// call directly from any thread. Called from Overlay::Draw() (render
		// thread) when a menu/transition that should kill an active lock is
		// detected (pause, star map, data menu, dialogue, a cell-transition
		// loading screen, ...) — see the comment in Overlay.cpp. No-op if
		// already Off.
		void ForceOff();

	private:
		void Advance();  // game thread only

		std::atomic<VATSMode>    m_mode{ VATSMode::kOff };
		std::atomic<BodyPart>    m_bodyPart{ BodyPart::kSuit };
		std::mutex               m_targetLock;
		RE::NiPointer<RE::Actor> m_target;
	};
}
