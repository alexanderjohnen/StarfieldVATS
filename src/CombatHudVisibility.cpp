#include "CombatHudVisibility.h"

#include <string>
#include <vector>

namespace VATS
{
	namespace
	{
		// Candidate parent-path prefixes for the clips below - common
		// Bethesda HUD root-path conventions (Skyrim/FO4-era AS2 style,
		// since these clip names use classic AS2 MovieClip "_mc" naming
		// despite HONKCORE's AS3 widget framework sitting on top). Tried
		// with and without a "HitDamageIndicatorClip" container segment,
		// since it's unconfirmed whether that name is itself a path
		// component or just a symbol/class name.
		constexpr const char* kPathPrefixes[] = {
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

		struct HiddenEntry
		{
			std::string path;
			bool        originalValue = true;
		};

		std::vector<HiddenEntry> s_entries;
		bool                     s_searched = false;

		[[nodiscard]] RE::Scaleform::GFx::ASMovieRootBase* GetHudMovieRoot()
		{
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMenuMovie("HUDMenu") : nullptr;
			return movie ? movie->asMovieRoot.get() : nullptr;
		}

		void FindPaths()
		{
			s_searched = true;
			auto* root = GetHudMovieRoot();
			if (!root) {
				REX::WARN("[VATS] combat-hud: HUDMenu movie unavailable");
				return;
			}
			for (const char* prefix : kPathPrefixes) {
				for (const char* clip : kClipNames) {
					std::string path = std::string(prefix) + "." + clip + "._visible";
					if (root->IsAvailable(path.c_str())) {
						s_entries.push_back(HiddenEntry{ std::move(path), true });
						REX::INFO("[VATS] combat-hud: found '{}'", s_entries.back().path);
					}
				}
			}
			if (s_entries.empty()) {
				REX::WARN("[VATS] combat-hud: none of the candidate paths resolved, hide/restore is a no-op - check kPathPrefixes/kClipNames in CombatHudVisibility.cpp");
			}
		}
	}

	void CombatHudVisibility::Hide()
	{
		if (!s_searched) {
			FindPaths();
		}
		auto* root = GetHudMovieRoot();
		if (!root) {
			return;
		}
		for (auto& entry : s_entries) {
			RE::Scaleform::GFx::Value original;
			if (root->GetVariable(&original, entry.path.c_str())) {
				entry.originalValue = original.GetBoolean();
			}
			const RE::Scaleform::GFx::Value falseVal(false);
			root->SetVariable(entry.path.c_str(), falseVal);
		}
		if (!s_entries.empty()) {
			REX::INFO("[VATS] combat-hud: hid {} element(s)", s_entries.size());
		}
	}

	void CombatHudVisibility::Restore()
	{
		auto* root = GetHudMovieRoot();
		if (!root) {
			return;
		}
		for (const auto& entry : s_entries) {
			const RE::Scaleform::GFx::Value val(entry.originalValue);
			root->SetVariable(entry.path.c_str(), val);
		}
	}
}
