#pragma once

namespace VATS
{
	// Starfield's own native "bend the fired shot toward whatever's
	// aim-assisted" mechanism (RE::AimAssistData::bulletBendingConeAngle,
	// BGSAimAssistModel, form type AAMD) - the same class of system used
	// by classic Bethesda VATS to curve shots without moving the camera
	// (see starfield-vats-mod-design memory, "Crash on locking" section's
	// sibling design notes and the FO4 real-time-VATS research). Needed
	// since standard Starfield weapons resolve via instant hitscan - no
	// real in-flight Projectile object exists to redirect the way
	// ProjectileTracker does for slow ordnance (rockets/grenades).
	//
	// Reached from an actor's equipped weapon via a pointer chain fully
	// confirmed in-game 2026-08-22 (every offset either carries a
	// CommonLibSF static_assert or was cross-validated live):
	//   Actor::inventoryList (0xA0, BSGuarded<BGSInventoryList*,
	//     BSReadWriteLock> - read raw, deliberately bypassing
	//     BSReadWriteLock::LockRead/UnlockRead since that's already on
	//     this project's list of mapped-but-crashed engine calls; see
	//     commonlibsf-unmapped-ids memory)
	//     -> BGSInventoryList::data (0x28, BSTArray<BGSInventoryItem>)
	//       -> first item that's formType kWEAP and IsEquipped()
	//         -> BGSInventoryItem::instanceData (live per-item instance,
	//            mods/attachments applied - not the static form template)
	//           -> WeaponDataAim* (TESObjectWEAPInstanceData + 0x18)
	//             -> BGSAimAssistModel* (WeaponDataAim + 0x38 - NOT +0x30,
	//                which is a BGSWwiseEventForm/audio cue; found by
	//                sweeping every pointer field in WeaponDataAim and
	//                checking formType against kAAMD/0x94)
	//               -> AimAssistData (embedded inline at +0x38 within the
	//                  form, per RE::BGSBaseFormT<T,...>::data)
	//                 -> bulletBendingConeAngle (+0x38 within that: 12.0
	//                    degrees, confirmed live, a real/sane value)
	//                 -> aimAssistEnabled (+0x5C: confirmed FALSE live -
	//                    likely gated to gamepad input, Alexander plays
	//                    M+KB; untested whether forcing it true alone is
	//                    sufficient to make hitscan resolution actually
	//                    consult bulletBendingConeAngle, or whether
	//                    there's a separate device-check gate elsewhere -
	//                    this is the open question ForceAimAssist tests)
	class AimAssistProbe
	{
	public:
		// Call with the player (or any actor) whose *currently equipped*
		// weapon should have aim-assist force-enabled, once per shot.
		// Writes aimAssistEnabled=true on the live BGSAimAssistModel
		// instance if found (a plain bool, not a pointer/handle - low
		// blast radius even if this hypothesis turns out incomplete).
		// Logs the whole chain either way. Safe to call from any thread -
		// all reads/writes are SafeRead/SafeWrite-guarded, no engine
		// calls.
		static void ForceAimAssist(RE::Actor* a_actor);
	};
}
