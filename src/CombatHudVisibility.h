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
	// find nothing. "Fixed" by calling HideActive() every frame while
	// Locked instead of once, each (prefix, clip) candidate re-checked
	// every frame so a clip spawning mid-combat gets caught within a frame
	// or two - mechanically sound, but see the DISABLED note below.
	//
	// DISABLED again, same day: the Overlay::Draw() call site that invoked
	// HideActive() every frame was commented out after a crash report from
	// the same test session showed an unrelated background engine job
	// thread ("BSJobs 10") faulting deep inside Scaleform's own AS3 VM,
	// with a *different* installed mod's DLL on that thread's call stack -
	// not StarfieldVATS.dll anywhere in it. Not proven to be caused by
	// this function, but going from one Scaleform touch per lock to ~60/sec
	// for the whole Locked duration is the one thing this project changed
	// this session that plausibly increases exposure to a genuine
	// thread-safety violation (unsynchronized concurrent access to the
	// live HUDMenu AS3 VM's internal state from the D3D Present thread,
	// racing the engine's own background script evaluation) - and this
	// project has already had two other confirmed hard failures from
	// Scaleform meddling in the same session (the now-removed
	// HealthWidgetReader.cpp - see git history/HANDOFF.md).
	// HideActive() itself is left implemented (harmless if never called)
	// in case a safer invocation strategy is found later - do not re-wire
	// it to run every frame from the render thread without first ruling
	// out the thread-safety theory above.
	class CombatHudVisibility
	{
	public:
		// Currently unused - see the DISABLED note above. Was meant to be
		// called every frame while Locked, not once at lock time.
		static void HideActive();
		static void Restore();
	};
}
