#pragma once

namespace VATS
{
	// Suppresses the aim-down-sights button (Settings::adsBlockKeyVK,
	// default VK_RBUTTON) at the OS level via a low-level mouse hook while
	// VATS is Locked, so Starfield never sees the press and never enters
	// ADS - the whole point of this mod is that the camera/weapon never
	// visibly reorients onto the target (see AimAssist.h), so a manual ADS
	// during a lock would fight that. Same technique and shape as
	// BackKeyInterceptor (which does the equivalent for the back/Tab key),
	// just WH_MOUSE_LL instead of WH_KEYBOARD_LL. Only the down edge is
	// swallowed, matching BackKeyInterceptor's reasoning: an orphan
	// button-up reaching the game is harmless.
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
