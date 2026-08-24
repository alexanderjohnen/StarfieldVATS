#pragma once

namespace VATS
{
	// Hides Starfield's native hit marker, kill marker, and crit banner
	// while VATS is Locked, restoring them on unlock - our own hit/miss
	// flash (Overlay.cpp) already gives that feedback. Floating damage
	// numbers are deliberately NOT attempted here anymore (see below) -
	// use DamageNumbersVisibility.cpp for those instead.
	//
	// History, 2026-08-23/24 (see HANDOFF.md/git log for the full blow-by-
	// blow): originally believed all five elements (DamageNumberText_mc,
	// CritText_mc, CritBanner_mc, HitIndicator_mc, KillIndicator_mc) lived
	// as siblings inside one dynamically-spawned "HitDamageIndicatorClip"
	// container, and that a one-shot Hide() at Lock time found nothing
	// because the container didn't exist yet (clips spawn per-hit). Fixed
	// by polling every frame instead - but that introduced a *different*
	// problem: touching the live HUDMenu AS3 VM ~60x/sec from the D3D
	// Present thread, for the whole Locked duration, is suspected (not
	// proven) to have caused a later crash report showing an unrelated
	// engine job thread faulting inside Scaleform's own VM. Disabled
	// again the same session.
	//
	// A real JPEXS decompile of hudmenu.gfx (docs/hudmenu-decompiled/,
	// 2026-08-24) then settled the actual structure: `HitDamageIndicator.
	// as`'s `SpawnNewClip()` really does addChild() a fresh, anonymously-
	// named `HitDamageIndicatorClip` per hit (containing
	// `DamageNumberText_mc`) - genuinely, permanently unreachable by any
	// static path, full stop, not worth ever probing again. But
	// `HitKillIndicator.as` (`HitIndicator_mc`, `KillIndicator_mc`,
	// `CritBanner_mc` nested under `HitIndicator_mc`) is a DIFFERENT,
	// persistent, timeline-placed object - constructed once, subscribes to
	// `BSUIDataManager` once, never destroyed - not dynamically spawned at
	// all. The original "never found" failure for THESE three was
	// therefore a wrong-*path* guess, not a timing problem - a one-shot
	// Hide()/Restore() (this file's current design, same safe pattern as
	// CrosshairVisibility/DamageNumbersVisibility - no per-frame Scaleform
	// touching, zero relation to the crash-suspected mechanism above)
	// should work fine IF the container's real instance name is guessed
	// correctly. Unconfirmed as of this rewrite - see kContainerNames/
	// kFallbackParents in the .cpp for the current guesses and the log for
	// which one (if any) resolved.
	//
	// Editing HONKCORE's hudmenu.gfx directly (JPEXS) was considered and
	// rejected 2026-08-24, Alexander's own call: it would hide the marker
	// permanently (not just while VATS is Locked), and the edit would need
	// to be redone on every HONKCORE update. This runtime toggle avoids
	// both problems.
	class CombatHudVisibility
	{
	public:
		// Call once when a lock starts. Searches for the candidate
		// container paths (cached after the first call), hides whatever
		// resolves. Safe no-op if nothing resolves.
		static void HideActive();

		// Call once when a lock ends. Restores exactly what the matching
		// HideActive() actually hid - safe no-op otherwise.
		static void Restore();
	};
}
