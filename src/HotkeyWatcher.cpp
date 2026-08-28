#include "HotkeyWatcher.h"

#include "Settings.h"
#include "VATSController.h"

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

		while (!a_stop.stop_requested()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(16));

			const auto& settings = Settings::Get();
			if (!settings.enabled || !GameWindowHasFocus()) {
				wasDown = false;
				continue;
			}

			const bool isDown =
				(::GetAsyncKeyState(static_cast<int>(settings.activationKeyVK)) & 0x8000) != 0;
			if (isDown && !wasDown) {
				Controller::Get().RequestAdvance();
			}
			wasDown = isDown;
		}
	}
}
