#include "Targeting.h"

#include "GameOffsets.h"
#include "HealthReader.h"
#include "SafeMem.h"
#include "Settings.h"
#include "UI/CameraProject.h"

namespace VATS
{
	namespace
	{
		// The only reliable "is this actor dead" test this project has.
		// Actor::boolBits & BOOL_BITS::kDead - used everywhere here until
		// 2026-08-25 - is a confirmed no-op: the bit reads identically for
		// a living actor and for one lying dead on the floor. Health going
		// to zero (or negative, on overkill) is what actually distinguishes
		// them, and it only became readable once live health worked.
		[[nodiscard]] bool IsAlive(RE::Actor* a_actor)
		{
			HealthReading hp{};
			if (!GetActorHealth(a_actor, hp)) {
				// Unreadable health is not evidence of death - treat as
				// alive so a failed read can never silently make every
				// actor untargetable.
				return true;
			}
			return hp.current > 0.0f;
		}

		// Offsets/constants live in GameOffsets.h — empirically verified,
		// see the long comment in Targeting.h for how each one was proven.
		constexpr auto kCellReferencesOffset = GameOffsets::kCellReferences;
		constexpr auto kFormTypeOff = GameOffsets::kFormType;
		constexpr auto kLocationOff = GameOffsets::kLocation;
		constexpr auto kBoolBitsOff = GameOffsets::kBoolBits;
		constexpr auto kFormTypeACHR = GameOffsets::kFormTypeACHR;
		constexpr auto kDeadBit = GameOffsets::kActorDeadBit;

		template <class T>
		[[nodiscard]] bool Read(const void* a_base, std::size_t a_off, T& a_out)
		{
			return SafeRead(static_cast<const std::byte*>(a_base) + a_off, &a_out, sizeof(T));
		}

