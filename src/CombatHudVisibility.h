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
	// Hides every (prefix, clip) combination that resolves, not just the
	// first hit, since these clips may not all share one directly-
	// addressable container even if they're conceptually grouped. A wrong
	// guess for any one is a safe no-op (IsAvailable/GetVariable simply
	// return false).
	class CombatHudVisibility
	{
	public:
		static void Hide();
		static void Restore();
	};
}
