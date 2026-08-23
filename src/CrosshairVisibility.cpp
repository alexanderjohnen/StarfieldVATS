#include "CrosshairVisibility.h"

namespace VATS
{
	namespace
	{
		// Best-guess candidate INIPref key names for Starfield's Interface >
		// Crosshair toggle, tried in order - Bethesda's established
		// bXxx:Category naming convention for boolean prefs, no confirmed
		// source for the exact key. Whichever one actually resolves first
		// gets logged and used; if the log shows none resolved, check
		// Documents\My Games\Starfield\StarfieldPrefs.ini after toggling the
		// option in-game for the real key and add it here.
		constexpr const char* kCandidateNames[] = {
			"bCrosshairEnabled:Interface",
			"bShowCrosshair:Interface",
			"bCrosshairEnable:Interface",
			"bEnableCrosshair:Interface",
			"bCrosshair:Interface",
		};

		RE::Setting* s_setting = nullptr;
		bool         s_originalValue = true;
		bool         s_searched = false;

		void FindSetting()
		{
			s_searched = true;
			auto* collection = RE::INIPrefSettingCollection::GetSingleton();
			if (!collection) {
				REX::ERROR("[VATS] crosshair: INIPrefSettingCollection singleton unavailable");
				return;
			}
			for (const char* name : kCandidateNames) {
				if (auto* setting = collection->GetSetting(name)) {
					s_setting = setting;
					REX::INFO("[VATS] crosshair: found backing setting '{}'", name);
					return;
				}
			}
			REX::WARN("[VATS] crosshair: none of the candidate setting names resolved, hide/restore is a no-op - check StarfieldPrefs.ini for the real key and update CrosshairVisibility.cpp's kCandidateNames");
		}
	}

	void CrosshairVisibility::Hide()
	{
		if (!s_searched) {
			FindSetting();
		}
		if (!s_setting) {
			return;
		}
		s_originalValue = s_setting->GetBool();
		s_setting->SetBool(false);
		REX::INFO("[VATS] crosshair: hidden (was {})", s_originalValue);
	}

	void CrosshairVisibility::Restore()
	{
		if (!s_setting) {
			return;
		}
		s_setting->SetBool(s_originalValue);
		REX::INFO("[VATS] crosshair: restored to {}", s_originalValue);
	}
}