		// Should this actor be targetable at all?
		//
		// There is no safe ready-made hostility test here. CommonLibSF's
		// Actor::IsHostileToActor is declared against an Address Library ID
		// of literally 0 - unmapped, the exact shape that has hard-crashed
		// this project twice (GetActorKnowledge, HasDetectionLOS) - and the
		// virtual IsInCombat sits at a reverse-engineered vtable slot deep
		// enough that calling the wrong one would be worse than reading the
		// wrong field. So this is built on measurement instead.
		//
		// The probe that produced it (2026-08-25) logged a companion and an
		// enemy side by side:
		//   companion  combatTarget=0x00000000  boolBits=0x162021A2
		//   enemy      combatTarget=0x00000001  boolBits=0x122021A2
		//
		// The two boolBits differ in exactly one bit, 0x04000000 - bit 26,
		// which the header calls kPlayerTeammate, set on the companion and
		// clear on the enemy. Unlike the dead bit (nominally 1<<11, proven
		// inert in this game), this one is corroborated by measurement
		// rather than taken on the header's word: the single differing bit
		// between a teammate and a non-teammate landing exactly on the bit
		// named "teammate" is not a coincidence worth doubting.
		//
		// combatTarget is the second signal, and it reads 1 for the enemy
		// against a player formID of 0x14 - so it holds a handle, not a
		// form ID, and 1 is the player's. That matches this project's own
		// projectile logs, where player-fired rounds carry shooterHandle=1.
		// It is a strictly stronger filter (only actors actively fighting
		// the player) but it would also exclude an enemy who has not
		// noticed you yet, which would break sneak attacks - so it is off
		// by default.
		[[nodiscard]] bool IsTargetable(RE::Actor* a_actor)
		{
			const auto& settings = Settings::Get();
			if (!settings.ignoreFriendlyActors) {
				return true;
			}

			std::uint32_t boolBits = 0;
			if (Read(a_actor, kBoolBitsOff, boolBits) && (boolBits & GameOffsets::kActorPlayerTeammateBit) != 0) {
				return false;
			}

			if (settings.requireHostileTarget) {
				std::uint32_t combatTarget = 0;
				if (!Read(a_actor, GameOffsets::kCurrentCombatTarget, combatTarget) ||
					combatTarget != GameOffsets::kPlayerHandle) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool TryReadCandidate(const void* a_entry, const void* a_player, RE::NiPoint3& a_posOut)
		{
			if (!a_entry || a_entry == a_player) {
				return false;
			}

			std::uint8_t formType = 0;
			if (!Read(a_entry, kFormTypeOff, formType) || formType != kFormTypeACHR) {
				return false;
			}

			std::uint32_t boolBits = 0;
			if (Read(a_entry, kBoolBitsOff, boolBits) && (boolBits & kDeadBit) != 0) {
				return false;  // dead
			}

			return Read(a_entry, kLocationOff, a_posOut);
		}
	}

	std::optional<TargetPick> FindNearestActorToCrosshair(float a_maxRange, float a_maxConeDeg, RE::Actor* a_exclude, bool a_requireAlive, bool a_requireEngagedWithPlayer, bool a_requireOnScreen)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* playerCamera = RE::PlayerCamera::GetSingleton();
		if (!player || !playerCamera) {
			return std::nullopt;
		}

		auto* cell = player->parentCell;
		auto* cameraRoot = playerCamera->cameraRoot.get();
		if (!cell || !cameraRoot) {
			return std::nullopt;
		}

		std::uint32_t size = 0;
		std::uint32_t capacity = 0;
		std::uint64_t data = 0;
		if (!Read(cell, kCellReferencesOffset, size) ||
			!Read(cell, kCellReferencesOffset + 4, capacity) ||
			!Read(cell, kCellReferencesOffset + 8, data) ||
			size == 0 || capacity < size || !data) {
			return std::nullopt;
		}

		const auto& world = cameraRoot->world;
		const RE::NiPoint3 camPos = world.translate;

		// Row 1 of the rotation matrix is the view direction — empirically
		// verified in-game 2026-08-22 (dot product ~0.99 against the known
		// direction to a dead-center target, in two independent tests). Row
		// 0 (which NiCamera::WorldToScreen's depth term is built from) is
		// NOT forward here — it has zero Z and behaves like a lateral axis.
		RE::NiPoint3 camFwd{ world.rotate.entry[1].x, world.rotate.entry[1].y, world.rotate.entry[1].z };
		const float  fwdLen = std::sqrt(camFwd.x * camFwd.x + camFwd.y * camFwd.y + camFwd.z * camFwd.z);
		if (fwdLen < 1.0e-4f) {
			return std::nullopt;
		}
		camFwd.x /= fwdLen;
		camFwd.y /= fwdLen;
		camFwd.z /= fwdLen;

		std::optional<TargetPick> best;
		float                     bestCosAngle = std::cos(a_maxConeDeg * 0.01745329f);
		// Sanity bound against a garbage `size` (wrong offset, corrupted
		// read) turning this into a near-unbounded loop — NOT meant as a
		// practical limit. Found and fixed 2026-08-22: the previous cap of
		// 4096 was silently truncating real cells (a station interior with
		// 6900+ references), always missing whatever landed past index
		// 4096 — which a moving/following actor (added to the cell's
		// reference list dynamically as they walk in, rather than placed by
		// the level designer near the start of the array) reliably does.
		// Confirmed via a companion NPC found instantly on the player's ship
		// (a small cell, well under the old cap) but never found in any
		// larger location, despite standing in plain sight the whole time.
		constexpr std::uint32_t   kScanCap = 32768;
		const std::uint32_t       scanCount = std::min<std::uint32_t>(size, kScanCap);

		// Compact funnel telemetry — kept permanently (not just for one-off
		// diagnostics) because reverse-engineered offsets/axes in this
		// codebase have broken silently more than once. One line per scan,
		// cheap, always answers "how far did candidates get" without a
		// special diagnostic build.
		std::uint32_t nActorsSeen = 0;
		std::uint32_t nOutOfRange = 0;
		std::uint32_t nOutsideCone = 0;
		std::uint32_t nDeadSkipped = 0;
		std::uint32_t nFriendlySkipped = 0;
		std::uint32_t nNotEngagedSkipped = 0;
		std::uint32_t nOffScreenSkipped = 0;
		float         closestMissAngleDeg = -1.0f;  // smallest angle among actors that failed the cone check

		for (std::uint32_t i = 0; i < scanCount; ++i) {
			std::uint64_t entry = 0;
			if (!Read(reinterpret_cast<const void*>(data), 8ull * i, entry) || !entry) {
				continue;
			}

			auto* candidate = reinterpret_cast<RE::Actor*>(entry);
			if (a_exclude && candidate == a_exclude) {
				continue;
			}

			RE::NiPoint3 pos{};
			if (!TryReadCandidate(reinterpret_cast<const void*>(entry), player, pos)) {
				continue;
			}
			++nActorsSeen;

			const float dx = pos.x - camPos.x;
			const float dy = pos.y - camPos.y;
			const float dz = pos.z - camPos.z;
			const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (dist > a_maxRange || dist < 1.0e-4f) {
				++nOutOfRange;
				continue;
			}

			const float cosAngle = (dx * camFwd.x + dy * camFwd.y + dz * camFwd.z) / dist;
			const float angleDeg = std::acos(std::clamp(cosAngle, -1.0f, 1.0f)) * 57.29578f;
			if (cosAngle <= bestCosAngle) {
				++nOutsideCone;
				if (closestMissAngleDeg < 0.0f || angleDeg < closestMissAngleDeg) {
					closestMissAngleDeg = angleDeg;
				}
				continue;
			}

			// Checked only here, after this candidate has already beaten
			// every previous one on angle, so the cost lands on a handful
			// of actors per scan rather than on every reference.
			if (a_requireAlive && !IsAlive(candidate)) {
				++nDeadSkipped;
				continue;
			}

			if (!IsTargetable(candidate)) {
				++nFriendlySkipped;
				continue;
			}

			if (a_requireEngagedWithPlayer) {
				std::uint32_t combatTarget = 0;
				if (!Read(candidate, GameOffsets::kCurrentCombatTarget, combatTarget) ||
					combatTarget != GameOffsets::kPlayerHandle) {
					++nNotEngagedSkipped;
					continue;
				}
			}

			// Must actually be on screen, not merely inside the cone.
			//
			// The cone alone is not a visibility test and cannot be made
			// into one by choosing a number: at Starfield's default 90
			// degree horizontal FOV the screen edge sits at 45 degrees, so
			// the 60 degree advance cone was letting through targets the
			// player cannot see. That is exactly what Alexander hit -
			// auto-advance jumped to an enemy "behind" him, logged at 52.4
			// degrees off axis, i.e. genuinely off-screen rather than
			// genuinely behind.
			//
			// Projecting is the honest test, and it costs nothing extra:
			// the same projection already runs every frame to place the
			// target box, and it accounts for aspect ratio, which no single
			// angle can.
			if (a_requireOnScreen) {
				float sx = 0.0f;
				float sy = 0.0f;
				if (!UI::WorldToScreen(pos, sx, sy) ||
					sx < 0.0f || sx > 1.0f || sy < 0.0f || sy > 1.0f) {
					++nOffScreenSkipped;
					continue;
				}
			}

			bestCosAngle = cosAngle;
			best = TargetPick{ RE::NiPointer<RE::Actor>(candidate), dist, angleDeg };
		}

		if (scanCount < size) {
			REX::WARN("[targeting] cellRefs={} exceeds scan cap {} — {} entries were NOT scanned this call",
				size, kScanCap, size - scanCount);
		}
		REX::INFO("[targeting] cellRefs={} scanned={} actorsSeen={} outOfRange={} outsideCone={} deadSkipped={} friendlySkipped={} notEngagedSkipped={} offScreenSkipped={} closestMissAngle={:.1f}deg camFwd=({:.3f},{:.3f},{:.3f}) -> {}",
			size, scanCount, nActorsSeen, nOutOfRange, nOutsideCone, nDeadSkipped, nFriendlySkipped, nNotEngagedSkipped, nOffScreenSkipped, closestMissAngleDeg, camFwd.x, camFwd.y, camFwd.z, best ? "FOUND" : "none");

		return best;
	}

