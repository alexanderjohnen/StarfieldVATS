#include "CombatHudVisibility.h"

#include <string>
#include <vector>

namespace VATS
{
	namespace
	{
		// Instance name for the HitKillIndicator container (class
		// HitKillIndicator, see scripts/HitKillIndicator.as in
		// docs/hudmenu-decompiled/), CONFIRMED 2026-08-24 by Alexander
		// directly in JPEXS's timeline view (hudmenu.gfx, frame 1's display
		// list): `PlaceObject2 (chid: 275, dpt: 127, nm:
		// "HitAndKillIndicator_mc")` - placed directly on the main
		// timeline, no nesting. Old guesses kept as harmless fallbacks only
		// (never resolved, but a miss costs nothing).
		constexpr const char* kContainerNames[] = {
			"HitAndKillIndicator_mc",  // confirmed real name
			"HitKillIndicator_mc",
			"HitKillIndicator",
		};
		constexpr const char* kFallbackParents[] = {
			"_root",
			"_root.HUDMovieBaseInstance",
			"_root.HitDamageIndicatorClip",  // old guess, kept just in case
		};

		std::vector<std::string> s_containerPaths;
		bool                     s_searched = false;

		[[nodiscard]] RE::Scaleform::GFx::ASMovieRootBase* GetHudMovieRoot()
		{
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMenuMovie("HUDMenu") : nullptr;
			return movie ? movie->asMovieRoot.get() : nullptr;
		}

		// Builds the list of full container paths to try (parent + name),
		// once. Real HUDMenu root path (IMenu::GetRootPath()) tried first,
		// then static fallbacks - same pattern CrosshairVisibility/
		// DamageNumbersVisibility already use successfully.
		void BuildContainerPaths()
		{
			s_searched = true;
			std::vector<std::string> parents;
			auto*                    ui = RE::UI::GetSingleton();
			auto                     menu = ui ? ui->GetMenu("HUDMenu") : nullptr;
			if (menu) {
				if (const char* realRoot = menu->GetRootPath(); realRoot && realRoot[0] != '\0') {
					REX::INFO("[VATS] combat-hud: HUDMenu root path = '{}'", realRoot);
					parents.push_back(realRoot);
					parents.push_back(std::string(realRoot) + ".HUDMovieBaseInstance");
				}
			}
			for (const char* fallback : kFallbackParents) {
				parents.push_back(fallback);
			}

			for (const auto& parent : parents) {
				for (const char* name : kContainerNames) {
					s_containerPaths.push_back(parent + "." + name);
				}
			}
		}

		// (path, wasVisible) pairs actually toggled this Lock, so Restore()
		// only touches what Hide() actually changed.
		std::vector<std::pair<std::string, bool>> s_hidden;

		void TryHideLeaf(RE::Scaleform::GFx::ASMovieRootBase* a_root, const std::string& a_path)
		{
			RE::Scaleform::GFx::Value current;
			if (!a_root->GetVariable(&current, a_path.c_str()) || !current.IsBoolean()) {
				return;  // not present or not a boolean - nothing to do
			}
			const bool wasVisible = current.GetBoolean();
			if (wasVisible) {
				const RE::Scaleform::GFx::Value falseVal(false);
				a_root->SetVariable(a_path.c_str(), falseVal);
			}
			s_hidden.emplace_back(a_path, wasVisible);
			REX::INFO("[VATS] combat-hud: found '{}' (was visible={})", a_path, wasVisible);
		}
	}

	void CombatHudVisibility::HideActive()
	{
		if (!s_searched) {
			BuildContainerPaths();
		}
		auto* root = GetHudMovieRoot();
		if (!root) {
			return;
		}

		s_hidden.clear();
		for (const auto& container : s_containerPaths) {
			// HitIndicator_mc/KillIndicator_mc are direct children of the
			// container; CritBanner_mc is nested one level deeper under
			// HitIndicator_mc specifically (per HitKillIndicator.as:
			// `this.HitIndicator_mc.CritBanner_mc`), not a sibling.
			TryHideLeaf(root, container + ".HitIndicator_mc._visible");
			TryHideLeaf(root, container + ".KillIndicator_mc._visible");
			TryHideLeaf(root, container + ".HitIndicator_mc.CritBanner_mc._visible");
		}
		if (s_hidden.empty()) {
			REX::WARN("[VATS] combat-hud: none of the candidate HitKillIndicator paths resolved - see kContainerNames/kFallbackParents in CombatHudVisibility.cpp, or the logged HUDMenu root path above for a better guess");
		}
	}

	void CombatHudVisibility::Restore()
	{
		auto* root = GetHudMovieRoot();
		if (!root) {
			return;
		}
		for (const auto& [path, wasVisible] : s_hidden) {
			if (wasVisible) {
				const RE::Scaleform::GFx::Value trueVal(true);
				root->SetVariable(path.c_str(), trueVal);
			}
		}
		s_hidden.clear();
	}
}
