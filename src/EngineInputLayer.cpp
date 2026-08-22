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
			REX::ERROR("[VATS] BSInputEnableManager singleton unavailable, engine input layer not installed");
			return;
		}
		if (!mgr->AllocateNewLayer(&s_layer, "StarfieldVATS")) {
			REX::ERROR("[VATS] BSInputEnableManager::AllocateNewLayer failed");
			s_layer = nullptr;
			return;
		}
		REX::INFO("[VATS] engine input layer allocated, id={}", s_layer->GetLayerID());
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
		REX::INFO("[VATS] engine input layer: POVSwitch/TabMenuMaybe/WheelZoom {}", a_blocked ? "disabled" : "enabled");
	}
}
