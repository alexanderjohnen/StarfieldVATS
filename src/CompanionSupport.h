#pragma once

namespace VATS
{
	// Support actions aimed at the player's own companions, as opposed to
	// everything else in this mod, which is aimed at killing things.
	//
	// The targeting half was free: Targeting::GetCrosshairTeammate is the
	// same engine-computed, occlusion-correct crosshair pick the combat path
	// already uses, keeping exactly the actors that one discards. So "who am
	// I pointing at, and is it a friend" was solved before this file existed.
	//
	// FIRST STEP ONLY (2026-08-28). This is healing and nothing else, and it
	// is deliberately the narrowest possible version of the idea:
	//
	//   * No menu. Building one means re-adding input capture to the ImGui
	//     overlay, which D3DHook.cpp stripped out on purpose ("a passive
	//     always-on HUD, not an interactive menu") and which has crash
	//     history here. One key, one action, no selection.
	//   * No item cost yet. The heal is free. Consuming an aid item is
	//     ObjectReference::RemoveItem in Papyrus, which needs the VM route
	//     that is currently blocked - see below.
	//   * No buffs, no Starborn powers. Both need
	//     Actor::DoCombatSpellApply, which exists only as a Papyrus native.
	//     Reaching it means IVirtualMachine::DispatchMethodCall, whose
	//     argument parameter is typed BSTThreadScrapFunction - and
	//     CommonLibSF defines that as a bare alias for std::function, which
	//     is a placeholder rather than a binding. Handing the engine a
	//     structure of the wrong shape is exactly the failure this project
	//     has already had three times, so that route waits until the real
	//     layout is known. See docs/FINDINGS.md.
	//
	// What healing instead rides on is the one engine-call mechanism this
	// project has already proven in production: the RTTI-verified
	// ActorValueOwner sub-object that HealthReader has been reading live
	// health through since 2026-08-25. Same object, same verification, one
	// vtable slot further along.
	class CompanionSupport
	{
	public:
		// Heals a_actor. MUST be called on the game thread - Advance() already
		// runs there, having been queued by RequestAdvance(). This project has
		// crashed once from touching engine state off-thread, and this is the
		// first engine call here that WRITES, so the rule is not negotiable.
		//
		// Logs health before and after, which is what makes a press a
		// measurement: if the two are equal, RestoreActorValue does not do
		// what its name says on this build, and that is worth learning
		// immediately rather than inferring from a health bar.
		static void HealActor(RE::Actor* a_actor);

		// Called from the keyboard-hook pump thread. Queues the real work onto
		// the game thread and returns at once - the engine call must never run
		// on an input thread.
		static void RequestAction();

		// TEMPORARY (2026-08-28). Walks the player inventory and logs every
		// entry: form ID, form type, stack count. Purely reads - no engine
		// call, no write.
		//
		// It exists to harvest facts we must not invent. The plan for aid
		// items is a fixed table (Alexander idea, and a good one: the base
		// game set is tiny and its form IDs never change), but nobody here
		// knows those IDs, and a guessed ID fails SILENTLY - the item is
		// simply never found, which looks like a broken feature rather than
		// a wrong number. Same for the ALCH form type constant: this project
		// only has ACHR=75 confirmed, so the type is logged rather than
		// filtered on.
		//
		// Runs once when a support session opens - bounded, deliberate, and
		// exactly when it is relevant. Delete once the table is filled in.
		static void LogPlayerInventory();
	};
}
