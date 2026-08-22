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
			// AimAssist::Start() temporarily disabled 2026-08-22 for
			// isolation testing: Alexander confirmed the last crash-free
			// build predates the steering-loop/mouse-hook feature (it
			// still "snapped" onto the target instead of gradually
			// converging). This installs a permanent WH_MOUSE_LL hook
			// covering the whole session, not just while Locked - if
			// disabling it alone stops the N-press crash, that's a clean,
			// single-variable confirmation pointing at this file
			// specifically. Re-enable once that's confirmed either way.
			// VATS::AimAssist::Start();
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
