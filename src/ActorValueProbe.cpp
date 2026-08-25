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

		// TypeDescriptor: { void* vftable; void* spare; char name[]; }
		constexpr std::size_t kTypeDescriptorNameOffset = 0x10;

		// How far into an Actor to look for base-class vtable pointers.
		// Actor's declared base list already runs to 0x1D8, and the whole
		// object is far larger; 0x400 covers every base comfortably while
		// keeping the one-time scan trivial.
		constexpr std::size_t kVTableScanBytes = 0x400;

		// Reads the RTTI class name for a candidate vtable pointer. Every
		// step is SafeRead-guarded, so feeding this a value that merely
		// looks like a pointer (or does not point at a vtable at all)
		// fails cleanly instead of faulting.
		[[nodiscard]] bool ReadRttiName(const void* a_vtable, char* a_out, std::size_t a_outLen)
		{
			if (!a_vtable) {
				return false;
			}

			const void* locatorPtr = nullptr;
			if (!SafeRead(reinterpret_cast<const std::byte*>(a_vtable) - sizeof(void*), &locatorPtr, sizeof(locatorPtr)) || !locatorPtr) {
				return false;
			}

			RTTICompleteObjectLocator locator{};
			if (!SafeRead(locatorPtr, &locator, sizeof(locator))) {
				return false;
			}
			// signature==1 identifies the x64 form and rejects most garbage
			// that happened to be readable.
			if (locator.signature != 1 || locator.typeDescriptor == 0) {
				return false;
			}

			const auto moduleBase = REX::FModule::GetExecutingModule().GetBaseAddress();
			if (!moduleBase) {
				return false;
			}

			const auto* descriptor = reinterpret_cast<const std::byte*>(moduleBase + locator.typeDescriptor);
			std::memset(a_out, 0, a_outLen);
			if (!SafeRead(descriptor + kTypeDescriptorNameOffset, a_out, a_outLen - 1)) {
				return false;
			}
			a_out[a_outLen - 1] = '\0';
			return true;
		}

		// Walks the Actor object for a sub-object whose vtable's RTTI name
		// is ActorValueOwner, and returns its offset. Logs the full map of
		// every base class it finds on the way the first time it runs -
		// that map is exactly the information CommonLibSF's Actor.h is
		// missing, so it is worth having in the log regardless of whether
		// the target name turns up.
		[[nodiscard]] bool FindActorValueOwnerOffset(RE::Actor* a_actor, std::size_t& a_out)
		{
			static bool s_loggedMap = false;
			const bool  logMap = !s_loggedMap;
			s_loggedMap = true;

			bool        found = false;
			std::size_t foundAt = 0;

			for (std::size_t off = 0; off < kVTableScanBytes; off += sizeof(void*)) {
				const void* vtable = nullptr;
				if (!SafeRead(reinterpret_cast<const std::byte*>(a_actor) + off, &vtable, sizeof(vtable)) || !vtable) {
					continue;
				}

				char name[128]{};
				if (!ReadRttiName(vtable, name, sizeof(name))) {
					continue;
				}

				if (logMap) {
					REX::INFO("[VATS] actor rtti map: +0x{:03X} -> {}", off, name);
				}

				// MSVC decorates the name as ".?AVActorValueOwner@RE@@" (or
				// "@@" with no namespace), so match on the substring rather
				// than the whole decorated string.
				if (!found && std::strstr(name, "ActorValueOwner") != nullptr) {
					found = true;
					foundAt = off;
					if (!logMap) {
						break;
					}
				}
			}

			if (found) {
				a_out = foundAt;
			}
			return found;
		}
	}

	bool TryGetLiveHealth(RE::Actor* a_actor, float& a_out)
	{
		if (!a_actor) {
			return false;
		}

		auto* avList = RE::ActorValue::GetSingleton();
		if (!avList || !avList->health) {
			return false;
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
					REX::WARN("[VATS] live health: no ActorValueOwner sub-object found in Actor - not calling anything");
				}
				return false;
			}
			s_haveOffset = true;
			REX::INFO("[VATS] live health: ActorValueOwner sub-object confirmed at Actor+0x{:03X}", s_ownerOffset);
		}

		// Re-confirm identity on every call before dispatching through the
		// vtable. Two guarded reads and a substring compare is nothing next
		// to a per-frame overlay, and it means a stale/garbage object can
		// never reach the virtual call - the call only ever happens on a
		// sub-object that identifies itself as ActorValueOwner right now.
		auto*       ownerBytes = reinterpret_cast<std::byte*>(a_actor) + s_ownerOffset;
		const void* vtable = nullptr;
		if (!SafeRead(ownerBytes, &vtable, sizeof(vtable)) || !vtable) {
			return false;
		}
		char name[128]{};
		if (!ReadRttiName(vtable, name, sizeof(name)) || std::strstr(name, "ActorValueOwner") == nullptr) {
			return false;
		}

		auto* owner = reinterpret_cast<RE::ActorValueOwner*>(ownerBytes);
		a_out = owner->GetActorValue(*avList->health);
		return true;
	}
}
