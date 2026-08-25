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
		std::string              s_realRoot;  // e.g. "root1" - kept for the baseline sanity probe in HideActive()
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
					s_realRoot = realRoot;
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

			// Added 2026-08-25: every candidate above uses dot-notation and
			// still failed to resolve per HANDOFF.md ("none of the
			// candidate paths resolved"), even the confirmed real name
			// combined with the confirmed real root and the standard
			// "_root" alias. Untried alternative: GFx/Scaleform also
			// supports the older AS1/2 slash-notation target-path syntax
			// ("/root1/Name" instead of "root1.Name") - a real, distinct
			// addressing convention, not a blind guess. IsAvailable() is
			// already proven safe to call on an unresolved path (see
			// HideActive()'s comment) so widening the search costs
			// nothing.
			for (const auto& parent : parents) {
				for (const char* name : kContainerNames) {
					s_containerPaths.push_back("/" + parent + "/" + name);
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

		// Baseline sanity probe (2026-08-25): every candidate container
		// path has failed IsAvailable() so far, including the JPEXS-
		// confirmed real name at the JPEXS-confirmed real root - enough
		// misses that the problem might not be the path string at all.
		// CommonLibSF's own GameMenuBase.h shows the ENGINE ITSELF doing
		// `uiMovie->asMovieRoot->GetVariable(&menuObj, GetRootPath())` -
		// i.e. GetVariable on the bare root path, no child appended,
		// resolves the root clip object as a Value. Mirroring that exact,
		// engine-proven call here: if this ALSO fails, the fault is
		// upstream of any path guess (wrong root/movie pointer, wrong
		// timing) and no amount of path-string variation will ever fix
		// this. If it SUCCEEDS, the root chain is proven good and the
		// fault is specifically in reaching a named child from it.
		if (root && !s_realRoot.empty()) {
			RE::Scaleform::GFx::Value rootVal;
			const bool                gotRoot = root->GetVariable(&rootVal, s_realRoot.c_str());
			REX::INFO("[VATS] combat-hud: baseline GetVariable('{}') ok={} isObject={} isDisplayObject={} isUndefined={}",
				s_realRoot, gotRoot, gotRoot && rootVal.IsObject(), gotRoot && rootVal.IsDisplayObject(), gotRoot && rootVal.IsUndefined());
		}

		s_hidden.clear();
		for (const auto& container : s_containerPaths) {
			// Diagnostic (2026-08-24): even the confirmed-real
			// "root1.HitAndKillIndicator_mc" container didn't resolve on
			// the first try after using it. IsAvailable() never
			// constructs a Value (unlike GetVariable), so it's safe to
			// call on the bare container path too, unlike the bare-clip
			// GetVariable probe that crashed HealthWidgetReader.cpp -
			// logs exactly which level (container itself vs. its
			// children) is the one that doesn't resolve.
			const bool containerAvailable = root->IsAvailable(container.c_str());
			REX::INFO("[VATS] combat-hud: container '{}' available={}", container, containerAvailable);

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
