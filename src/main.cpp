#include "AdsBlocker.h"
#include "AimAssist.h"
#include "BackKeyInterceptor.h"
#include "EngineInputLayer.h"
#include "HitEventLogger.h"
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
			// Disabled 2026-08-23: instant hard crash on launch, same clean
			// CommonLibSF abort as HitEventLogger below ("REL/IDDB.cpp(417):
			// Failed to find offset for Address Library ID! Invalid ID: 0")
			// - one of PlayerIronSightsStartEvent::GetEventSource,
			// PlayerIronSightsEndEvent::GetEventSource, or
			// BSTEventSource<T>::RegisterSink itself isn't mapped for
			// 1.16.244.0. Confirmed via screenshot on the main menu, before
			// any save even loaded. See AdsBlocker.h - needs a mapped
			// alternative or the gap reported upstream before this can be
			// re-enabled; the camera-state-polling and OS-input-hook
			// approaches tried before this one both compiled/ran safely but
			// had no actual effect on ADS, so there's no safe fallback
			// version of this feature to fall back to right now.
			// VATS::AdsBlocker::Start();
			VATS::EngineInputLayer::Init();
			// Disabled 2026-08-23: instant hard crash on every launch
			// (a clean CommonLibSF-level abort, not a wild-pointer crash -
			// "REL/IDDB.cpp(417): Failed to find offset for Address
			// Library ID! Invalid ID: 0"). One of the REL::IDs this needs
			// (RE::TESHitEvent::GetEventSource and/or
			// BSTEventSource<T>::RegisterSink - this is the first time
			// this project has used either) isn't actually mapped in the
			// Address Library for game version 1.16.244.0, despite being
			// declared in the CommonLibSF header - compiles fine, never
			// verified against this Starfield build. See HitEventLogger.h
			// for the full ground-truth-hit-confirmation idea; needs a
			// mapped alternative (or the ID gap reported upstream) before
			// this can be re-enabled.
			// VATS::HitEventLogger::Start();
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
