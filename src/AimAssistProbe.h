#pragma once

namespace VATS
{
	// Diagnostic-only (2026-08-22): probes the pointer chain from an
	// actor's equipped weapon down to what's hypothesized to be Starfield's
	// native BGSAimAssistModel (form type AAMD) - specifically its
	// bulletBendingConeAngle field, the game's own "bend the fired shot
	// toward whatever's aim-assisted" mechanism. Found via CommonLibSF/
	// CommonLibF4 cross-reference (BGSAimAssistModel is new in Starfield,
	// no FO4 precedent), never independently verified in-game - this is
	// the verification step, following this project's established rule of
	// never trusting a struct-offset guess without in-game confirmation
	// (see commonlibsf-unmapped-ids memory). Logs the whole chain and
	// changes nothing else. See starfield-vats-mod-design memory for the
	// full reasoning (why a native bullet-bending mechanism matters: most
	// Starfield weapons resolve via instant hitscan, so there's no real
	// in-flight Projectile object to redirect the way ProjectileTracker
	// does for slow ordnance).
	//
	// Cross-validated via a known-good sibling field: WeaponDataAim's
	// aimModel pointer (offset 0x28, an already-named/trusted field in
	// CommonLibSF) should resolve to a form of type AMDL (BGSAimModel,
	// 0x93) - if that check fails, the whole pointer chain up to that
	// point is wrong and the AAMD hypothesis can't even be tested yet.
	class AimAssistProbe
	{
	public:
		// Call with the player (or any actor) whose *currently equipped*
		// weapon should be probed. Safe to call from any thread - all
		// reads are SafeRead-guarded, no writes, no engine calls.
		static void ProbeEquippedWeapon(RE::Actor* a_actor);
	};
}
