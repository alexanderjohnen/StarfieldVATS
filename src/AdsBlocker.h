#pragma once

namespace VATS
{
	// Forces the player's camera out of iron-sights (RE::CameraState::
	// kIronSights) whenever it enters that state while VATS is Locked - the
	// weapon/camera should never visibly reorient onto the target, only the
	// round itself curves (see AimAssist.h), so a manual ADS during a lock
	// would fight that.
	//
	// Replaces an earlier WH_MOUSE_LL-hook-based approach (swallow the ADS
	// button at the OS level) that correctly intercepted the button message
	// (confirmed via log) but had zero effect on ADS actually engaging -
	// Starfield evidently reads that input through a path an OS-level
	// message hook can't see (raw input, most likely), the same class of
	// problem EngineInputLayer.h documents for the Tab-key interceptor.
	// This reacts to the actual engine-side effect (the camera state
	// transition) instead of trying to suppress its cause - found
	// 2026-08-23 by decompiling HONKCORE's hudmenu.gfx (a pure Scaleform
	// HUD replacement mod, no SFSE dependency) and tracing its "isAiming"
	// widget-visibility condition back to a camera-state-driven signal, not
	// an actor/animation-graph flag - see AdsBlocker.cpp for the full
	// trail.
	class AdsBlocker
	{
	public:
		static void Start();
		static void Stop();

	private:
		static void ThreadProc(const std::stop_token& a_stop);

		static inline std::jthread m_thread;
	};
}
