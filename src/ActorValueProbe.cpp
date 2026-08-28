#include "ActorValueProbe.h"

#include "SafeMem.h"

#include "RE/A/ActorValueOwner.h"

#include <cstring>

namespace VATS
{
	namespace
	{
		// MSVC x64 RTTI, as emitted for every polymorphic class. Laid out
		// here rather than pulled from a library because it is read purely
		// as data: given a vtable pointer V, the complete object locator
		// sits at V[-1], and its typeDescriptor/classDescriptor fields are
		// image-relative 32-bit offsets from the module base (the x64
		// form - on x86 they would be raw pointers).
		struct RTTICompleteObjectLocator
		{
			std::uint32_t signature;         // 00 - 1 on x64
			std::uint32_t offset;            // 04 - this sub-object's offset within the complete object
			std::uint32_t cdOffset;          // 08
			std::uint32_t typeDescriptor;    // 0C - image-relative
			std::uint32_t classDescriptor;   // 10 - image-relative
			std::uint32_t self;              // 14 - image-relative, points back at this locator
		};

		// Describes the whole inheritance graph of the complete class. Its
		// base class array is what actually names each base and, crucially,
		// gives that base's offset within the complete object.
		struct RTTIClassHierarchyDescriptor
		{
			std::uint32_t signature;       // 00
			std::uint32_t attributes;      // 04
			std::uint32_t numBaseClasses;  // 08
			std::uint32_t baseClassArray;  // 0C - image-relative, array of image-relative BCD pointers
		};

		struct RTTIBaseClassDescriptor
		{
			std::uint32_t typeDescriptor;     // 00 - image-relative
			std::uint32_t numContainedBases;  // 04
			std::int32_t  mdisp;              // 08 - THIS base's offset within the complete object
			std::int32_t  pdisp;              // 0C
			std::int32_t  vdisp;              // 10
			std::uint32_t attributes;         // 14
			std::uint32_t classDescriptor;    // 18
		};

		// TypeDescriptor: { void* vftable; void* spare; char name[]; }
		constexpr std::size_t kTypeDescriptorNameOffset = 0x10;

		// Sanity bound on the declared base count, so a bad read can't turn
		// the walk below into a runaway loop.
		constexpr std::uint32_t kMaxBaseClasses = 256;

		// Reads a type descriptor's decorated class name, given its
		// image-relative address. Guarded throughout, so garbage fails
		// cleanly instead of faulting.
		[[nodiscard]] bool ReadTypeName(std::uintptr_t a_moduleBase, std::uint32_t a_typeDescriptorRva, char* a_out, std::size_t a_outLen)
		{
			if (!a_moduleBase || a_typeDescriptorRva == 0) {
				return false;
			}
			const auto* descriptor = reinterpret_cast<const std::byte*>(a_moduleBase + a_typeDescriptorRva);
			std::memset(a_out, 0, a_outLen);
			if (!SafeRead(descriptor + kTypeDescriptorNameOffset, a_out, a_outLen - 1)) {
				return false;
			}
			a_out[a_outLen - 1] = '\0';
			return true;
		}

		// Resolves the complete object locator for an object's primary
		// vtable.
		[[nodiscard]] bool ReadLocator(const void* a_object, RTTICompleteObjectLocator& a_out)
		{
			const void* vtable = nullptr;
			if (!SafeRead(a_object, &vtable, sizeof(vtable)) || !vtable) {
				return false;
			}
			const void* locatorPtr = nullptr;
			if (!SafeRead(reinterpret_cast<const std::byte*>(vtable) - sizeof(void*), &locatorPtr, sizeof(locatorPtr)) || !locatorPtr) {
				return false;
			}
			if (!SafeRead(locatorPtr, &a_out, sizeof(a_out))) {
				return false;
			}
			// signature==1 identifies the x64 form and rejects most garbage
			// that happened to be readable.
			return a_out.signature == 1 && a_out.classDescriptor != 0;
		}

