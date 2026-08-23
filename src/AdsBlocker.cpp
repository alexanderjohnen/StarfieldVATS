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

		// Maps Settings::adsBlockKeyVK to the WM_*BUTTONDOWN message
		// WH_MOUSE_LL actually reports for it. Only the buttons a
		// low-level mouse hook can see at all are supported - no
		// VK_LBUTTON (that's the fire button, deliberately never touched
		// here, see AimAssist.cpp).
		[[nodiscard]] bool IsConfiguredButtonDown(WPARAM a_wParam, const MSLLHOOKSTRUCT* a_info, std::uint32_t a_configuredVK)
		{
			switch (a_configuredVK) {
			case VK_RBUTTON:
				return a_wParam == WM_RBUTTONDOWN;
			case VK_MBUTTON:
				return a_wParam == WM_MBUTTONDOWN;
			case VK_XBUTTON1:
				return a_wParam == WM_XBUTTONDOWN && GET_XBUTTON_WPARAM(a_info->mouseData) == XBUTTON1;
			case VK_XBUTTON2:
				return a_wParam == WM_XBUTTONDOWN && GET_XBUTTON_WPARAM(a_info->mouseData) == XBUTTON2;
			default:
				return false;
			}
		}

		// Low-level hooks have a strict response-time budget - keep this
		// body to cheap, non-blocking checks plus the swallow decision, same
		// lesson BackKeyInterceptor's comment documents (REX::INFO does
		// blocking file I/O and was found to intermittently break delivery
		// when called inline here).
		LRESULT CALLBACK HookProc(int a_code, WPARAM a_wParam, LPARAM a_lParam)
		{
			if (a_code == HC_ACTION) {
				const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(a_lParam);
				const bool  injected = (info->flags & LLMHF_INJECTED) != 0;
				if (!injected && Settings::Get().blockAdsWhileLocked &&
					IsConfiguredButtonDown(a_wParam, info, Settings::Get().adsBlockKeyVK)) {
					const bool hasFocus = GameWindowHasFocus();
					const bool locked = Controller::Get().GetMode() == VATSMode::kLocked;
					if (hasFocus && locked) {
						std::thread([]() { REX::INFO("[VATS] ADS button swallowed (Locked)"); }).detach();
						return 1;  // swallow - Starfield never sees this button press
					}
				}
			}
			return ::CallNextHookEx(nullptr, a_code, a_wParam, a_lParam);
		}
	}

	void AdsBlocker::ThreadProc(const std::stop_token& a_stop)
	{
		s_hook = ::SetWindowsHookExW(WH_MOUSE_LL, HookProc, nullptr, 0);
		if (!s_hook) {
			REX::ERROR("failed to install ADS-block mouse hook, GetLastError={}", ::GetLastError());
			return;
		}
		REX::INFO("ADS blocker started");

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
