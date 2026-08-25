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

		// Read-only, one-shot per session. Answers two separate questions
		// that both have to be settled before anything else is built.
		//
		// 1) Where the object sits, and how big the coordinate space is.
		//    No offset can be chosen sensibly without this, and a blind
		//    guess risks flinging the crit banner off-screen.
		//
		// 2) Whether a CRIT can be DETECTED rather than merely relocated -
		//    Alexander's question, and the better design if it works: hide
		//    the vanilla visuals outright and draw our own crit indicator
		//    on the VATS box, with nothing moved and so nothing to restore.
		//    The engine-side route is closed (BSUIDataManager, which is
		//    what HitKillIndicator.as subscribes to for `uHitType ===
		//    CRITICAL`, does not appear anywhere in CommonLibSF, so reaching
		//    it would mean reverse-engineering an unmapped interface - the
		//    exact category that has crashed this project twice). The
		//    Scaleform side may be readable instead: a crit makes the AS
		//    call `CritBanner_mc.gotoAndPlay("CriticalHit")`, so the clip's
		//    playhead moves. If currentFrame/currentLabel read as sane
		//    values here, polling one of them detects crits without writing
		//    anything at all.
		void LogInspection(RE::Scaleform::GFx::ASMovieRootBase* a_root, const std::string& a_hitIndicatorPath)
		{
			static bool s_logged = false;
			if (s_logged) {
				return;
			}
			s_logged = true;

			const std::string paths[] = { a_hitIndicatorPath, a_hitIndicatorPath + ".CritBanner_mc" };
			for (const auto& objectPath : paths) {
				if (!a_root->IsAvailable(objectPath.c_str())) {
					REX::INFO("[VATS] combat-hud: inspect '{}' not available", objectPath);
					continue;
				}
				for (const char* property : { "x", "y", "width", "height", "currentFrame", "totalFrames", "currentLabel" }) {
					const std::string         path = objectPath + "." + property;
					RE::Scaleform::GFx::Value value;
					if (!a_root->GetVariable(&value, path.c_str())) {
						REX::INFO("[VATS] combat-hud: inspect {} unreadable", path);
					} else if (value.IsNumber()) {
						REX::INFO("[VATS] combat-hud: inspect {} = {:.1f}", path, value.GetNumber());
					} else if (value.IsString()) {
						REX::INFO("[VATS] combat-hud: inspect {} = '{}'", path, value.GetString());
					} else {
						REX::INFO("[VATS] combat-hud: inspect {} present but neither number nor string", path);
					}
				}
			}

			for (const char* stagePath : { "root1.stage.stageWidth", "root1.stage.stageHeight" }) {
				RE::Scaleform::GFx::Value value;
				if (a_root->GetVariable(&value, stagePath) && value.IsNumber()) {
					REX::INFO("[VATS] combat-hud: inspect {} = {:.1f}", stagePath, value.GetNumber());
				}
			}
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
			// Hide the CONTAINER itself first, not just its children. This
			// is what makes full suppression possible at all.
			//
			// Hiding the children works for ordinary hits but breaks on
			// crits: a crit runs the indicator's timeline, and entering a
			// keyframe re-instantiates the timeline-placed children with
			// their authored properties, putting `visible` back to true. Any
			// property we set on a child - visibility, position, alpha - is
			// subject to being reset that way, so no one-shot write to a
			// child can hold. Re-applying it would mean polling the AS3 VM
			// for the whole lock, which is the mechanism already under
			// suspicion for an earlier crash here.
			//
			// The container sidesteps that entirely: HitAndKillIndicator_mc
			// is placed on the main timeline, which does not animate, so
			// nothing re-instantiates it and nothing resets what we set. A
			// hidden container cannot render any descendant no matter how
			// often those descendants are rebuilt. Alexander's call
			// (2026-08-25) is to suppress crit feedback outright and rely on
			// the game's own crit sound cue, which this delivers without a
			// single per-frame write.
			bool changedAny = false;
			changedAny |= TryHideLeaf(root, container);

			// The children are still hidden as well. Redundant while the
			// container hide holds, harmless if it does not, and it keeps
			// ordinary-hit suppression working even in the latter case.
			changedAny |= TryHideLeaf(root, container + ".HitIndicator_mc");
			changedAny |= TryHideLeaf(root, container + ".KillIndicator_mc");

			changedAny |= TryHideLeaf(root, container + ".HitIndicator_mc.CritBanner_mc");

			// Hiding does not fully hold for crits, and that is the whole
			// reason move mode exists. Alexander's observation: ordinary
			// hits show nothing while Locked (the hide works), but a
			// CRITICAL hit briefly flashes up both the crit banner and a hit
			// marker. That fits Flash's keyframe behaviour - entering a
			// keyframe re-instantiates the timeline-placed display objects
			// with their authored properties, resetting `visible` back to
			// true - and it also resolves what the crit visual actually is.
			// An earlier reading of the same evidence concluded it must be a
			// different object entirely, since a hidden parent cannot render
			// a child; the real explanation is that the parent stops being
			// hidden for those few frames.
			//
			// So the position is offset as well as the visibility being set.
			// The two do not conflict: ordinary hits stay suppressed because
			// the hide is still in force, and the crit flash - which
			// overrides it - happens wherever the object has been moved to.
			// Alexander's call that a brief hit marker alongside a crit is
			// fine is what makes this workable.
			if (settings.moveCritMarker) {
				changedAny |= TryMoveObject(root, container + ".HitIndicator_mc", settings.critMarkerOffsetX, settings.critMarkerOffsetY);
			} else {
				LogInspection(root, container + ".HitIndicator_mc");
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
