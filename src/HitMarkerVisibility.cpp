#include "HitMarkerVisibility.h"

namespace VATS
{
	namespace
	{
		// Best-guess AS variable paths for HitIndicator_mc's visibility,
		// tried in order - common Bethesda HUD root-path conventions
		// (Skyrim/FO4-era AS2 style, since "HitIndicator_mc"'s own naming
		// is classic AS2 MovieClip convention despite HONKCORE's AS3
		// widget framework sitting on top of it) tried first, plus a bare
		// relative path in case GetVariable/SetVariable on the root object
		// don't need a leading _root.
		constexpr const char* kCandidatePaths[] = {
			"_root.HitIndicator_mc._visible",
			"_root.HUDMovieBaseInstance.HitIndicator_mc._visible",
			"HitIndicator_mc._visible",
			"_root.HUDMovieBaseInstance.HitIndicator_mc_39._visible",
		};

		const char* s_foundPath = nullptr;
		bool        s_originalValue = true;
		bool        s_searched = false;

		// Re-resolved on every call rather than cached across Hide()/
		// Restore() - RE::Scaleform::Ptr<ASMovieRootBase> can't be held in
		// a static (its Release() isn't exposed the way Ptr<T>::TryDetach
		// needs), and re-fetching via UI::GetMenuMovie is a plain menuMap
		// lookup anyway, cheap enough to just do twice.
		[[nodiscard]] RE::Scaleform::GFx::ASMovieRootBase* GetHudMovieRoot()
		{
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMenuMovie("HUDMenu") : nullptr;
			return movie ? movie->asMovieRoot.get() : nullptr;
		}

		void FindPath()
		{
			s_searched = true;
			auto* root = GetHudMovieRoot();
			if (!root) {
				REX::WARN("[VATS] hitmarker: HUDMenu movie unavailable");
				return;
			}
			for (const char* path : kCandidatePaths) {
				if (root->IsAvailable(path)) {
					s_foundPath = path;
					REX::INFO("[VATS] hitmarker: found variable '{}'", path);
					return;
				}
			}
			REX::WARN("[VATS] hitmarker: none of the candidate paths resolved, hide/restore is a no-op - check the AS paths in HitMarkerVisibility.cpp's kCandidatePaths");
		}
	}

	void HitMarkerVisibility::Hide()
	{
		if (!s_searched) {
			FindPath();
		}
		if (!s_foundPath) {
			return;
		}
		auto* root = GetHudMovieRoot();
		if (!root) {
			return;
		}
		RE::Scaleform::GFx::Value original;
		if (root->GetVariable(&original, s_foundPath)) {
			s_originalValue = original.GetBoolean();
		}
		const RE::Scaleform::GFx::Value falseVal(false);
		root->SetVariable(s_foundPath, falseVal);
		REX::INFO("[VATS] hitmarker: hidden (was {})", s_originalValue);
	}

	void HitMarkerVisibility::Restore()
	{
		if (!s_foundPath) {
			return;
		}
		auto* root = GetHudMovieRoot();
		if (!root) {
			return;
		}
		const RE::Scaleform::GFx::Value val(s_originalValue);
		root->SetVariable(s_foundPath, val);
		REX::INFO("[VATS] hitmarker: restored to {}", s_originalValue);
	}
}
