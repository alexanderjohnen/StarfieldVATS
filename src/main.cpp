#include "AimAssist.h"
#include "BackKeyInterceptor.h"
#include "EngineInputLayer.h"
#include "HotkeyWatcher.h"
#include "Settings.h"
#include "UI/D3DHook.h"
#include "UI/Overlay.h"

namespace
{
	void OnSFSEMessage(SFSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SFSE::MessagingInterface::kPostDataLoad:
			VATS::Settings::Get().Load();
			VATS::HotkeyWatcher::Start();
			VATS::BackKeyInterceptor::Start();
			// Re-enabled 2026-08-22: was briefly disabled to isolate the
			// N-press hard-crash-without-log, but the crash reproduced
			// with AimAssist off too — its mouse hook is cleared as a
			// suspect. Real cause tracked down to HasDetectionLOS (see
			// Targeting.cpp).
			VATS::AimAssist::Start();
			VATS::EngineInputLayer::Init();
			break;
		default:
			break;
		}
	}
}

SFSE_PLUGIN_PRELOAD(const SFSE::PreLoadInterface* a_sfse)
{
	SFSE::Init(a_sfse);

	return true;
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
	SFSE::Init(a_sfse);

	REX::INFO("{} v{} loaded", SFSE::GetPluginName(), SFSE::GetPluginVersion().string());

	const auto* messaging = SFSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSFSEMessage)) {
		REX::ERROR("failed to register SFSE message listener");
		return false;
	}

	// Arms the DXGI/D3D12 hook chain. Safe to call this early — the actual
	// swapchain/device don't exist yet, so this just waits for them.
	VATS::UI::Install();
	VATS::UI::SetDrawCallback(&VATS::UI::Draw);

	return true;
}
