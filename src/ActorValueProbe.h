#pragma once

namespace VATS
{
	// Live current health via the engine's own accessor, 2026-08-25.
	//
	// WHY THIS EXISTS: every data-only attempt to find an actor's live
	// current health failed with hard evidence, not just "didn't find it"
	// - avStorage.baseValues reads once at full HP and then never changes
	// across a whole fight (so it is MAX, not current), avStorage.modifiers
	// has no health entry at all, and two rounds of blind raw-memory diff
	// scanning over the Actor object turned up only countdown timers.
	//
	// WHAT CHANGED: Alexander's Techrunner mod displays a target's live
	// current health (screenshot-confirmed dropping 345 -> 198.53 as the
	// target was damaged, fractional, unmistakably the real value) and
	// **Techrunner ships no SFSE plugin at all** - it is an ESM with
	// Papyrus scripts. Papyrus' Actor.GetValue() therefore returns live
	// health, which means the engine-side accessor
	// ActorValueOwner::GetActorValue does too. That is the function to use;
	// there is no hidden data field to find.
	//
	// WHY THIS IS NOT THE USUAL RISK: this project's established rule is
	// "relocated data reads are safe, relocated FUNCTION calls are the
	// crash category" (two confirmed crashes from mapped-but-wrong REL::IDs,
	// see the commonlibsf-unmapped-ids notes). GetActorValue is not a
	// REL::ID call at all - it is a plain virtual call through the object's
	// own vtable, slot 01, with a signature declared in CommonLibSF's
	// ActorValueOwner.h. No Address Library lookup is involved, so the
	// entire failure mode that burned this project twice does not apply.
	//
	// THE ONE REAL UNKNOWN, and how it is removed before any call happens:
	// CommonLibSF's Actor.h does NOT list ActorValueOwner among Actor's
	// base classes, so the header cannot tell us where the ActorValueOwner
	// sub-object sits inside an Actor. Rather than guess an offset, the
	// probe walks the Actor object looking for a vtable pointer whose MSVC
	// RTTI type descriptor literally spells "ActorValueOwner", and only
	// calls through a sub-object whose identity was proven that way. Every
	// step of that identification is a SafeRead-guarded plain data read -
	// the same discipline used for every other offset in this project.
	// If the name is never found, this reports failure and nothing is
	// called.
	[[nodiscard]] bool TryGetLiveHealth(RE::Actor* a_actor, float& a_out);
}
