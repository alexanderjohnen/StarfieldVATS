#include "AdsBlocker.h"

#include "Settings.h"
#include "VATSController.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef ERROR  // wingdi.h's ERROR macro clashes with REX::ERROR below

namespace VATS
{
	namespace
	{
		// Real key-up simulation (SendInput) - same proven technique
		// VATSController.cpp's scanner-close logic already uses
		// successfully. Mouse buttons only, matching what
		// Settings::adsReleaseKeyVK is documented to accept.
		void SendButtonUp(std::uint32_t a_vk)
		{
			INPUT up{};
			up.type = INPUT_MOUSE;
			switch (a_vk) {
			case VK_RBUTTON:
				up.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
				break;
			case VK_MBUTTON:
				up.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
				break;
			case VK_XBUTTON1:
				up.mi.dwFlags = MOUSEEVENTF_XUP;
				up.mi.mouseData = XBUTTON1;
				break;
			case VK_XBUTTON2:
				up.mi.dwFlags = MOUSEEVENTF_XUP;
				up.mi.mouseData = XBUTTON2;
				break;
			default:
				return;
			}
			::SendInput(1, &up, sizeof(INPUT));
		}

		class StartSink : public RE::BSTEventSink<RE::PlayerControls::PlayerIronSightsStartEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::PlayerControls::PlayerIronSightsStartEvent&, RE::BSTEventSource<RE::PlayerControls::PlayerIronSightsStartEvent>*) override
			{
				if (!Settings::Get().blockAdsWhileLocked || Controller::Get().GetMode() != VATSMode::kLocked) {
					return RE::BSEventNotifyControl::kContinue;
				}
				REX::INFO("[VATS] ADS: PlayerIronSightsStartEvent while Locked, forcing camera back + synthetic release");
				if (auto* camera = RE::PlayerCamera::GetSingleton()) {
					camera->SetCameraState(RE::CameraState::kFirstPerson);
				}
				SendButtonUp(Settings::Get().adsReleaseKeyVK);
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class EndSink : public RE::BSTEventSink<RE::PlayerControls::PlayerIronSightsEndEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::PlayerControls::PlayerIronSightsEndEvent&, RE::BSTEventSource<RE::PlayerControls::PlayerIronSightsEndEvent>*) override
			{
				// Logged unconditionally (not gated on Locked) purely as
				// confirmation the event pair fires at all and roughly when -
				// useful ground truth for the Cutter-focus-mode question in
				// AdsBlocker.h's header comment. Remove once that's settled.
				REX::INFO("[VATS] ADS: PlayerIronSightsEndEvent");
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		StartSink s_startSink;
		EndSink   s_endSink;
	}

	void AdsBlocker::Start()
	{
		auto* startSource = RE::PlayerControls::PlayerIronSightsStartEvent::GetEventSource();
		auto* endSource = RE::PlayerControls::PlayerIronSightsEndEvent::GetEventSource();
		if (!startSource || !endSource) {
			REX::WARN("[VATS] ADS blocker: GetEventSource() returned null, not registered");
			return;
		}
		startSource->RegisterSink(&s_startSink);
		endSource->RegisterSink(&s_endSink);
		REX::INFO("[VATS] ADS blocker registered (PlayerIronSightsStartEvent/EndEvent)");
	}
}
