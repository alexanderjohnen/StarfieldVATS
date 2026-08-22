#include "BackKeyInterceptor.h"

#include "Settings.h"
#include "VATSController.h"

#include <chrono>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef ERROR  // wingdi.h's ERROR macro clashes with REX::ERROR below

namespace VATS
{
	namespace
	{
		HHOOK s_hook = nullptr;

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

		// WH_KEYBOARD_LL callback. Runs on BackKeyInterceptor's own thread
		// (the thread that installed the hook, per Windows' contract for
		// low-level hooks), invoked as part of that thread's message pump
		// below — nothing here runs on the game thread or render thread.
		//
		// Only ever swallows Settings::backKeyVK, and only while the game
		// has focus and VATS is currently Locked — every other key, and
		// every keypress outside those conditions, passes straight through
		// via CallNextHookEx untouched. Checked on key-down only: the
		// matching key-up is deliberately let through even though its
		// down was swallowed (an orphan key-up reaching the game is
		// harmless; trying to also suppress it would need extra state to
		// track "was the down swallowed" and isn't worth the complexity).
		//
		// The actual work (logging, ForceOff) is dispatched to a detached
		// thread rather than done inline here - low-level hooks have a
		// strict response-time budget (Windows can silently stop
		// delivering events to a hook that takes too long), and
		// REX::INFO does blocking file I/O. Found 2026-08-22: this was
		// intermittently failing to intercept at all, almost certainly
		// this exact problem - the hook body must stay only cheap, non-
		// blocking checks plus the swallow decision.
		LRESULT CALLBACK HookProc(int a_code, WPARAM a_wParam, LPARAM a_lParam)
		{
			if (a_code == HC_ACTION && (a_wParam == WM_KEYDOWN || a_wParam == WM_SYSKEYDOWN)) {
				const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(a_lParam);
				if (info->vkCode == Settings::Get().backKeyVK) {
					// Diagnostic (2026-08-22): "[VATS] back key intercepted"
					// never once appeared in a full test session despite
					// Alexander pressing Tab while Locked - this logs every
					// matching keydown regardless of the other two gates
					// (focus/mode, both cheap non-blocking checks, safe to
					// evaluate inline) so the next test's log says exactly
					// which one is rejecting it, instead of the hook body
					// silently doing nothing whenever any gate fails. Remove
					// once the real cause is confirmed.
					const bool     hasFocus = GameWindowHasFocus();
					const VATSMode mode = Controller::Get().GetMode();
					const bool     shouldSwallow = hasFocus && mode == VATSMode::kLocked;
					std::thread([hasFocus, mode, shouldSwallow]() {
						REX::INFO("[VATS] back key seen: hasFocus={} mode={} -> {}",
							hasFocus, mode == VATSMode::kLocked ? "Locked" : "Off",
							shouldSwallow ? "swallow+ForceOff" : "pass through");
						if (shouldSwallow) {
							Controller::Get().ForceOff();
						}
					}).detach();
					if (shouldSwallow) {
						return 1;  // swallow - Starfield never sees this keydown
					}
				}
			}
			return ::CallNextHookEx(nullptr, a_code, a_wParam, a_lParam);
		}
	}

	void BackKeyInterceptor::ThreadProc(const std::stop_token& a_stop)
	{
		// hMod is documented as ignorable for WH_KEYBOARD_LL as long as the
		// hook proc lives in the calling process (it does - we're an
		// in-process SFSE plugin) - NULL is the textbook-correct value here,
		// not a module handle.
		s_hook = ::SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, nullptr, 0);
		if (!s_hook) {
			REX::ERROR("failed to install back-key hook, GetLastError={}", ::GetLastError());
			return;
		}
		REX::INFO("back-key interceptor started");

		// A low-level hook only fires while its installing thread pumps
		// messages. PeekMessage (not blocking GetMessage) so the loop can
		// still check the stop token for clean shutdown, same polling-loop
		// shape as HotkeyWatcher.
		while (!a_stop.stop_requested()) {
			MSG msg;
			while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
				::TranslateMessage(&msg);
				::DispatchMessageW(&msg);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		::UnhookWindowsHookEx(s_hook);
		s_hook = nullptr;
	}

	void BackKeyInterceptor::Start()
	{
		if (m_thread.joinable()) {
			return;
		}
		m_thread = std::jthread(&BackKeyInterceptor::ThreadProc);
	}

	void BackKeyInterceptor::Stop()
	{
		if (m_thread.joinable()) {
			m_thread.request_stop();
			m_thread.join();
		}
	}
}
