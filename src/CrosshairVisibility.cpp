#include "CrosshairVisibility.h"

namespace VATS
{
	namespace
	{
		// Real setting names, found 2026-08-23 via a community-maintained
		// dump of Starfield's known INI settings (stepmodifications.org
		// forum topic 19019 — strings extracted from the executable, default
		// values pulled via the in-game `getini` console command) rather
		// than guessing: `bCrosshairEnabled=1` under [GamePlay], and
		// `fCrosshairAlphaPercent=100.0` under [Interface]. Neither has been
		// tried in-game yet, and it's unconfirmed which live collection
		// (GameSettingCollection vs. INIPrefSettingCollection) actually
		// holds the resolved Setting object at runtime — the bool is tried
		// first (a clean hide), falling back to zeroing the float (opacity)
		// if the bool isn't found. Both collections are tried for each name
		// since a wrong guess there is equally free (GameSettingCollection::
		// GetSetting is a REL::ID call but an extremely fundamental/
		// ubiquitous one; INIPrefSettingCollection::GetSetting has no
		// REL::ID of its own at all, just a linked-list name lookup) — see
		// CrosshairVisibility.h for why neither lookup path can crash on a
		// miss.
		constexpr const char* kBoolCandidates[] = {
			"bCrosshairEnabled:GamePlay",
			// Earlier (2026-08-23) guesses, kept as a fallback only:
			"bCrosshairEnabled:Interface",
		};
		constexpr const char* kFloatCandidates[] = {
			"fCrosshairAlphaPercent:Interface",
		};

		RE::Setting* s_boolSetting = nullptr;
		RE::Setting* s_floatSetting = nullptr;
		bool         s_originalBool = true;
		float        s_originalFloat = 100.0f;
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
					s_boolSetting = s;
					REX::INFO("[VATS] crosshair: found bool setting '{}' via GameSettingCollection", name);
					return;
				}
				if (auto* s = TryFind(iniPrefs, name)) {
					s_boolSetting = s;
					REX::INFO("[VATS] crosshair: found bool setting '{}' via INIPrefSettingCollection", name);
					return;
				}
			}

			for (const char* name : kFloatCandidates) {
				if (auto* s = TryFind(gameSettings, name)) {
					s_floatSetting = s;
					REX::INFO("[VATS] crosshair: found float setting '{}' via GameSettingCollection", name);
					return;
				}
				if (auto* s = TryFind(iniPrefs, name)) {
					s_floatSetting = s;
					REX::INFO("[VATS] crosshair: found float setting '{}' via INIPrefSettingCollection", name);
					return;
				}
			}

			REX::WARN("[VATS] crosshair: none of the candidate settings resolved in either collection, hide/restore is a no-op");
		}
	}

	void CrosshairVisibility::Hide()
	{
		if (!s_searched) {
			FindSetting();
		}
		if (s_boolSetting) {
			s_originalBool = s_boolSetting->GetBool();
			s_boolSetting->SetBool(false);
			REX::INFO("[VATS] crosshair: hidden via bool setting (was {})", s_originalBool);
		} else if (s_floatSetting) {
			s_originalFloat = s_floatSetting->GetFloat();
			s_floatSetting->SetFloat(0.0f);
			REX::INFO("[VATS] crosshair: hidden via alpha=0 (was {})", s_originalFloat);
		}
	}

	void CrosshairVisibility::Restore()
	{
		if (s_boolSetting) {
			s_boolSetting->SetBool(s_originalBool);
			REX::INFO("[VATS] crosshair: restored bool to {}", s_originalBool);
		} else if (s_floatSetting) {
			s_floatSetting->SetFloat(s_originalFloat);
			REX::INFO("[VATS] crosshair: restored alpha to {}", s_originalFloat);
		}
	}
}
