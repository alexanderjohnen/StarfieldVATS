#include "AdsBlocker.h"

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
		std::atomic<bool> s_pendingEndLock{ false };

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
					Controller::Get().GetMode() != VATSMode::kOff &&
					MatchesConfiguredButton(a_wParam, info, Settings::Get().adsButtonVK)) {
					// Signal only. Everything this used to do inline - a
					// VATS_LOG (blocking file I/O) and ForceOff (which
					// unwinds the whole lock: HUD restore, projectile-type
					// override release, combat-target thread join) - ran
					// inside a WH_MOUSE_LL callback, i.e. in the SYSTEM-WIDE
					// input delivery path, where Windows stops delivering
					// events to a hook that overruns its time budget and
					// every other application waits behind it. This is the
					// same mistake AimAssist.cpp already documents twice
					// (see its HookProc). The pump loop below picks the flag
					// up within 5ms and does the work on its own thread.
					s_pendingEndLock.store(true, std::memory_order_relaxed);
				}
			}
			return ::CallNextHookEx(nullptr, a_code, a_wParam, a_lParam);
		}
	}

	void AdsBlocker::ThreadProc(const std::stop_token& a_stop)
	{
		// A WH_MOUSE_LL hook is a SYSTEM-WIDE cost: once installed, every
		// mouse event on the whole desktop - in every application, whether
		// Starfield is even running in the foreground - is routed through
		// this process before it reaches its real target. Installing one
		// for a feature the user has switched off is pure overhead, so
		// honour bEndLockOnAds here and not only inside the callback
		// (2026-08-27). The hook used to go in unconditionally.
		if (!Settings::Get().endLockOnAds) {
			VATS_LOG("[VATS] ADS watcher: bEndLockOnAds=0, mouse hook not installed");
			return;
		}

		VATS_LOG("[VATS] ADS watcher started (ends lock on button 0x{:X})", Settings::Get().adsButtonVK);

		RunLowLevelHookPump(
			a_stop, LowLevelHookKind::kMouse,
			reinterpret_cast<LowLevelHookProc>(&HookProc), "ADS watcher",
			[]() {
				// The work the callback deliberately did not do. It runs in
				// the same pump iteration the callback fired in, so this
				// costs no latency - it only moves the work off the
				// system-wide input path.
				if (s_pendingEndLock.exchange(false, std::memory_order_relaxed)) {
					VATS_LOG("[VATS] ADS button pressed while Locked, ending lock");
					Controller::Get().ForceOff("player aimed down sights");
				}
			},
			nullptr);
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
