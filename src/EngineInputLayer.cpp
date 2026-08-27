#include "EngineInputLayer.h"

namespace VATS
{
	namespace
	{
		RE::BSInputEnableLayer* s_layer = nullptr;
	}

	void EngineInputLayer::Init()
	{
		auto* mgr = RE::BSInputEnableManager::GetSingleton();
		if (!mgr) {
			VATS_ERROR("[VATS] BSInputEnableManager singleton unavailable, engine input layer not installed");
			return;
		}
		if (!mgr->AllocateNewLayer(&s_layer, "StarfieldVATS")) {
			VATS_ERROR("[VATS] BSInputEnableManager::AllocateNewLayer failed");
			s_layer = nullptr;
			return;
		}
		VATS_LOG("[VATS] engine input layer allocated, id={}", s_layer->GetLayerID());
	}

	void EngineInputLayer::SetBlocked(bool a_blocked)
	{
		if (!s_layer) {
			return;
		}
		const bool enable = !a_blocked;
		s_layer->EnableUserEvent(RE::USER_EVENT_FLAG::POVSwitch, enable);
		s_layer->EnableUserEvent(RE::USER_EVENT_FLAG::TabMenuMaybe, enable);
		s_layer->EnableUserEvent(RE::USER_EVENT_FLAG::WheelZoom, enable);
		VATS_LOG("[VATS] engine input layer: POVSwitch/TabMenuMaybe/WheelZoom {}", a_blocked ? "disabled" : "enabled");
	}

	void EngineInputLayer::SetAdsBlocked(bool a_blocked)
	{
		if (!s_layer) {
			return;
		}
		s_layer->EnableUserEvent(RE::USER_EVENT_FLAG::Fighting, !a_blocked);
		VATS_LOG("[VATS] engine input layer: Fighting {}", a_blocked ? "disabled" : "enabled");
	}
}
