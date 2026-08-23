#include "AdsBlocker.h"

#include "AdsProbe.h"
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

		enum class ButtonEdge
		{
			kNone,
			kDown,
			kUp,
		};

		// Maps Settings::adsBlockKeyVK to the WM_*BUTTON{DOWN,UP} messages
		// WH_MOUSE_LL actually reports for it. Only the buttons a low-level
		// mouse hook can see at all are supported - no VK_LBUTTON (that's
		// the fire button, deliberately never touched here, see
		// AimAssist.cpp).
		[[nodiscard]] ButtonEdge ClassifyButtonEvent(WPARAM a_wParam, const MSLLHOOKSTRUCT* a_info, std::uint32_t a_configuredVK)
		{
			switch (a_configuredVK) {
			case VK_RBUTTON:
				if (a_wParam == WM_RBUTTONDOWN) return ButtonEdge::kDown;
				if (a_wParam == WM_RBUTTONUP) return ButtonEdge::kUp;
				return ButtonEdge::kNone;
			case VK_MBUTTON:
				if (a_wParam == WM_MBUTTONDOWN) return ButtonEdge::kDown;
				if (a_wParam == WM_MBUTTONUP) return ButtonEdge::kUp;
				return ButtonEdge::kNone;
			case VK_XBUTTON1:
			case VK_XBUTTON2:
				if (a_wParam == WM_XBUTTONDOWN || a_wParam == WM_XBUTTONUP) {
					const auto want = a_configuredVK == VK_XBUTTON1 ? XBUTTON1 : XBUTTON2;
					if (GET_XBUTTON_WPARAM(a_info->mouseData) == want) {
						return a_wParam == WM_XBUTTONDOWN ? ButtonEdge::kDown : ButtonEdge::kUp;
					}
				}
				return ButtonEdge::kNone;
			default:
				return ButtonEdge::kNone;
			}
		}

		// Low-level hooks have a strict response-time budget - keep this
		// body to cheap, non-blocking checks plus the swallow decision, same
		// lesson BackKeyInterceptor's comment documents (REX::INFO does
		// blocking file I/O and was found to intermittently break delivery
		// when called inline here).
		//
		// Diagnostic (2026-08-23): dumps a raw player snapshot on every
		// down/up of the configured button, regardless of VATS lock state -
		// not just while swallowing - so an *unlocked* baseline (normal ADS,
		// nothing blocked) can be diffed against a locked attempt to find
		// whatever actually drives aiming. See AdsProbe.h. Remove the dump
		// calls once a real fix (or a confirmed dead end) lands; the
		// swallow-while-Locked behavior itself stays regardless.
		LRESULT CALLBACK HookProc(int a_code, WPARAM a_wParam, LPARAM a_lParam)
		{
			if (a_code == HC_ACTION) {
				const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(a_lParam);
				const bool  injected = (info->flags & LLMHF_INJECTED) != 0;
				const auto  edge = injected ? ButtonEdge::kNone : ClassifyButtonEvent(a_wParam, info, Settings::Get().adsBlockKeyVK);
				if (edge != ButtonEdge::kNone && GameWindowHasFocus()) {
					const bool locked = Controller::Get().GetMode() == VATSMode::kLocked;
					const bool shouldSwallow = edge == ButtonEdge::kDown && locked && Settings::Get().blockAdsWhileLocked;
					std::thread([edge, locked, shouldSwallow]() {
						const char* tag = edge == ButtonEdge::kDown ? "ads-down" : "ads-up";
						REX::INFO("[VATS] ADS button {} (locked={}, swallowed={})",
							edge == ButtonEdge::kDown ? "pressed" : "released", locked, shouldSwallow);
						DumpPlayerRawRange(tag);
					}).detach();
					if (shouldSwallow) {
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
