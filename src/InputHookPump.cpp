#include "InputHookPump.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef ERROR  // wingdi.h's ERROR macro clashes with REX::ERROR below

namespace VATS
{
	namespace
	{
		[[nodiscard]] bool GameWindowHasFocus()
		{
			const HWND foreground = ::GetForegroundWindow();
			if (!foreground) {
				return false;
			}
			DWORD pid = 0;
			::GetWindowThreadProcessId(foreground, &pid);
			return pid == ::GetCurrentProcessId();
		}

		// Upper bound on how long the pump can sit idle without noticing
		// that focus changed or that a stop was requested. It is NOT the
		// latency of the hook itself: an input event wakes
		// MsgWaitForMultipleObjectsEx immediately, so the callback runs at
		// once rather than after this interval. Only the focus check and
		// the stop check are paced by it, and neither needs to be quick.
		constexpr DWORD kIdleTickMs = 100;
	}

	void RunLowLevelHookPump(
		const std::stop_token&       a_stop,
		LowLevelHookKind             a_kind,
		LowLevelHookProc             a_proc,
		const char*                  a_name,
		const std::function<void()>& a_onPump,
		const std::function<void()>& a_onUninstall)
	{
		const int idHook = (a_kind == LowLevelHookKind::kMouse) ? WH_MOUSE_LL : WH_KEYBOARD_LL;
		HHOOK     hook = nullptr;
		bool      loggedInstall = false;

		// hMod is documented as ignorable for the LL hooks as long as the
		// hook proc lives in the calling process (it does - we are an
		// in-process SFSE plugin), so NULL is the textbook-correct value.
		const auto install = [&]() {
			if (hook) {
				return;
			}
			hook = ::SetWindowsHookExW(idHook, reinterpret_cast<HOOKPROC>(a_proc), nullptr, 0);
			if (!hook) {
				VATS_ERROR("[input] {}: SetWindowsHookEx failed, GetLastError={}", a_name, ::GetLastError());
				return;
			}
			// Logged once rather than on every focus change - alt-tabbing
			// in and out of a game is not an event worth a log line each
			// way, and at iLogLevel=2 this would otherwise be the noisiest
			// thing in the file.
			if (!loggedInstall) {
				loggedInstall = true;
				VATS_LOG("[input] {}: hook installed (and removed again whenever the game loses focus)", a_name);
			}
		};

		const auto uninstall = [&]() {
			if (!hook) {
				return;
			}
			::UnhookWindowsHookEx(hook);
			hook = nullptr;
			if (a_onUninstall) {
				a_onUninstall();
			}
		};

		while (!a_stop.stop_requested()) {
			if (GameWindowHasFocus()) {
				install();
			} else {
				uninstall();
			}

			// Blocks until an input event arrives for our hook, a message
			// is posted, or the idle tick expires. MWMO_INPUTAVAILABLE is
			// what makes this correct rather than merely cheap: without
			// it, input that was already present before the wait began
			// would not re-signal, and the pump could block past an event
			// it was supposed to deliver.
			::MsgWaitForMultipleObjectsEx(0, nullptr, kIdleTickMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

			// A low-level hook fires from inside this dispatch, on this
			// thread - so by the time PeekMessage drains, the callback has
			// already run and whatever it flagged is ready for a_onPump
			// below. That ordering is why the deferred work costs no
			// latency despite not running in the callback itself.
			MSG msg;
			while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
				::TranslateMessage(&msg);
				::DispatchMessageW(&msg);
			}

			if (a_onPump) {
				a_onPump();
			}
		}

		uninstall();
	}
}
