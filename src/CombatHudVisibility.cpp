#include "CombatHudVisibility.h"

#include "Settings.h"

#include <string>
#include <utility>
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

		// Exactly what this Lock changed, so Restore() puts back only that.
		// Carries a float rather than a bool because move mode restores x/y
		// coordinates while hide mode restores a visibility flag.
		struct Touched
		{
			std::string path;
			bool        isBoolean{ true };
			bool        originalBool{ true };
			float       originalNumber{ 0.0f };
		};
		std::vector<Touched> s_touched;

		// Property name for visibility. hudmenu is ActionScript 3 -
		// docs/hudmenu-decompiled/scripts/HitKillIndicator.as opens with
		// `package`, declares `public class HitKillIndicator extends
		// MovieClip` and imports `flash.display.MovieClip`, all of which are
		// AS3-only. In AS3 the property is `visible`; `_visible` is the AS2
		// spelling, and it is what every attempt here used until 2026-08-25.
		// That fits the evidence exactly: the container path resolves
		// (`root1.HitAndKillIndicator_mc available=true`) while every leaf
		// under it failed, i.e. the object was always reachable and only the
		// property name was wrong. AS2 spelling kept as a trailing fallback
		// since trying it costs nothing.
		constexpr const char* kVisibilityProperties[] = { "visible", "_visible" };

		// Hides one display object by whichever visibility property actually
		// resolves on it.
		bool TryHideLeaf(RE::Scaleform::GFx::ASMovieRootBase* a_root, const std::string& a_objectPath)
		{
			// Log whether the object itself resolves, separately from its
			// properties - that distinction is what finally localized this
			// bug, so keep it visible for the next one.
			const bool objectAvailable = a_root->IsAvailable(a_objectPath.c_str());
			REX::INFO("[VATS] combat-hud: object '{}' available={}", a_objectPath, objectAvailable);
			if (!objectAvailable) {
				return false;
			}

			for (const char* property : kVisibilityProperties) {
				const std::string         path = a_objectPath + "." + property;
				RE::Scaleform::GFx::Value current;
				if (!a_root->GetVariable(&current, path.c_str()) || !current.IsBoolean()) {
					continue;
				}
				const bool wasVisible = current.GetBoolean();
				if (wasVisible) {
					const RE::Scaleform::GFx::Value falseVal(false);
					a_root->SetVariable(path.c_str(), falseVal);
				}
				s_touched.push_back(Touched{ path, true, wasVisible, 0.0f });
				REX::INFO("[VATS] combat-hud: hid '{}' (was visible={})", path, wasVisible);
				return true;
			}

			REX::WARN("[VATS] combat-hud: '{}' resolves but neither 'visible' nor '_visible' read as a boolean on it", a_objectPath);
			return false;
		}

		// Shifts one display object by a_dx/a_dy in its parent's coordinate
		// space, remembering the originals so Restore() can put it back.
		// Used instead of hiding when Alexander wants the crit banner kept
		// but out of the way - it rides along on HitIndicator_mc, being a
		// child of it, so moving the parent is the only way to relocate the
		// banner without also losing it.
		bool TryMoveObject(RE::Scaleform::GFx::ASMovieRootBase* a_root, const std::string& a_objectPath, float a_dx, float a_dy)
		{
			if (!a_root->IsAvailable(a_objectPath.c_str())) {
				return false;
			}

			bool movedAny = false;
			for (const auto& [axis, delta] : { std::pair{ "x", a_dx }, std::pair{ "y", a_dy } }) {
				const std::string         path = a_objectPath + "." + axis;
				RE::Scaleform::GFx::Value current;
				if (!a_root->GetVariable(&current, path.c_str()) || !current.IsNumber()) {
					continue;
				}
				const double original = current.GetNumber();
				const RE::Scaleform::GFx::Value moved(original + static_cast<double>(delta));
				a_root->SetVariable(path.c_str(), moved);
				s_touched.push_back(Touched{ path, false, true, static_cast<float>(original) });
				// The parent clip's coordinate scale is unknown, so log the
				// original alongside the new value - one real session's
				// numbers make the INI offsets calibratable instead of
				// guessed.
				REX::INFO("[VATS] combat-hud: moved '{}' {:.1f} -> {:.1f}", path, original, original + delta);
				movedAny = true;
			}
			return movedAny;
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

		s_touched.clear();
		const auto& settings = Settings::Get();
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

			if (!containerAvailable) {
				continue;
			}

			// HitIndicator_mc/KillIndicator_mc are declared as public vars
			// on the class, so they are direct children of the container.
			// CritBanner_mc is nested one level deeper under HitIndicator_mc
			// specifically (per HitKillIndicator.as:
			// `this.HitIndicator_mc.CritBanner_mc`), not a sibling.
			// Hit and kill markers are always hidden - confirmed working
			// in-game 2026-08-25, Alexander reports no hit markers at all
			// while Locked.
			bool changedAny = false;
			changedAny |= TryHideLeaf(root, container + ".HitIndicator_mc");
			changedAny |= TryHideLeaf(root, container + ".KillIndicator_mc");

			// The crit visual is handled separately, and it is NOT simply a
			// child of the hit marker. It was assumed to be, on the strength
			// of HitKillIndicator.as declaring `this.HitIndicator_mc.
			// CritBanner_mc` - but Alexander observed a crit marker still
			// appearing in a session where the hit marker was verifiably
			// hidden, and a hidden parent cannot render a child. So whatever
			// he is seeing is a different object; the decompile also lists a
			// separate `CritText_mc` alongside `CritBanner_mc`.
			//
			// Rather than guess again, every candidate is probed for
			// availability and logged, and each one that resolves is either
			// hidden or moved per the setting. Probing costs nothing:
			// IsAvailable never constructs a Value, and a miss is a no-op.
			static constexpr const char* kCritPaths[] = {
				".HitIndicator_mc.CritBanner_mc",
				".CritBanner_mc",
				".CritText_mc",
				".HitIndicator_mc.CritText_mc",
				".KillIndicator_mc.CritBanner_mc",
			};
			for (const char* suffix : kCritPaths) {
				const std::string path = container + suffix;
				if (settings.moveCritMarker) {
					changedAny |= TryMoveObject(root, path, settings.critMarkerOffsetX, settings.critMarkerOffsetY);
				} else {
					changedAny |= TryHideLeaf(root, path);
				}
			}

			// Stop at the first container that actually worked. The
			// remaining candidates are aliases for the same objects
			// ("_root" resolves to the same clip as "root1"), so carrying on
			// re-processed objects already dealt with - harmless, but it
			// recorded confusing "was visible=false" entries for things this
			// call had itself just hidden a moment earlier.
			if (changedAny) {
				return;
			}
		}

		REX::WARN("[VATS] combat-hud: none of the candidate HitKillIndicator paths resolved - see kContainerNames/kFallbackParents in CombatHudVisibility.cpp, or the logged HUDMenu root path above for a better guess");
	}

	void CombatHudVisibility::Restore()
	{
		auto* root = GetHudMovieRoot();
		if (!root) {
			return;
		}
		for (const auto& touched : s_touched) {
			if (touched.isBoolean) {
				if (touched.originalBool) {
					const RE::Scaleform::GFx::Value trueVal(true);
					root->SetVariable(touched.path.c_str(), trueVal);
				}
			} else {
				const RE::Scaleform::GFx::Value original(static_cast<double>(touched.originalNumber));
				root->SetVariable(touched.path.c_str(), original);
			}
		}
		s_touched.clear();
	}
}
