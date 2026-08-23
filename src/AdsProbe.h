#pragma once

namespace VATS
{
	// Diagnostic only (2026-08-23) - no fix yet. AdsBlocker's OS-level
	// mouse-hook swallow of the ADS button fires and logs correctly, but
	// Alexander confirmed ADS still visibly engaged - the same class of
	// problem EngineInputLayer.h already documents for the Tab-key
	// interceptor (Starfield evidently reads some inputs through a path an
	// OS hook can't suppress). This probe looks for a real engine-level
	// signal of "is the player aiming" to force-reset instead, the same
	// general strategy AimAssistProbe already uses successfully for
	// aimAssistEnabled:
	//
	//  1. TESObjectREFR (an Actor base class) implements
	//     IAnimationGraphManagerHolder, whose GetGraphVariableImplBool is a
	//     real virtual function call requiring no REL::ID at all (pure
	//     vtable dispatch on a live object) - an unknown variable name is a
	//     safe, crash-free miss, same guarantee as Setting::GetSetting. This
	//     tries a list of plausible Havok Behavior graph variable names.
	//  2. ActorState::actorState1/actorState2 (RE/A/ActorState.h) - two
	//     packed bitfields, typed header members already trusted elsewhere
	//     in this project's inheritance chain (same offsetof-trust level as
	//     GameOffsets::kBoolBits). Logged raw so a manual bit-diff (aiming
	//     vs. not) can spot which bit(s), if any, track aim state, mirroring
	//     the raw-dword-diff technique ProjectileTracker.cpp used to find
	//     RE::Projectile's real offsets.
	//
	// Logs one snapshot immediately and another ~200ms later so a real test
	// (aim while Locked, or just aim normally to get a baseline) shows
	// whether anything actually changes between the two samples.
	void LogAimStateSnapshot(const char* a_tag);

	// Diagnostic (2026-08-23): neither the graph-variable candidates nor
	// actorState1/actorState2 changed between an "ADS button swallowed"
	// snapshot and one 200ms later, yet Alexander confirmed ADS still
	// visibly engaged - the real signal isn't in either place this probe
	// already checks. Dumps a wide raw dword window of the player Actor
	// object (0x0-0x400) so two dumps, one tagged "ads-down" (button
	// pressed, current WEAPON aim animation about to start) and one
	// "ads-up" (released), can be diffed by hand for whatever offset
	// actually flips - same technique ProjectileTracker.cpp used to find
	// RE::Projectile's real offsets. Called on every button transition
	// regardless of VATS lock state, so a clean *unlocked* baseline (aim
	// normally, nothing being swallowed) can be compared against a locked
	// attempt.
	void DumpPlayerRawRange(const char* a_tag);
}
