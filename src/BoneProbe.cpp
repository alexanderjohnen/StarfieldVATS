#include "BoneProbe.h"

#include "GameOffsets.h"
#include "SafeMem.h"
#include "Settings.h"
#include "WorldBoundProbe.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace VATS
{
	namespace
	{
		template <class T>
		[[nodiscard]] bool Read(const void* a_base, std::size_t a_off, T& a_out)
		{
			return SafeRead(static_cast<const std::byte*>(a_base) + a_off, &a_out, sizeof(T));
		}

		// NiObjectNET::name (BSFixedString, confirmed via CommonLibSF's own
		// static_assert(offsetof(NiObjectNET, name) == 0x10)) and
		// NiAVObject::world/NiTransform::translate (both confirmed by
		// static_assert on the enclosing structs' total sizes, which only
		// arithmetically fit if these field offsets are also right) - all
		// high-confidence, unlike the offset below.
		constexpr std::size_t kNiObjectName = offsetof(RE::NiObjectNET, name);
		constexpr std::size_t kNiAVObjectWorld = offsetof(RE::NiAVObject, world);
		constexpr std::size_t kNiTransformTranslate = offsetof(RE::NiTransform, translate);

		// UNVERIFIED, unlike the three above - CommonLibSF's own
		// NiNode.h has its static_assert on sizeof(NiNode) commented out,
		// meaning literally nobody has confirmed this layout for this
		// game version. Best estimate: right after NiAVObject's own
		// (confirmed) 0x130 bytes, no padding expected for a pointer-sized
		// member. Every use below sanity-checks the resulting {size,
		// capacity, data} triple before trusting it as a real array,
		// rather than assuming a non-faulting read means the offset was
		// right - same discipline as every other guessed offset in this
		// project (avStorage, cell references).
		// Starting guess only. sizeof(RE::NiAVObject) is 0x130 and the
		// header static_asserts it - but a header assert proves what the
		// header believes, not what the game's memory does, and this
		// project has already been bitten by exactly that
		// (TESObjectCELL::references was off by 8).
		//
		// It was wrong here too, and silently: the walk was rejecting the
		// children array at this offset on every single actor, so it never
		// descended past the root node and the only bone it ever found was
		// 'HumanExportRoot' at the feet. That looked like "Starfield names
		// its bones differently", which is why it went unexamined for
		// days. Confirmed 2026-08-26 by the bonedump: "1 nodes visited".
		constexpr std::size_t kNiNodeChildrenGuess = sizeof(RE::NiAVObject);

		// Window searched when the guess does not hold, and the shape a
		// candidate must have: BSTArray is {size@00, capacity@04,
		// data@08} (BSTArrayBase is size/capacity, the allocator is empty,
		// sizeof(BSTArray<void*>) == 0x10 - all header-asserted and
		// consistent with what the old code read, so the triple layout was
		// never the problem, only where it sits).
		constexpr std::size_t kChildrenSearchBegin = 0x0F0;
		constexpr std::size_t kChildrenSearchEnd = 0x220;

		// BSStringPool::Entry (confirmed sizeof == 0x18): _left@00,
		// {_length|_right} union@08, _refCount@10, _flags@14. String bytes
		// for a leaf entry (external() == false) sit immediately after the
		// struct, i.e. at entry+0x18 - see BSStringPool.h's Entry::data().
		constexpr std::size_t  kEntryLengthOrRight = 0x08;
		constexpr std::size_t  kEntryFlags = 0x14;
		constexpr std::size_t  kEntryStringData = 0x18;
		constexpr std::uint8_t kEntryExternalFlag = 1u << 1;

		constexpr std::size_t kMaxNameLen = 48;
		constexpr int         kMaxNodesVisited = 400;  // sanity cap, same spirit as the cell-reference scan cap
		constexpr int         kMaxDepth = 10;

		// Loose, case-insensitive substring match against a short list of
		// plausible "center" joint names - deliberately not exact
		// (Starfield's real naming convention for these is unconfirmed,
		// may or may not follow Skyrim/FO4's "NPC COM [COM ]" bracketed
		// style). A false-positive match just adds one extra log line;
		// it's not wired into anything that could be harmed by it.
		constexpr std::array<const char*, 7> kCandidates{
			"COM", "Spine", "Chest", "Torso", "Pelvis", "Hips", "Root"
		};

		[[nodiscard]] bool CaseInsensitiveContains(const char* a_haystack, const char* a_needle)
		{
			const std::size_t needleLen = std::strlen(a_needle);
			if (needleLen == 0) {
				return true;
			}
			for (const char* p = a_haystack; *p; ++p) {
				if (_strnicmp(p, a_needle, needleLen) == 0) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool ContainsCandidate(const char* a_name)
		{
			for (const char* c : kCandidates) {
				if (CaseInsensitiveContains(a_name, c)) {
					return true;
				}
			}
			return false;
		}

		// Reads the {size, capacity, data} triple at a_off and decides
		// whether it is really a node's children array.
		//
		// The decisive test is not the shape of the triple - garbage
		// passes a range check often enough - but a BACK-REFERENCE: every
		// child's parent pointer (NiAVObject::parent @ 0x038) must point
		// at the node we read the array from. A coincidence would have to
		// produce a plausible array header, a readable pointer at the
		// first slot, and that pointer's 0x38 field matching the node's
		// own address. That is strong enough to accept without an eye on
		// it, which is the point: this is meant to settle the offset, not
		// add a fourth thing to eyeball in-game.
		[[nodiscard]] bool ReadNodeName(const void* a_node, char (&a_out)[kMaxNameLen]);

		[[nodiscard]] bool TryReadChildren(std::uint64_t a_node, std::size_t a_off,
			std::uint32_t& a_outSize, std::uint64_t& a_outData)
		{
			std::uint32_t size = 0;
			std::uint32_t capacity = 0;
			std::uint64_t data = 0;
			if (!Read(reinterpret_cast<const void*>(a_node), a_off + 0x00, size) ||
				!Read(reinterpret_cast<const void*>(a_node), a_off + 0x04, capacity) ||
				!Read(reinterpret_cast<const void*>(a_node), a_off + 0x08, data)) {
				return false;
			}
			if (size == 0 || size > 200 || capacity < size || capacity > 200) {
				return false;
			}
			if (data < 0x10000 || (data & 0x7) != 0) {
				return false;
			}

			std::uint64_t firstChild = 0;
			if (!Read(reinterpret_cast<const void*>(data), 0, firstChild) ||
				firstChild < 0x10000 || (firstChild & 0x7) != 0) {
				return false;
			}

			// Validate by reading the first child's NAME, not by following
			// its parent pointer.
			//
			// The parent version (first child's NiAVObject::parent @ 0x038
			// pointing back here) found nothing anywhere in 0xF0..0x1C0 on
			// 2026-08-26, on a human and a mantid alike. But 0x038 is
			// itself only a header claim, so a correct candidate could have
			// been thrown away by a wrong second guess - and there was
			// evidence for exactly that: worldBound at 0x100 off this same
			// pointer reads sane radii on every actor, so the header layout
			// IS right that far, which argues the array should have been
			// where the header put it.
			//
			// A name is the better test because ReadNodeName is already
			// PROVEN on these objects - it is what produced
			// 'HumanExportRoot' and 'MantidA_mrRigRoot' - so a failure here
			// means the candidate is not a node, not that another guess was
			// wrong. Skeleton joints are always named; a random qword that
			// happens to look like an array header will not resolve to a
			// readable string through the BSStringPool chain.
			char name[kMaxNameLen];
			if (!ReadNodeName(reinterpret_cast<const void*>(firstChild), name) || name[0] == '\0') {
				return false;
			}

			a_outSize = size;
			a_outData = data;
			return true;
		}

		// Resolved once per session against a real actor's root node, then
		// reused. Zero means "not resolved yet".
		std::atomic<std::size_t> g_childrenOffset{ 0 };

		// Latched after one full sweep comes up empty, so the failure is
		// reported once instead of once per call. It logged every second
		// for six minutes on the run that found it (2026-08-26) - a
		// diagnostic loud enough to bury the rest of the log is its own
		// problem, and this project has already seen log volume affect
		// timing.
		std::atomic<bool> g_childrenProbeFailed{ false };

		[[nodiscard]] std::size_t ResolveChildrenOffset(std::uint64_t a_root)
		{
			if (const std::size_t known = g_childrenOffset.load(std::memory_order_relaxed); known != 0) {
				return known;
			}
			if (g_childrenProbeFailed.load(std::memory_order_relaxed)) {
				return 0;
			}

			std::uint32_t size = 0;
			std::uint64_t data = 0;
			if (TryReadChildren(a_root, kNiNodeChildrenGuess, size, data)) {
				g_childrenOffset.store(kNiNodeChildrenGuess, std::memory_order_relaxed);
				REX::INFO("[VATS] bone: children offset confirmed at the header's 0x{:X} ({} children)",
					kNiNodeChildrenGuess, size);
				return kNiNodeChildrenGuess;
			}

			for (std::size_t off = kChildrenSearchBegin; off < kChildrenSearchEnd; off += 0x08) {
				if (off == kNiNodeChildrenGuess) {
					continue;
				}
				if (TryReadChildren(a_root, off, size, data)) {
					// Logged rather than silently accepted: the first
					// candidate found is not necessarily the children
					// array, and the name of its first entry is what says
					// whether this is a skeleton or something else that
					// happens to be shaped like an array of named objects.
					char firstName[kMaxNameLen]{};
					std::uint64_t firstChild = 0;
					if (Read(reinterpret_cast<const void*>(data), 0, firstChild)) {
						ReadNodeName(reinterpret_cast<const void*>(firstChild), firstName);
					}
					REX::INFO("[VATS] bone: children candidate at 0x{:X} - {} entries, first named '''{}'''",
						off, size, firstName);
					g_childrenOffset.store(off, std::memory_order_relaxed);
					REX::INFO("[VATS] bone: children offset FOUND at 0x{:X} ({} children) - header said 0x{:X}",
						off, size, kNiNodeChildrenGuess);
					return off;
				}
			}

			g_childrenProbeFailed.store(true, std::memory_order_relaxed);
			REX::INFO("[VATS] bone: children offset not found in 0x{:X}..0x{:X} - tree walk stays at the root, bone route closed for this session",
				kChildrenSearchBegin, kChildrenSearchEnd);
			return 0;
		}

		// Follows BSStringPool::Entry::leaf() by hand (plain data, no
		// engine call) and copies the string bytes out. Bounded to a few
		// hops as a guard against a corrupted/cyclic external-entry chain.
		// Returns true (with an empty string) for a null/empty name, which
		// is a normal, common case - only returns false on an actual
		// unreadable/implausible chain.
		[[nodiscard]] bool ReadNodeName(const void* a_node, char (&a_out)[kMaxNameLen])
		{
			a_out[0] = '\0';

			std::uint64_t entry = 0;
			if (!Read(a_node, kNiObjectName, entry)) {
				return false;
			}
			if (!entry) {
				return true;  // no name, not an error
			}

			for (int hop = 0; hop < 4; ++hop) {
				std::uint8_t flags = 0;
				if (!Read(reinterpret_cast<const void*>(entry), kEntryFlags, flags)) {
					return false;
				}
				if ((flags & kEntryExternalFlag) == 0) {
					break;
				}
				std::uint64_t next = 0;
				if (!Read(reinterpret_cast<const void*>(entry), kEntryLengthOrRight, next) || !next) {
					return false;
				}
				entry = next;
			}

			std::uint32_t length = 0;
			if (!Read(reinterpret_cast<const void*>(entry), kEntryLengthOrRight, length)) {
				return false;
			}
			length = std::min<std::uint32_t>(length, kMaxNameLen - 1);
			if (length > 0 &&
				!SafeRead(reinterpret_cast<const std::byte*>(entry) + kEntryStringData, a_out, length)) {
				return false;
			}
			a_out[length] = '\0';
			return true;
		}

		[[nodiscard]] bool ReadWorldPos(const void* a_node, RE::NiPoint3& a_out)
		{
			return Read(a_node, kNiAVObjectWorld + kNiTransformTranslate, a_out);
		}
	}

	void BoneProbe::LogIfChanged(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}

		static std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> s_lastLogged;
		const std::uint32_t                                                             formID = a_actor->GetFormID();
		const auto                                                                      now = std::chrono::steady_clock::now();
		if (const auto it = s_lastLogged.find(formID); it != s_lastLogged.end() && now - it->second < std::chrono::seconds(1)) {
			return;
		}
		s_lastLogged[formID] = now;

		std::uint64_t loadedData = 0;
		if (!Read(a_actor, GameOffsets::kActorLoadedData, loadedData) || !loadedData) {
			return;
		}
		std::uint64_t root = 0;
		if (!Read(reinterpret_cast<const void*>(loadedData), GameOffsets::kLoadedRefData3D, root) || !root) {
			return;
		}

		RE::NiPoint3 feet{};
		if (!Read(a_actor, GameOffsets::kLocation, feet)) {
			return;
		}
		const RE::NiPoint3 worldBoundRef = WorldBoundProbe::GetAimPoint(a_actor, feet);

		struct WorkItem
		{
			std::uint64_t node;
			int           depth;
		};
		const std::size_t childrenOffset = ResolveChildrenOffset(root);

		std::vector<WorkItem> stack{ { root, 0 } };
		int                   visited = 0;
		int                   matches = 0;
		int                   named = 0;

		// One-shot per actor, and only while the aim diagnostics are on:
		// dump EVERY named node the walk reaches, with its depth, instead
		// of only the ones matching a guessed candidate list.
		//
		// The candidate list has now run for days and has only ever
		// matched 'HumanExportRoot' - which sits at the feet
		// (aboveFeet=0.00 in every sample) and is therefore useless as an
		// aim point. Two explanations fit that equally well and the log
		// cannot tell them apart: either the walk never descends past the
		// root (the children offset is a documented guess, see
		// kNiNodeChildren), or it descends fine and Starfield simply does
		// not name its bones "COM"/"Spine"/"Chest". Dumping names with
		// depths separates those in one run: depth 0 only means the walk
		// is stuck, deeper names mean the naming assumption was wrong.
		//
		// This matters because a real chest bone would end the aim-point
		// problem outright rather than improve it - it is pose-correct and
		// creature-correct by construction, where every bounding-sphere
		// model so far has had to trade the two off against each other.
		static std::unordered_set<std::uint32_t> s_dumped;
		const bool dumpNames = Settings::Get().debugAimMarkers && !s_dumped.contains(formID);

		while (!stack.empty() && visited < kMaxNodesVisited) {
			const WorkItem item = stack.back();
			stack.pop_back();
			++visited;

			char       name[kMaxNameLen];
			const bool haveName = ReadNodeName(reinterpret_cast<const void*>(item.node), name) && name[0] != '\0';

			if (dumpNames && haveName) {
				++named;
				RE::NiPoint3 pos{};
				const bool   havePos = ReadWorldPos(reinterpret_cast<const void*>(item.node), pos);
				REX::INFO("[VATS] bonedump: formID=0x{:08X} depth={} name='{}' aboveFeet={:.3f}",
					formID, item.depth, name, havePos ? pos.z - feet.z : -99.0f);
			}

			if (haveName && ContainsCandidate(name)) {
				RE::NiPoint3 pos{};
				if (ReadWorldPos(reinterpret_cast<const void*>(item.node), pos)) {
					++matches;
					REX::INFO("[VATS] bone: formID=0x{:08X} name='{}' pos=({:.2f},{:.2f},{:.2f}) aboveFeet={:.2f} vsWorldBound=({:.2f},{:.2f},{:.2f})",
						formID, name, pos.x, pos.y, pos.z, pos.z - feet.z,
						pos.x - worldBoundRef.x, pos.y - worldBoundRef.y, pos.z - worldBoundRef.z);
				}
			}

			if (item.depth >= kMaxDepth) {
				continue;
			}

			// Attempt children (NiNode::children, see kNiNodeChildren's
			// comment for why this offset is a guess) - sanity-checked
			// before trusting it as a real array, since item.node isn't
			// necessarily a NiNode at all (leaf mesh objects have no
			// children, and there's no cheap RTTI check available here).
			std::uint32_t size = 0;
			std::uint64_t data = 0;
			if (childrenOffset == 0 || !TryReadChildren(item.node, childrenOffset, size, data)) {
				continue;  // a leaf, or not a node with children - either way, nothing below it
			}
			for (std::uint32_t i = 0; i < size && visited + static_cast<int>(stack.size()) < kMaxNodesVisited; ++i) {
				std::uint64_t child = 0;
				if (Read(reinterpret_cast<const void*>(data), 8ull * i, child) && child) {
					stack.push_back({ child, item.depth + 1 });
				}
			}
		}

		if (dumpNames) {
			s_dumped.insert(formID);
			REX::INFO("[VATS] bonedump: formID=0x{:08X} done - {} nodes visited, {} named (walk cap {} nodes / depth {})",
				formID, visited, named, kMaxNodesVisited, kMaxDepth);
		}

		if (matches == 0) {

			REX::INFO("[VATS] bone: formID=0x{:08X} no candidate name matched ({} nodes visited)", formID, visited);
		}
	}
}
