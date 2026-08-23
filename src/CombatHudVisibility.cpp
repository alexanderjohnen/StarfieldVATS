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

			// IMenu::GetRootPath() is a real virtual call on the live
			// HUDMenu instance (RE/I/IMenu.h - every concrete menu class
			// implements its own root path string; GameMenuBase.h's own
			// LoadMovie() override calls this exact function to resolve
			// its code object). Using the actual value instead of guessing
			// "_root" - the earlier version of this file guessed and found
			// nothing at all.
			std::vector<std::string> prefixes;
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu("HUDMenu") : nullptr;
			if (menu) {
				const char* realRoot = menu->GetRootPath();
				if (realRoot && realRoot[0] != '\0') {
					REX::INFO("[VATS] combat-hud: HUDMenu root path = '{}'", realRoot);
					prefixes.push_back(realRoot);
					prefixes.push_back(std::string(realRoot) + ".HitDamageIndicatorClip");
				}
			}
			for (const char* fallback : kFallbackPrefixes) {
				prefixes.push_back(fallback);
			}

			for (const auto& prefix : prefixes) {
				for (const char* clip : kClipNames) {
					std::string path = prefix + "." + clip + "._visible";
					if (root->IsAvailable(path.c_str())) {
						s_entries.push_back(HiddenEntry{ path, true });
						REX::INFO("[VATS] combat-hud: found '{}'", s_entries.back().path);
					}
				}
			}
			if (s_entries.empty()) {
				REX::WARN("[VATS] combat-hud: none of the candidate paths resolved, hide/restore is a no-op - check kFallbackPrefixes/kClipNames in CombatHudVisibility.cpp, or whether the logged HUDMenu root path above suggests a different nesting");
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