		// Finds the ActorValueOwner base's offset within Actor by walking
		// Actor's RTTI class hierarchy.
		//
		// An earlier version scanned the object for base-class vtable
		// pointers and read each one's RTTI name - that found all 25
		// vtables but every single one reported ".?AVActor@@", because a
		// complete object locator names the COMPLETE class, not the base
		// whose sub-object that particular vtable serves. The base names
		// (and, more usefully, each base's offset) live one level further
		// in, in the class hierarchy descriptor's base class array, where
		// every entry carries both a type descriptor and an mdisp. So this
		// reads the answer directly instead of inferring it from where a
		// pointer happened to sit.
		//
		// Logs the entire base list with offsets the first time it runs -
		// that is precisely the layout information CommonLibSF's Actor.h is
		// missing, and it is worth having in the log even if the lookup
		// below fails.
		[[nodiscard]] bool FindActorValueOwnerOffset(RE::Actor* a_actor, std::size_t& a_out)
		{
			static bool s_loggedMap = false;
			const bool  logMap = !s_loggedMap;
			s_loggedMap = true;

			const auto moduleBase = REX::FModule::GetExecutingModule().GetBaseAddress();
			if (!moduleBase) {
				return false;
			}

			RTTICompleteObjectLocator locator{};
			if (!ReadLocator(a_actor, locator)) {
				VATS_WARN("[VATS] actor rtti: could not resolve the complete object locator");
				return false;
			}

			RTTIClassHierarchyDescriptor hierarchy{};
			if (!SafeRead(reinterpret_cast<const void*>(moduleBase + locator.classDescriptor), &hierarchy, sizeof(hierarchy)) ||
				hierarchy.numBaseClasses == 0 || hierarchy.numBaseClasses > kMaxBaseClasses || hierarchy.baseClassArray == 0) {
				VATS_WARN("[VATS] actor rtti: class hierarchy descriptor unreadable or implausible (numBases={})", hierarchy.numBaseClasses);
				return false;
			}

			bool        found = false;
			std::size_t foundAt = 0;

			const auto* array = reinterpret_cast<const std::byte*>(moduleBase + hierarchy.baseClassArray);
			for (std::uint32_t i = 0; i < hierarchy.numBaseClasses; ++i) {
				std::uint32_t descriptorRva = 0;
				if (!SafeRead(array + static_cast<std::size_t>(i) * sizeof(std::uint32_t), &descriptorRva, sizeof(descriptorRva)) || descriptorRva == 0) {
					continue;
				}

				RTTIBaseClassDescriptor descriptor{};
				if (!SafeRead(reinterpret_cast<const void*>(moduleBase + descriptorRva), &descriptor, sizeof(descriptor))) {
					continue;
				}

				char name[160]{};
				if (!ReadTypeName(moduleBase, descriptor.typeDescriptor, name, sizeof(name))) {
					continue;
				}

				if (logMap) {
					VATS_TRACE("[VATS] actor base[{:02}] +0x{:03X} -> {}", i, descriptor.mdisp, name);
				}

				// The game's classes sit in the global namespace, so the
				// decorated form is ".?AVActorValueOwner@@" - but match on
				// the substring so a namespaced build would work too. Only
				// a base reachable by plain member displacement is usable;
				// a virtual base (pdisp >= 0) would need a different, much
				// more involved resolution path and is not handled.
				if (!found && descriptor.pdisp < 0 && descriptor.mdisp >= 0 &&
					std::strstr(name, "ActorValueOwner") != nullptr) {
					found = true;
					foundAt = static_cast<std::size_t>(descriptor.mdisp);
				}
			}

			if (found) {
				a_out = foundAt;
			}
			return found;
		}
	}

	// Resolves the RTTI-verified ActorValueOwner sub-object of an Actor, or
	// nullptr. Everything about the identification is described on
	// TryGetLiveHealth in the header; this is that logic, factored out so
	// the write path below cannot drift from the read path that has been
	// running in production since 2026-08-25.
	namespace
	{
		[[nodiscard]] RE::ActorValueOwner* ResolveVerifiedOwner(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return nullptr;
			}

			// Cached across calls, but deliberately re-verified below rather
			// than trusted blind: the offset is a property of the class layout,
			// not of any one actor, so it only has to be searched for once.
			static std::size_t s_ownerOffset = 0;
			static bool        s_haveOffset = false;
			if (!s_haveOffset) {
				if (!FindActorValueOwnerOffset(a_actor, s_ownerOffset)) {
					static bool s_loggedFailure = false;
					if (!s_loggedFailure) {
						s_loggedFailure = true;
						VATS_WARN("[VATS] actor values: no ActorValueOwner sub-object found in Actor - not calling anything");
					}
					return nullptr;
				}
				s_haveOffset = true;
				VATS_LOG("[VATS] actor values: ActorValueOwner sub-object confirmed at Actor+0x{:03X}", s_ownerOffset);
			}

