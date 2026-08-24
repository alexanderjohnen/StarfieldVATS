#include "DamageNumbersVisibility.h"

namespace VATS
{
	namespace
	{
		// Real setting name, found 2026-08-24 via a real StarfieldPrefs.ini
		// sample (a player's saved settings after toggling Interface >
		// "Show Damage Numbers" off in-game) - `bDamageNumbersEnabled`
		// under `[Interface]`, same naming convention as
		// `bCrosshairEnabled` under `[GamePlay]` (CrosshairVisibility.cpp).
		// Kept as a small candidate list anyway, same defensive pattern as
		// CrosshairVisibility - trying an extra guess is free, and neither
		// collection's GetSetting can crash on a miss.
		constexpr const char* kBoolCandidates[] = {
			"bDamageNumbersEnabled:Interface",
			// Fallback guesses only, in case the confirmed name above is
			// wrong for this game version:
			"bShowDamageNumbers:Interface",
			"bDamageNumbersEnabled:GamePlay",
		};

		RE::Setting* s_setting = nullptr;
		bool         s_originalValue = true;
		bool         s_searched = false;

		template <class T>
		[[nodiscard]] RE::Setting* TryFind(T* a_collection, const char* a_name)
		{
			return a_collection ? a_collection->GetSetting(a_name) : nullptr;
		}

		void FindSetting()
		{
			s_searched = true;
			auto* gameSettings = RE::GameSettingCollection::GetSingleton();
			auto* iniPrefs = RE::INIPrefSettingCollection::GetSingleton();

			for (const char* name : kBoolCandidates) {
				if (auto* s = TryFind(gameSettings, name)) {
					s_setting = s;
					REX::INFO("[VATS] damage numbers: found setting '{}' via GameSettingCollection", name);
					return;
				}
				if (auto* s = TryFind(iniPrefs, name)) {
					s_setting = s;
					REX::INFO("[VATS] damage numbers: found setting '{}' via INIPrefSettingCollection", name);
					return;
				}
			}

			REX::WARN("[VATS] damage numbers: none of the candidate settings resolved in either collection, hide/restore is a no-op");
		}
	}

	void DamageNumbersVisibility::Hide()
	{
		if (!s_searched) {
			FindSetting();
		}
		if (!s_setting) {
			return;
		}
		s_originalValue = s_setting->GetBool();
		s_setting->SetBool(false);
		REX::INFO("[VATS] damage numbers: hidden (was {})", s_originalValue);
	}

	void DamageNumbersVisibility::Restore()
	{
		if (!s_setting) {
			return;
		}
		s_setting->SetBool(s_originalValue);
		REX::INFO("[VATS] damage numbers: restored to {}", s_originalValue);
	}
}
