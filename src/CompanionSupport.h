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
		// Called from the hotkey thread. Queues the real work onto the game
		// thread and returns immediately - this project has crashed once
		// already from touching engine state off-thread, so the write never
		// happens on the caller's thread.
		static void RequestHeal();

	private:
		// Runs on the game thread only.
		static void HealCrosshairTeammate();
	};
}
