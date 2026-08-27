#include "BackKeyInterceptor.h"

#include "InputHookPump.h"
#include "Settings.h"
#include "VATSController.h"

#include <atomic>
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

		// Filled in by HookProc, consumed by the pump loop - see HookProc.
		std::atomic<bool> s_pendingBackKey{ false };
		std::atomic<bool> s_pendingHasFocus{ false };
		std::atomic<bool> s_pendingLocked{ false };
		std::atomic<bool> s_pendingSwallow{ false };

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
					// Signalled, not dispatched (2026-08-27). Spawning a
					// std::thread is itself real work - a kernel thread
					// create plus a stack commit - and it happened here, in
					// the system-wide input path, on every press of this key
					// regardless of whether anything came of it (the common
					// case by far: Tab outside VATS, which just wanted the
					// diagnostic line). The pump loop below already ticks
					// every 5ms and can do both the log and the ForceOff off
					// the input path, with no thread per keystroke.
					s_pendingHasFocus.store(hasFocus, std::memory_order_relaxed);
					s_pendingLocked.store(mode == VATSMode::kLocked, std::memory_order_relaxed);
					s_pendingSwallow.store(shouldSwallow, std::memory_order_relaxed);
					s_pendingBackKey.store(true, std::memory_order_relaxed);
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
		// Same reasoning as AdsBlocker: a low-level hook is a system-wide
		// cost, so a feature switched off must not leave one installed.
		if (!Settings::Get().interceptBackKey) {
			VATS_LOG("back-key interceptor: bInterceptBackKey=0, keyboard hook not installed");
			return;
		}

		VATS_LOG("back-key interceptor started");

		RunLowLevelHookPump(
			a_stop, LowLevelHookKind::kKeyboard,
			reinterpret_cast<LowLevelHookProc>(&HookProc), "back-key interceptor",
			[]() {
				if (s_pendingBackKey.exchange(false, std::memory_order_relaxed)) {
					const bool hasFocus = s_pendingHasFocus.load(std::memory_order_relaxed);
					const bool locked = s_pendingLocked.load(std::memory_order_relaxed);
					const bool swallowed = s_pendingSwallow.load(std::memory_order_relaxed);
					VATS_TRACE("[VATS] back key seen: hasFocus={} mode={} -> {}",
						hasFocus, locked ? "Locked" : "Off",
						swallowed ? "swallow+ForceOff" : "pass through");
					if (swallowed) {
						Controller::Get().ForceOff("back key pressed");
					}
				}
			},
			nullptr);
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
