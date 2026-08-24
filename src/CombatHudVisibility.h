#pragma once

namespace VATS
{
	// Hides Starfield's native hit-feedback HUD elements (hit marker, kill
	// marker, floating damage numbers, crit text/banner) while VATS is
	// Locked - all confirmed to live together as sibling MovieClips inside
	// one "HitDamageIndicatorClip" container within HUDMenu's Scaleform
	// movie (found 2026-08-23 via a strings dump of hudmenu.gfx: the
	// native onHitKillDataChange callback pushes a HudHitKillIndicatorData
	// struct - fScreenX/fScreenY/fDamage/bShowDamageNumber/bInScopes/
	// uHitType/bIsMitigated - to this clip, which contains
	// DamageNumberText_mc, CritText_mc, CritBanner_mc, HitIndicator_mc, and
	// KillIndicator_mc as children). None of these are covered by
	// CrosshairVisibility's game-setting toggle - they're repopulated fresh
	// from native data on every hit, not gated by a persistent setting.
	// Supersedes the earlier standalone HitMarkerVisibility, whose
	// candidate paths were guessed before this fuller picture of the
	// display-list nesting was found.
	//
	// Hides each clip directly by name via ScaleformGFxASMovieRootBase::
	// SetVariable/GetVariable (same mechanism CrosshairVisibility uses),
	// tried under several candidate parent-path prefixes since the exact
	// nesting under HUDMenu's root timeline is unconfirmed - only symbol
	// names were found via the strings dump, not their actual placement.
	//
	// 2026-08-24: a single Hide() call at Lock time (the original design)
	// never found any of these paths available, because they weren't wrong
	// - they're right, but the clips genuinely don't exist yet at Lock
	// time. The strings dump also found "SpawnNewClip" sitting right next
	// to "onHitDamageIndicatorAnimationFinish", strongly suggesting these
	// are created fresh on each hit and destroyed once their animation
	// finishes - a one-shot check before any hit has happened will always
	// find nothing. Fixed by calling HideActive() every frame while Locked
	// (Overlay::Draw) instead of once - each (prefix, clip) candidate is
	// re-checked every frame, so a clip that spawns mid-combat gets caught
	// and hidden within a frame or two of appearing rather than never being
	// found at all. Only logs/writes on an actual true->false transition,
	// so steady-state cost while Locked is one GetVariable call per
	// candidate per frame, no SetVariable/log spam once everything's
	// already hidden.
	class CombatHudVisibility
	{
	public:
		// Call every frame while Locked (not once at lock time) - see above
		// for why. Cheap and idempotent; a safe no-op for candidates that
		// don't currently resolve to anything.
		static void HideActive();
		static void Restore();
	};
}
