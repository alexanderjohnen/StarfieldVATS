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

		// Call once when a lock ends (Controller::ForceOff()/Advance()'s
		// unlock branch). Restores whatever value Engage() saw before
		// overwriting it. Safe no-op if never engaged.
		static void Disengage();
	};
}
