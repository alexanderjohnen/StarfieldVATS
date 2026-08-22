#pragma once

namespace VATS
{
	// Intercepts the real "back" key (Settings::backKeyVK, same key that
	// opens Starfield's Tab-opened character hub, "DataMenu") at the OS
	// level via a low-level keyboard hook, so that while VATS is Locked,
	// pressing it ends VATS instead of letting the press also reach the
	// game and open DataMenu at the same time. Mirrors vanilla behavior:
	// pressing this key while the hand scanner is open closes the scanner
	// first, and only opens DataMenu on a further press once nothing else
	// is open to close — see starfield-vats-mod-design memory, 2026-08-22.
	//
	// This is a different technique from HotkeyWatcher/the scanner-close
	// SendInput logic — those only ever generate input. This one suppresses
	// a real keypress from ever reaching the game, which needs a system-
	// wide WH_KEYBOARD_LL hook (requires a message-pumping thread) rather
	// than plain polling.
	class BackKeyInterceptor
	{
	public:
		static void Start();
		static void Stop();

	private:
		static void ThreadProc(const std::stop_token& a_stop);

		static inline std::jthread m_thread;
	};
}