	RE::NiPointer<RE::Actor> GetCrosshairActivationTarget()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return nullptr;
		}

		RE::TESObjectREFR* target = nullptr;
		if (!Read(player, GameOffsets::kPlayerCommandTarget, target) || !target) {
			return nullptr;
		}

		std::uint8_t formType = 0;
		if (!Read(target, kFormTypeOff, formType) || formType != kFormTypeACHR) {
			return nullptr;  // not an actor (e.g. a container/terminal under the crosshair)
		}

		// Corpses were targetable until 2026-08-25 (Alexander noticed while
		// we were fixing the same root cause for auto-advance): this used
		// the boolBits dead bit, which never actually flips, so nothing was
		// ever filtered out. Health is the working test - see IsAlive.
		auto* actor = reinterpret_cast<RE::Actor*>(target);
		if (!IsAlive(actor)) {
			return nullptr;
		}

		if (!IsTargetable(actor)) {
			return nullptr;  // companion / non-hostile, see IsTargetable
		}

		return RE::NiPointer<RE::Actor>(actor);
	}

	bool HasDetectionLOS(RE::Actor* a_source, RE::Actor* a_target)
	{
		if (!a_source || !a_target) {
			return false;
		}
		// DISABLED 2026-08-22: this was a raw hand-cast call at
		// REL::ID(170456) with a calling shape (two leading dummy args,
		// (0, 0)) copied verbatim from Cassiopeia Papyrus Extender's usage
		// rather than independently verified — per Alexander, the last
		// confirmed crash-free build predates this function being wired
		// into the per-frame Locked overlay/aim-assist path, and every
		// hard-crash-without-log since (repeatedly, across several
		// rebuilds that changed unrelated things — scanner-close
		// mechanism, ImGui/display-size handling, AimAssist's mouse
		// hook — none of which fixed it) lines up with the exact frame
		// this first executes: the render thread's first Draw() call
		// after a lock, which is also this call's first-ever invocation
		// from this specific thread/context. Mapped, non-zero ID is not
		// proof of safety (see commonlibsf-unmapped-ids memory) — treat
		// as the prime suspect until proven otherwise with the real call
		// disabled. Returns true (no LOS gating — matches pre-feature
		// behavior) until the calling convention can be independently
		// re-derived, e.g. from Cassiopeia's actual disassembly rather
		// than by inference from its call site alone.
		return true;
	}
}
