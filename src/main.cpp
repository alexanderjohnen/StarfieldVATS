#include "AdsBlocker.h"
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
			// Re-enabled 2026-08-24 under a new, much lower-risk mechanism:
			// ends the VATS lock on ADS instead of trying to block ADS
			// itself. No BSTEventSource<T>::RegisterSink call anymore (that
			// was the previous version's crash - see AdsBlocker.h for the
			// full four-attempt trail), just the same WH_MOUSE_LL detection
			// AimAssist/BackKeyInterceptor already use safely.
			VATS::AdsBlocker::Start();
			VATS::EngineInputLayer::Init();
			// A hit-confirmation listener lived here until 2026-08-28. It
			// crashed on every launch and was never made to work: the REL::IDs
			// it needs are declared in the CommonLibSF headers but not mapped
			// for game version 1.16.244.0, so it aborts at load rather than
			// running. Removed rather than left commented out - see
			// docs/FINDINGS.md, "Crash-causing gaps", which keeps the detail
			// and the idea for whenever those IDs get mapped.
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

	VATS_LOG("{} v{} loaded", SFSE::GetPluginName(), SFSE::GetPluginVersion().string());

	const auto* messaging = SFSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSFSEMessage)) {
		VATS_ERROR("failed to register SFSE message listener");
		return false;
	}

	// Arms the DXGI/D3D12 hook chain. Safe to call this early — the actual
	// swapchain/device don't exist yet, so this just waits for them.
	VATS::UI::Install();
	VATS::UI::SetDrawCallback(&VATS::UI::Draw);

	return true;
}
