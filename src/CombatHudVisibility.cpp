#include "CombatHudVisibility.h"

#include <string>
#include <vector>

namespace VATS
{
	namespace
	{
		// Static fallback prefixes, kept in case GetRealRootPrefixes below
		// can't resolve anything - common Bethesda HUD root-path
		// conventions (Skyrim/FO4-era AS2 style, since these clip names use
		// classic AS2 MovieClip "_mc" naming despite HONKCORE's AS3 widget
		// framework sitting on top).
		constexpr const char* kFallbackPrefixes[] = {
			"_root.HitDamageIndicatorClip",
			"_root.HUDMovieBaseInstance.HitDamageIndicatorClip",
			"_root",
			"_root.HUDMovieBaseInstance",
		};
		constexpr const char* kClipNames[] = {
			"DamageNumberText_mc",
			"CritText_mc",
			"CritBanner_mc",
			"HitIndicator_mc",
			"KillIndicator_mc",
		};

		std::vector<std::string> s_prefixes;
		bool                     s_prefixesBuilt = false;

		[[nodiscard]] RE::Scaleform::GFx::ASMovieRootBase* GetHudMovieRoot()
		{
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMenuMovie("HUDMenu") : nullptr;
			return movie ? movie->asMovieRoot.get() : nullptr;
		}

		// Only the parent-path candidates are cached (built once) - unlike
		// the old FindPaths(), this doesn't try to resolve actual (prefix,
		// clip) paths up front, since the clips themselves come and go.
		// HideActive()/Restore() re-check availability fresh every call.
		void BuildPrefixes()
		{
			s_prefixesBuilt = true;
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu("HUDMenu") : nullptr;
			if (menu) {
				const char* realRoot = menu->GetRootPath();
				if (realRoot && realRoot[0] != '\0') {
					REX::INFO("[VATS] combat-hud: HUDMenu root path = '{}'", realRoot);
					s_prefixes.push_back(realRoot);
					s_prefixes.push_back(std::string(realRoot) + ".HitDamageIndicatorClip");
				}
			}
			for (const char* fallback : kFallbackPrefixes) {
				s_prefixes.push_back(fallback);
			}
		}
	}

	void CombatHudVisibility::HideActive()
	{
		if (!s_prefixesBuilt) {
			BuildPrefixes();
		}
		auto* root = GetHudMovieRoot();
		if (!root) {
			return;
		}
		for (const auto& prefix : s_prefixes) {
			for (const char* clip : kClipNames) {
				const std::string path = prefix + "." + clip + "._visible";
				RE::Scaleform::GFx::Value current;
				if (!root->GetVariable(&current, path.c_str()) || !current.IsBoolean() || !current.GetBoolean()) {
					continue;  // not present, wrong type, or already hidden - nothing to do
				}
				const RE::Scaleform::GFx::Value falseVal(false);
				root->SetVariable(path.c_str(), falseVal);
				REX::INFO("[VATS] combat-hud: hid newly-visible '{}'", path);
			}
		}
	}

	void CombatHudVisibility::Restore()
	{
		auto* root = GetHudMovieRoot();
		if (!root || !s_prefixesBuilt) {
			return;
		}
		for (const auto& prefix : s_prefixes) {
			for (const char* clip : kClipNames) {
				const std::string path = prefix + "." + clip + "._visible";
				if (!root->IsAvailable(path.c_str())) {
					continue;
				}
				const RE::Scaleform::GFx::Value trueVal(true);
				root->SetVariable(path.c_str(), trueVal);
			}
		}
	}
}