			// Re-confirm identity on every call before dispatching through the
			// vtable. Two guarded reads and a substring compare is nothing next
			// to a per-frame overlay, and it means a stale/garbage object can
			// never reach the virtual call - the call only ever happens on a
			// sub-object that identifies itself as ActorValueOwner right now.
			auto* ownerBytes = reinterpret_cast<std::byte*>(a_actor) + s_ownerOffset;

			// The sub-object's own complete object locator records which offset
			// within the complete object that vtable serves. Requiring it to
			// match the offset RTTI told us ActorValueOwner lives at confirms,
			// on every single call, that we are about to dispatch through the
			// right sub-object of a genuinely polymorphic object - a freed or
			// recycled allocation will not satisfy it.
			RTTICompleteObjectLocator locator{};
			if (!ReadLocator(ownerBytes, locator) || locator.offset != s_ownerOffset) {
				return nullptr;
			}

			return reinterpret_cast<RE::ActorValueOwner*>(ownerBytes);
		}
	}

	bool TryGetLiveHealth(RE::Actor* a_actor, float& a_out)
	{
		auto* avList = RE::ActorValue::GetSingleton();
		if (!avList || !avList->health) {
			return false;
		}
		auto* owner = ResolveVerifiedOwner(a_actor);
		if (!owner) {
			return false;
		}
		a_out = owner->GetActorValue(*avList->health);  // vtable slot 01
		return true;
	}

	bool TryGetActorValue(RE::Actor* a_actor, const RE::ActorValueInfo& a_info, float& a_out)
	{
		auto* owner = ResolveVerifiedOwner(a_actor);
		if (!owner) {
			return false;
		}
		a_out = owner->GetActorValue(a_info);  // slot 01
		return true;
	}

	bool TryGetTemporaryModifier(RE::Actor* a_actor, const RE::ActorValueInfo& a_info, float& a_out)
	{
		auto* owner = ResolveVerifiedOwner(a_actor);
		if (!owner) {
			return false;
		}
		a_out = owner->GetModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, a_info);  // slot 08
		return true;
	}

	bool TryModTemporary(RE::Actor* a_actor, const RE::ActorValueInfo& a_info, float a_delta)
	{
		auto* owner = ResolveVerifiedOwner(a_actor);
		if (!owner) {
			return false;
		}
		// The FOUR-argument overload, deliberately, and this is not a style
		// choice - the three-argument one crashed the game outright
		// (EXCEPTION_ACCESS_VIOLATION at this line, 2026-08-29, crash log
		// named it with the actor and the ActorValueInfo both valid in
		// registers).
		//
		// CommonLibSF declares two overloads back to back: slot 06 takes an
		// extra TESObjectREFR*, slot 07 does not. Slots 01 and 09 on this
		// same object are proven correct - reading live health and healing
		// both work - so the entries exist, but nothing proves their ORDER.
		// If it is reversed, a three-argument call lands in the function
		// that wants four, and the callee reads an uninitialised register as
		// a pointer. That is exactly what an access violation here looks
		// like.
		//
		// Passing four arguments is safe under BOTH orderings, which is why
		// it is the fix rather than a coin flip: if slot 06 really is the
		// four-argument one, the call is correct; if the order is reversed
		// and it takes three, the extra register is simply ignored by the
		// x64 convention. No garbage is ever dereferenced either way.
		//
		// The ref is the actor itself - guaranteed valid, since its RTTI was
		// verified moments ago - rather than nullptr, which a callee that
		// does use the parameter might dereference.
		//
		// CONFIRMED IN GAME 2026-08-29, which is why this is a plain call
		// again: it was SEH-guarded while the reasoning above was only
		// reasoning. A run that applied the shield three times over a real
		// companion (0s -> 30s -> 57s -> 86s, item 10 -> 7, then expiry)
		// produced no fault and no warning of any kind. The guard was probe
		// armour for an unproven slot and had nothing left to catch.
		owner->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, a_info, a_delta, a_actor);  // slot 06
		return true;
	}

	bool TryRestoreHealth(RE::Actor* a_actor, float a_amount)
	{
		if (!(a_amount > 0.0f)) {
			return false;
		}
		auto* avList = RE::ActorValue::GetSingleton();
		if (!avList || !avList->health) {
			return false;
		}
		auto* owner = ResolveVerifiedOwner(a_actor);
		if (!owner) {
			return false;
		}
		owner->RestoreActorValue(*avList->health, a_amount);  // vtable slot 09
		return true;
	}
}
