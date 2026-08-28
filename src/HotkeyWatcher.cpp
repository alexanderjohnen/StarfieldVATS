#include "HotkeyWatcher.h"

#include "Settings.h"
#include "VATSController.h"

#include <chrono>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

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
	}

	void HotkeyWatcher::Start()
	{
		if (m_thread.joinable()) {
			return;
		}

		m_thread = std::jthread(&HotkeyWatcher::ThreadProc);
		VATS_LOG("hotkey watcher started");
	}

	void HotkeyWatcher::Stop()
	{
		if (m_thread.joinable()) {
			m_thread.request_stop();
			m_thread.join();
		}
	}

	void HotkeyWatcher::ThreadProc(const std::stop_token& a_stop)
	{
		bool wasDown = false;
		bool holdFired = false;
		auto pressStart = std::chrono::steady_clock::now();

		while (!a_stop.stop_requested()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(16));

			const auto& settings = Settings::Get();
			if (!settings.enabled || !GameWindowHasFocus()) {
				wasDown = false;
				continue;
			}

			// Tap versus hold, on one key (2026-08-28, Alexander design).
			//
			// This replaced putting the support action on the game own
			// activate key, which did not work: the low-level keyboard hook
			// never swallowed it, so pressing E healed nothing and opened a
			// conversation instead. That hook has the same history with the
			// back key (see BackKeyInterceptor comment on intermittent
			// interception), so the fix is not to try harder at swallowing -
			// it is to stop needing to. The VATS key is not a game binding, so
			// nothing has to be suppressed for it.
			//
			// Tap  = act   (open a session, lock, heal - whatever the mode says)
			// Hold = cancel (leave whatever is open)
			//
			// The hold fires the moment the threshold passes rather than on
			// release, so the feedback arrives while the key is still down and
			// the player is not left wondering how long is long enough. The
			// release that follows must then NOT also count as a tap, which is
			// what holdFired tracks.
			const bool isDown =
				(::GetAsyncKeyState(static_cast<int>(settings.activationKeyVK)) & 0x8000) != 0;

			if (isDown && !wasDown) {
				pressStart = std::chrono::steady_clock::now();
				holdFired = false;
			} else if (isDown && !holdFired &&
					   std::chrono::steady_clock::now() - pressStart >=
						   std::chrono::milliseconds(settings.holdToCancelMs)) {
				holdFired = true;
				Controller::Get().RequestCancel();
			} else if (!isDown && wasDown && !holdFired) {
				Controller::Get().RequestAdvance();
			}
			wasDown = isDown;
		}
	}
}
