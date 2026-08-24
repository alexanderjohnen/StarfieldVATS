#include "AdsBlocker.h"

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

		// Matches a WM_*BUTTONDOWN message against Settings::adsButtonVK.
		// XBUTTON1/2 share one message (WM_XBUTTONDOWN) and are told apart
		// via the high word of MSLLHOOKSTRUCT::mouseData, same as regular
		// Win32 XBUTTON handling.
		[[nodiscard]] bool MatchesConfiguredButton(WPARAM a_msg, const MSLLHOOKSTRUCT* a_info, std::uint32_t a_vk)
		{
			switch (a_vk) {
			case VK_RBUTTON:
				return a_msg == WM_RBUTTONDOWN;
			case VK_MBUTTON:
				return a_msg == WM_MBUTTONDOWN;
			case VK_XBUTTON1:
				return a_msg == WM_XBUTTONDOWN && HIWORD(a_info->mouseData) == XBUTTON1;
			case VK_XBUTTON2:
				return a_msg == WM_XBUTTONDOWN && HIWORD(a_info->mouseData) == XBUTTON2;
			default:
				return false;
			}
		}

		// Runs on AdsBlocker's own thread (the thread that installed the
		// hook, per Windows' contract for low-level hooks) - never the game
		// thread or render thread. Only ever reacts to a real (non-
		// injected) press of the configured ADS button while a lock is
		// active; every other message passes straight through via
		// CallNextHookEx untouched, and the button itself is never
		// swallowed - Starfield sees the real ADS press just like it always
		// did, we just drop our own lock alongside it.
		LRESULT CALLBACK HookProc(int a_code, WPARAM a_wParam, LPARAM a_lParam)
		{
			if (a_code == HC_ACTION) {
				const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(a_lParam);
				const bool  injected = (info->flags & LLMHF_INJECTED) != 0;
				if (!injected && Settings::Get().endLockOnAds &&
					Controller::Get().GetMode() == VATSMode::kLocked &&
					MatchesConfiguredButton(a_wParam, info, Settings::Get().adsButtonVK)) {
					REX::INFO("[VATS] ADS button pressed while Locked, ending lock");
					Controller::Get().ForceOff();
				}
			}
			return ::CallNextHookEx(nullptr, a_code, a_wParam, a_lParam);
		}
	}

	void AdsBlocker::ThreadProc(const std::stop_token& a_stop)
	{
		s_hook = ::SetWindowsHookExW(WH_MOUSE_LL, HookProc, nullptr, 0);
		if (!s_hook) {
			REX::ERROR("failed to install ADS-watch mouse hook, GetLastError={}", ::GetLastError());
			return;
		}
		REX::INFO("[VATS] ADS watcher started (ends lock on button 0x{:X})", Settings::Get().adsButtonVK);

		// A low-level hook only fires while its installing thread pumps
		// messages. PeekMessage (not blocking GetMessage) so the loop can
		// still check the stop token for clean shutdown, same polling-loop
		// shape as BackKeyInterceptor/AimAssist.
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

	void AdsBlocker::Start()
	{
		if (m_thread.joinable()) {
			return;
		}
		m_thread = std::jthread(&AdsBlocker::ThreadProc);
	}

	void AdsBlocker::Stop()
	{
		if (m_thread.joinable()) {
			m_thread.request_stop();
			m_thread.join();
		}
	}
}
