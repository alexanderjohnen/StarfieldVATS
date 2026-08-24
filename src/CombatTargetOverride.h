#pragma once

namespace VATS
{
	// EXPERIMENTAL, 2026-08-24 (Alexander's idea): forces Starfield's own
	// native enemy health bar to show on the VATS-Locked target by writing
	// directly into the player character's RE::Actor::currentCombatTarget
	// field (GameOffsets::kCurrentCombatTarget, a plain TESPointerHandle -
	// see that header for the full theory). Writes a_target->GetFormID()
	// directly as the handle value - an untested assumption that
	// TESPointerHandle == FormID for persistent-form references on this
	// game version (a common pattern in FO4-generation Bethesda engines).
	// Deliberately never resolves the handle via
	// BSPointerHandleManagerInterface::GetSmartPointer - a CONFIRMED CRASH
	// in this project (commonlibsf-unmapped-ids memory) - this only ever
	// writes/restores a plain uint32, never reads it back through any
	// handle-resolution call.
	//
	// Two earlier rounds both failed the same way (see git history for the
	// blow-by-blow): a single write at Lock survived barely a second before
	// the engine's own update stomped it back; re-writing once per render
	// frame (Overlay::Draw()) didn't fix it either - a same-frequency fight
	// against a field the engine recomputes at least once per frame just
	// ties/loses, it doesn't win, and the diagnostic logging that came with
	// testing it was suspected of contributing to a real gameplay
	// regression (far fewer shots getting redirected).
	//
	// Round 3 (this version): instead of racing the engine once per render
	// frame, a dedicated background thread re-writes the value continuously
	// at a tighter interval (5ms - not tied to render/frame rate at all,
	// deliberately not pushed to the extreme of ~1ms, see below) for as
	// long as a lock is active - maximizing the fraction of time our value
	// holds, on the theory that whatever native system samples this field
	// for the HUD (BSUIDataManager's "HudEnemyData" push) only needs to
	// catch it once in a while, not have it held permanently. No per-write
	// logging this time (Engage/Disengage still log once each) - the
	// log-spam risk from Round 2 doesn't apply here.
	//
	// Two residual risks Alexander raised, deliberately NOT dismissed:
	// (1) CPU cost of a thread writing continuously for the whole Locked
	// duration, not just during a shot like every other poll loop in this
	// project - a single 4-byte SafeWrite is cheap, but this runs the
	// entire time regardless. (2) currentCombatTarget is a general "combat
	// target" field per its name, not something documented as HUD-only -
	// forcing it for a much larger fraction of time than the previous
	// rounds increases exposure to any *other* native system that might
	// read it (companion AI, combat logic, anything not yet identified).
	// 5ms was chosen over a more extreme interval specifically to limit
	// both of these without abandoning the experiment - watch for any
	// unexpected behavior beyond the health bar itself when testing, not
	// just whether the bar shows.
	//
	// Same crash risk class as before regardless of interval: a plain
	// SafeWrite of a 4-byte-aligned uint32, unsynchronized against the
	// engine's own writes by design - a torn/racing write degrades to "one
	// write briefly overwritten," never a crash, same reasoning
	// ProjectileTracker.cpp's per-frame velocity writes already rely on.
	class CombatTargetOverride
	{
	public:
		// Call once when a lock starts (Controller::Advance()'s lock
		// branch). Reads and remembers the player's real
		// currentCombatTarget (so Disengage can restore it), then starts
		// the background thread that holds a_target's formID in place.
		// Safe no-op if PlayerCharacter/a_target is unavailable, the
		// initial read fails, or already engaged.
		static void Engage(RE::Actor* a_target);

		// Call once when a lock ends (Controller::ForceOff()/Advance()'s
		// unlock branch). Stops the background thread and restores
		// whatever value Engage() saw before overwriting it. Safe no-op if
		// never engaged.
		static void Disengage();

	private:
		static void ThreadProc(const std::stop_token& a_stop, std::uint32_t a_formID);

		static inline std::jthread m_thread;
	};
}
