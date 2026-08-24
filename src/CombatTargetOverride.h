#pragma once

namespace VATS
{
	// EXPERIMENTAL, 2026-08-24 (Alexander's idea): forces Starfield's own
	// native enemy health bar to show on the VATS-Locked target by writing
	// directly into the player character's RE::Actor::currentCombatTarget
	// field (GameOffsets::kCurrentCombatTarget, a plain TESPointerHandle -
	// see that header for the full theory). A read-only probe confirmed
	// this field changes live, multiple times per second, tracking
	// whatever's directly under the crosshair right now (0 when nothing
	// hostile is precisely aimed at, a stable formID-shaped value
	// otherwise) - strong behavioral evidence it's the right field, and
	// notably different from `commandTarget` (stayed 0 the whole time
	// during the same probe session, since that one's for
	// friendly/interactable targets, not hostiles). Whether writing to it
	// actually makes the native health bar appear and *stay* on our
	// target (even once the camera looks away, the whole point of VATS)
	// is what this change tests - not yet confirmed in-game.
	//
	// Writes a_target->GetFormID() directly as the handle value - an
	// untested assumption that TESPointerHandle == FormID for
	// persistent-form references on this game version (a common pattern
	// in FO4-generation Bethesda engines, and the probed values matched
	// this shape). Deliberately never resolves the handle via
	// BSPointerHandleManagerInterface::GetSmartPointer - a CONFIRMED CRASH
	// in this project (commonlibsf-unmapped-ids memory) - this only ever
	// writes/restores a plain uint32, never reads it back through any
	// handle-resolution call.
	//
	// 2026-08-24, two rounds of testing, both inconclusive-to-negative:
	// Round 1 - a single write at Lock time was confirmed via a (since
	// removed) read-only probe to survive barely over a second before the
	// native engine's own update stomped it back to whatever the real
	// crosshair reticle is doing. Alexander confirmed the native health
	// bar didn't stay either.
	// Round 2 - added Refresh(), a per-frame re-write meant to keep
	// winning that race (called from Overlay::Draw() while Locked). The
	// probe showed this DIDN'T fix it: the field flip-flopped between our
	// value and the engine's on essentially every single frame, never
	// stably holding ours - the engine evidently recomputes this field at
	// least once per frame, not occasionally, so a same-frequency fight
	// just ties/loses rather than winning. The native health bar still
	// didn't stay. Separately, the diagnostic logging that came with
	// testing this (removed along with Refresh()'s call site) was firing
	// on essentially every frame once the flip-flopping started, real log
	// spam this project doesn't normally accept - suspected contributor to
	// a real gameplay regression Alexander reported the same session (far
	// fewer shots getting redirected than in earlier builds).
	//
	// Refresh()'s call site is now DISABLED (Overlay.cpp) - the function
	// stays implemented in case a smarter invocation strategy turns up
	// (e.g. writing from whatever thread/timing the engine's own
	// recompute uses, if that can ever be determined), but per-frame
	// racing from the render thread specifically didn't work. Engage()/
	// Disengage() (one write at Lock, one restore at Unlock) are cheap and
	// harmless, so they stay wired into VATSController.cpp even though
	// their effect doesn't persist beyond about a second on its own.
	class CombatTargetOverride
	{
	public:
		// Call once when a lock starts (Controller::Advance()'s lock
		// branch). Reads and remembers the player's real
		// currentCombatTarget first (so Disengage can restore it), then
		// overwrites it with a_target's formID. Safe no-op if
		// PlayerCharacter/a_target is unavailable or the read/write fails
		// (SafeRead/SafeWrite-guarded throughout).
		static void Engage(RE::Actor* a_target);

		// Call every frame while Locked (Overlay::Draw()), after Engage()
		// - re-writes a_target's formID unconditionally, no read/compare,
		// to keep winning the race against the engine's own per-frame
		// overwrite. No-op if never engaged (Engage() failed or wasn't
		// called this lock).
		static void Refresh(RE::Actor* a_target);

		// Call once when a lock ends (Controller::ForceOff()/Advance()'s
		// unlock branch). Restores whatever value Engage() saw before
		// overwriting it. Safe no-op if never engaged.
		static void Disengage();
	};
}
