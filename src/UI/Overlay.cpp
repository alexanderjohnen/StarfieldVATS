#include "Overlay.h"

#include "CameraProject.h"
#include "GameOffsets.h"
#include "SafeMem.h"
#include "Settings.h"
#include "Targeting.h"
#include "VATSController.h"

#include "RE/U/UI.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace VATS::UI
{
	namespace
	{
		// Starfield HUD palette: clean off-white with a faint cyan tint,
		// matching the game's own UI instead of FO4's saturated green.
		// Locked target is fully opaque; kAimColor (dimmer) is used for the
		// "TARGETING (N)" hint text — named for the now-removed Aiming
		// state it originally shared a look with, kept as-is since it's
		// still the right dim/uncommitted tint for that hint.
		constexpr ImU32 kLockedColor = IM_COL32(225, 238, 240, 235);
		constexpr ImU32 kAimColor = IM_COL32(225, 238, 240, 140);
		constexpr ImU32 kWhiteDim = IM_COL32(225, 238, 240, 90);
		// Dark outline drawn under every white stroke. Without it the box
		// reads fine against dark backdrops but vanishes completely against
		// Starfield's bright station-interior surfaces (confirmed in-game
		// 2026-08-22: telemetry said "drawn" the whole time, nothing was
		// visible on screen) — contrast against an unknown, HUD-brightness
		// background needs a shadow, not just more line thickness.
		constexpr ImU32 kOutline = IM_COL32(10, 14, 16, 200);

		// Body-part aim-point offset, mirrored from AimAssist.cpp (kept as
		// a small local duplicate rather than a shared header — same
		// ~20-line block, different callers, not worth forcing a shared
		// abstraction across a namespace boundary for). See
		// GameOffsets::kAngle's comment for the pack case's unverified
		// facing-direction assumption.
		RE::NiPoint3 ResolveBodyPartWorldPos(RE::Actor* a_actor, const RE::NiPoint3& a_pos, BodyPart a_part)
		{
			RE::NiPoint3 out = a_pos;
			switch (a_part) {
			case BodyPart::kHelmet:
				out.z += GameOffsets::kBodyPartHelmetZ;
				break;
			case BodyPart::kPack:
				{
					out.z += GameOffsets::kBodyPartPackZ;
					RE::NiPoint3 angle{};
					if (SafeRead(reinterpret_cast<const std::byte*>(a_actor) + GameOffsets::kAngle, &angle, sizeof(angle))) {
						const float yaw = angle.z;
						out.x -= std::sin(yaw) * GameOffsets::kBodyPartPackBackDistance;
						out.y -= std::cos(yaw) * GameOffsets::kBodyPartPackBackDistance;
					}
				}
				break;
			case BodyPart::kSuit:
			default:
				out.z += GameOffsets::kBodyPartSuitZ;
				break;
			}
			return out;
		}

		// How often the pre-lock scan re-evaluates "what's under the
		// crosshair right now" (drives the "TARGETING (N)" hint while Off).
		// A full cell scan (Targeting.cpp) every single Present call
		// (~60/s) would both cost more than needed and flood the log with
		// its funnel telemetry line; 10 Hz is still imperceptibly instant.
		constexpr auto kAimScanInterval = std::chrono::milliseconds(100);

		void DrawCornerBrackets(ImDrawList* a_dl, float a_x0, float a_y0, float a_x1, float a_y1, ImU32 a_color)
		{
			// Starfield's HUD brackets read as thin, slightly longer ticks
			// rather than FO4's thick chunky corners — but thin enough to
			// miss in combat clutter, so kept a notch above hairline.
			constexpr float kCorner = 16.0f;
			constexpr float kThick = 3.0f;
			constexpr float kOutlineThick = kThick + 2.0f;

			const ImVec2 kCorners[4][2] = {
				{ { a_x0, a_y0 }, { a_x0 + kCorner, a_y0 } },  // top-left horiz
				{ { a_x0, a_y0 }, { a_x0, a_y0 + kCorner } },  // top-left vert
				{ { a_x1 - kCorner, a_y0 }, { a_x1, a_y0 } },  // top-right horiz
				{ { a_x1, a_y0 }, { a_x1, a_y0 + kCorner } },  // top-right vert
			};
			const ImVec2 kCorners2[4][2] = {
				{ { a_x0, a_y1 - kCorner }, { a_x0, a_y1 } },  // bottom-left vert
				{ { a_x0, a_y1 }, { a_x0 + kCorner, a_y1 } },  // bottom-left horiz
				{ { a_x1, a_y1 - kCorner }, { a_x1, a_y1 } },  // bottom-right vert
				{ { a_x1 - kCorner, a_y1 }, { a_x1, a_y1 } },  // bottom-right horiz
			};

			// Dark outline pass first, colored strokes on top — keeps the
			// box legible against both dark and bright/white backgrounds.
			for (const auto& seg : kCorners) {
				a_dl->AddLine(seg[0], seg[1], kOutline, kOutlineThick);
			}
			for (const auto& seg : kCorners2) {
				a_dl->AddLine(seg[0], seg[1], kOutline, kOutlineThick);
			}
			for (const auto& seg : kCorners) {
				a_dl->AddLine(seg[0], seg[1], a_color, kThick);
			}
			for (const auto& seg : kCorners2) {
				a_dl->AddLine(seg[0], seg[1], a_color, kThick);
			}
		}

		void DrawCenteredText(ImDrawList* a_dl, float a_centerX, float a_y, const char* a_text, ImU32 a_color)
		{
			const auto   size = ImGui::CalcTextSize(a_text);
			const ImVec2 pos{ a_centerX - size.x * 0.5f, a_y };
			// Same outline treatment as the brackets: a 1px dark halo keeps
			// the text readable over bright surfaces.
			for (float ox = -1.0f; ox <= 1.0f; ox += 1.0f) {
				for (float oy = -1.0f; oy <= 1.0f; oy += 1.0f) {
					if (ox != 0.0f || oy != 0.0f) {
						a_dl->AddText(ImVec2{ pos.x + ox, pos.y + oy }, kOutline, a_text);
					}
				}
			}
			a_dl->AddText(pos, a_color, a_text);
		}

		// a_value may be null — used for the Aiming highlight, which no
		// longer shows a distance readout (Starfield's own scan reticle
		// already has one, see Draw() below), just the label.
		void DrawTargetBox(float a_px, float a_py, const char* a_label, const char* a_value, ImU32 a_color)
		{
			auto* dl = ImGui::GetForegroundDrawList();

			constexpr float kHalfW = 58.0f;
			constexpr float kHalfH = 36.0f;
			const float     x0 = a_px - kHalfW;
			const float     y0 = a_py - kHalfH;
			const float     x1 = a_px + kHalfW;
			const float     y1 = a_py + kHalfH;

			// No filled background — Starfield's HUD elements sit directly
			// over the scene rather than on a tinted panel.
			DrawCornerBrackets(dl, x0, y0, x1, y1, a_color);
			if (a_value) {
				DrawCenteredText(dl, a_px, y0 + 6.0f, a_label, a_color);
				DrawCenteredText(dl, a_px, a_py + 2.0f, a_value, a_color);
			} else {
				DrawCenteredText(dl, a_px, a_py - 6.0f, a_label, a_color);
			}
		}

		// Converts a Windows virtual-key code to a single displayable
		// character for the hotkey hint. Covers the practical range
		// (letters, digits) — anything else falls back to "?" rather than
		// showing garbage; good enough since the INI default and every
		// setting Alexander has used so far is a plain letter key.
		char VKToDisplayChar(std::uint32_t a_vk)
		{
			if ((a_vk >= 'A' && a_vk <= 'Z') || (a_vk >= '0' && a_vk <= '9')) {
				return static_cast<char>(a_vk);
			}
			return '?';
		}
	}

	namespace
	{
		// Funnel telemetry for the per-frame Draw() path, throttled to
		// "log only on change" — Draw() runs once per Present (up to
		// ~60/s), so unconditional logging would flood the file. Matches
		// the always-on style of Targeting.cpp's telemetry, just gated.
		enum class DrawOutcome
		{
			kOff,
			kNoTarget,
			kDead,
			kLocationReadFailed,
			kProjectionFailed,
			kOffScreen,
			kDrawn,
		};

		DrawOutcome   s_lastOutcome = DrawOutcome::kOff;
		std::uint32_t s_lastFormID = 0;

		void LogIfChanged(DrawOutcome a_outcome, std::uint32_t a_formID, const char* a_detail)
		{
			if (a_outcome == s_lastOutcome && a_formID == s_lastFormID) {
				return;
			}
			s_lastOutcome = a_outcome;
			s_lastFormID = a_formID;
			REX::INFO("[overlay] formID=0x{:08X} {}", a_formID, a_detail);
		}

		// Dead-check + position read + projection, shared by the Aiming
		// highlight, the Locked target box, and the "TARGETING (N)" hint —
		// all three must agree on whether the target is currently valid and
		// on-screen. Originally the hint only checked "is there a cached
		// actor pointer at all", which let it show for a stale/dangling
		// commandTarget value during loading screens (found 2026-08-22,
		// screenshot showed the hint on a black loading screen with no
		// scanner or aim in progress) — routing it through this same
		// validation closes that gap without needing a separate "is the
		// scanner UI active" signal, which we don't have.
		bool ResolveOnScreen(RE::Actor* a_actor, BodyPart a_part, float& a_outSx, float& a_outSy, float& a_outDist)
		{
			const std::uint32_t formID = a_actor->GetFormID();

			// Dead or invalid targets: keep the overlay quiet rather than
			// tracking a corpse. All reads are guarded — a stale pointer
			// degrades to "no box", never a crash.
			std::uint32_t boolBits = 0;
			const bool    boolBitsRead = SafeRead(reinterpret_cast<const std::byte*>(a_actor) + GameOffsets::kBoolBits, &boolBits, sizeof(boolBits));
			if (boolBitsRead && (boolBits & GameOffsets::kActorDeadBit) != 0) {
				char detail[64];
				std::snprintf(detail, sizeof(detail), "skipped: dead bit set (boolBits=0x%08X)", boolBits);
				LogIfChanged(DrawOutcome::kDead, formID, detail);
				return false;
			}

			RE::NiPoint3 pos{};
			if (!SafeRead(reinterpret_cast<const std::byte*>(a_actor) + GameOffsets::kLocation, &pos, sizeof(pos))) {
				LogIfChanged(DrawOutcome::kLocationReadFailed, formID, "skipped: location read failed");
				return false;
			}
			pos = ResolveBodyPartWorldPos(a_actor, pos, a_part);

			float sx = 0.0f;
			float sy = 0.0f;
			if (!WorldToScreen(pos, sx, sy)) {
				char detail[96];
				std::snprintf(detail, sizeof(detail), "skipped: WorldToScreen failed (pos=(%.1f,%.1f,%.1f))", pos.x, pos.y, pos.z);
				LogIfChanged(DrawOutcome::kProjectionFailed, formID, detail);
				return false;
			}
			if (sx < -0.1f || sx > 1.1f || sy < -0.1f || sy > 1.1f) {
				char detail[64];
				std::snprintf(detail, sizeof(detail), "skipped: off-screen (sx=%.2f, sy=%.2f)", sx, sy);
				LogIfChanged(DrawOutcome::kOffScreen, formID, detail);
				return false;  // far off-screen
			}

			float dist = -1.0f;
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				const auto  pp = player->GetPosition();
				const float dx = pos.x - pp.x;
				const float dy = pos.y - pp.y;
				const float dz = pos.z - pp.z;
				dist = std::sqrt(dx * dx + dy * dy + dz * dz);
			}

			char detail[48];
			std::snprintf(detail, sizeof(detail), "drawn (dist=%.1fm)", dist);
			LogIfChanged(DrawOutcome::kDrawn, formID, detail);

			a_outSx = sx;
			a_outSy = sy;
			a_outDist = dist;
			return true;
		}

		// Draws the box for an already-resolved on-screen position (see
		// ResolveOnScreen). Kept separate so the hint text and the box can
		// share one resolution per frame instead of computing it twice.
		void DrawIfVisible(RE::Actor* a_actor, BodyPart a_part, const char* a_label, ImU32 a_color, bool a_showValue)
		{
			float sx = 0.0f;
			float sy = 0.0f;
			float dist = -1.0f;
			if (!ResolveOnScreen(a_actor, a_part, sx, sy, dist)) {
				return;
			}

			// Hit-chance cone (mirrors AimAssist.cpp's ComputeChancePercent
			// — same small duplicate-instead-of-shared-header tradeoff as
			// ResolveBodyPartWorldPos above): 100% dead center on the
			// crosshair (true screen center), falling off linearly to 0%
			// at Settings::assistRadius, ANDed with a real occlusion check
			// (HasDetectionLOS — see Targeting.h). Shown live so Alexander
			// can see exactly what the aim-assist would roll against right
			// now, not just after firing.
			char value[32] = "--";
			if (dist >= 0.0f) {
				float chancePercent = 0.0f;
				if (auto* player = RE::PlayerCharacter::GetSingleton(); player && HasDetectionLOS(player, a_actor)) {
					const float dx = sx - 0.5f;
					const float dy = sy - 0.5f;
					const float screenDist = std::sqrt(dx * dx + dy * dy);
					const float radius = Settings::Get().assistRadius;
					const float t = radius > 0.0f ? std::clamp(1.0f - screenDist / radius, 0.0f, 1.0f) : 0.0f;
					chancePercent = t * static_cast<float>(Settings::Get().centerHitChancePercent);
				}
				std::snprintf(value, sizeof(value), "%.0f m | %.0f%%", dist, chancePercent);
			}

			const auto& io = ImGui::GetIO();
			DrawTargetBox(sx * io.DisplaySize.x, sy * io.DisplaySize.y, a_label, a_showValue ? value : nullptr, a_color);

			// Small tether line from box toward screen bottom, echoing the
			// FO4 VATS look; subtle, mostly for readability against clutter.
			auto* dl = ImGui::GetForegroundDrawList();
			dl->AddLine(
				ImVec2{ sx * io.DisplaySize.x, sy * io.DisplaySize.y + 36.0f },
				ImVec2{ sx * io.DisplaySize.x, sy * io.DisplaySize.y + 56.0f },
				kWhiteDim, 1.5f);
		}
	}

	void Draw()
	{
		// A menu or transition that takes the player out of normal
		// crosshair-aiming gameplay should actually END an active lock, not
		// just hide the overlay — a hidden-but-still-Locked state was found
		// 2026-08-22 to leave stale state around (e.g. the box/status text
		// briefly showing a now-irrelevant target right as a menu closed).
		// `UI::menusVisible` (plain data field, offset 0x4FA) covers most
		// blocking menus (pause via Esc used to leak through until this was
		// found - screenshot 2026-08-22 showed "VATS: LOCKED" and the
		// target box right over the pause list). It does NOT cover
		// Starfield's Tab-opened character hub ("DataMenu", RTTI-confirmed
		// in CommonLibSF) — that one leaked through separately (also
		// screenshot-confirmed) and needs its own explicit IsMenuOpen
		// check, same pattern as MonocleMenu. Alexander additionally called
		// out dialogue and the star map as contexts that should end a lock
		// even if menusVisible turns out to already cover them - explicit
		// checks here either way, cheap and self-documenting. "StarMap" is
		// a best-effort guess: CommonLibSF has no top-level RTTI class
		// under exactly that name, only a large family of nested UI-data
		// structs prefixed "StarMap__" that strongly suggest it's the real
		// menu name — unconfirmed in-game, degrades to a no-op if wrong. A
		// cell-transition loading screen is covered by "LoadingMenu" (RTTI-
		// confirmed; also the file named in starfield-vats-mod-design
		// memory, loadingmenu.swf).
		if (auto* ui = RE::UI::GetSingleton()) {
			const bool blockingMenuOpen = ui->menusVisible ||
			                               ui->IsMenuOpen("DataMenu") ||
			                               ui->IsMenuOpen("StarMap") ||
			                               ui->IsMenuOpen("DialogueMenu") ||
			                               ui->IsMenuOpen("LoadingMenu") ||
			                               ui->IsMenuOpen("PauseMenu");
			if (blockingMenuOpen) {
				Controller::Get().ForceOff();
				return;
			}
		}

		const auto state = Controller::Get().GetOverlayState();

		// Unconditional, always-drawn ground-truth readout: independent of
		// target acquisition, dead checks, projection, off-screen culling —
		// anything that could be blamed for a mismatch. Added 2026-08-22
		// after an extended "box shows on OFF" investigation that turned out
		// to be Starfield's own dev console lagging behind by a line, not
		// our code — this text is what settled it, keep it around as the
		// standing ground truth rather than trusting the console's timing.
		{
			auto*       dl = ImGui::GetForegroundDrawList();
			const char* modeLabel = "OFF";
			ImU32       statusColor = IM_COL32(255, 100, 100, 255);
			switch (state.mode) {
			case VATSMode::kLocked:
				modeLabel = "LOCKED";
				statusColor = IM_COL32(120, 255, 140, 255);
				break;
			default:
				break;
			}
			char statusLine[64];
			std::snprintf(statusLine, sizeof(statusLine), "VATS: %s", modeLabel);
			dl->AddText(ImVec2{ 20.0f, 20.0f }, IM_COL32(0, 0, 0, 220), statusLine);  // outline
			dl->AddText(ImVec2{ 21.0f, 21.0f }, statusColor, statusLine);
		}

		// Re-evaluate "what's under the crosshair" at kAimScanInterval rather
		// than every frame — see the comment on that constant. Only needed
		// while Off, purely to drive the "TARGETING (N)" hint below (Locked
		// already has its own committed target). Source: PlayerCharacter's
		// crosshair-activation target (GetCrosshairActivationTarget) — real
		// engine-computed pick, pixel-precise, respects occlusion for free
		// (confirmed via in-game telemetry 2026-08-22, see
		// starfield-vats-ui-hook memory). No cone-scan fallback — that used
		// to exist ("nothing under crosshair? do a wide-cone pick instead")
		// but it doesn't respect occlusion, silently reintroducing
		// through-wall targeting. Range is naturally limited to normal
		// interaction distance while plain-aiming, but extends to the hand
		// scanner's own range (confirmed ~17m+ through cover, scanner's own
		// reticle claims 100m) when Alexander toggles the scanner on — same
		// underlying pick, longer reach, no code needed on our side for
		// that. See starfield-vats-mod-design memory, 2026-08-22.
		static auto                     s_lastScan = std::chrono::steady_clock::time_point{};
		static RE::NiPointer<RE::Actor> s_cachedPick;

		if (state.mode == VATSMode::kOff) {
			const auto now = std::chrono::steady_clock::now();
			if (now - s_lastScan >= kAimScanInterval) {
				s_lastScan = now;
				s_cachedPick = GetCrosshairActivationTarget();
			}
		} else {
			s_cachedPick = nullptr;
		}

		// Is the hand scanner ("MonocleMenu" — data\interface\monoclemenu.swf
		// in Starfield - Interface.ba2) currently open? Confirmed via
		// telemetry 2026-08-22 that UI::IsMenuOpen cleanly tracks the real
		// scanner toggle state (0/1, changing exactly on scan open/close).
		// ScannableComponent::GetOutlineState was tried first as a possible
		// "is this actor actively outlined right now" signal but returned 1
		// for a target even while the scanner was confirmed closed — reads
		// more like "is this a scannable component type" than a live
		// visibility signal, dropped.
		bool isScanning = false;
		if (auto* ui = RE::UI::GetSingleton()) {
			isScanning = ui->IsMenuOpen("MonocleMenu");
		}

		// "TARGETING (N)" hotkey hint — shown only while the hand scanner is
		// open (isScanning, see above) AND something valid is under the
		// crosshair, regardless of whether VATS is already engaged, so
		// Alexander knows the hotkey would do something useful right now.
		// Gated through ResolveOnScreen (same dead/position/projection/
		// on-screen checks as the actual box) rather than just "is
		// s_cachedPick non-null" — the looser check let the hint show
		// during a black loading screen off a stale commandTarget pointer
		// from the previous session (screenshot-confirmed 2026-08-22).
		//
		// Position derived from exact Photoshop measurements of a
		// 2560x1440 screenshot of the MonocleMenu scan reticle (2026-08-22):
		// the reticle circle is always centered on true screen center (it's
		// a crosshair-locked HUD element), radius 463px; "RANGE 100M" sits
		// 11px right of the circle's left edge, 6px above the horizontal
		// centerline, left-aligned (grows rightward/inward). Mirrored:our
		// hint sits 11px left of the circle's right edge, same 6px-above
		// offset, but right-aligned (grows leftward/inward) since it's the
		// mirror image — hence computing text width via CalcTextSizeA at
		// our actual custom font size rather than guessing an origin point.
		// All expressed as screen fractions of that 2560x1440 reference so
		// it scales with any resolution.
		if (isScanning && state.mode == VATSMode::kOff) {
			float sx = 0.0f, sy = 0.0f, dist = 0.0f;
			if (s_cachedPick && ResolveOnScreen(s_cachedPick.get(), BodyPart::kSuit, sx, sy, dist)) {
				char hint[32];
				std::snprintf(hint, sizeof(hint), "TARGETING (%c)", VKToDisplayChar(Settings::Get().activationKeyVK));
				const auto& io = ImGui::GetIO();
				auto*       dl = ImGui::GetForegroundDrawList();
				auto*       font = ImGui::GetFont();
				const float fontSize = ImGui::GetFontSize() * 1.6f;
				const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, hint);
				const float  rightEdgeX = io.DisplaySize.x * ((1280.0f + 463.0f - 11.0f) / 2560.0f);
				// Treated as the vertical CENTER of the target point, not
				// its top edge — our text renders noticeably taller than
				// RANGE's own (1.6x font scale), so anchoring by top edge
				// let it hang down across the centerline instead of
				// sitting above it (screenshot-confirmed 2026-08-22).
				const float  yCenter = io.DisplaySize.y * ((720.0f - 20.0f) / 1440.0f);  // -6 initial + 14px manual nudge (2026-08-22)
				const ImVec2 hintPos{ rightEdgeX - textSize.x, yCenter - textSize.y * 0.5f };
				dl->AddText(font, fontSize, ImVec2{ hintPos.x + 1.5f, hintPos.y + 1.5f }, kOutline, hint);
				dl->AddText(font, fontSize, hintPos, kAimColor, hint);
			}
		}

		if (state.mode == VATSMode::kOff) {
			LogIfChanged(DrawOutcome::kOff, 0, "off");
			return;
		}

		// Locked
		if (!state.actor) {
			LogIfChanged(DrawOutcome::kNoTarget, 0, "locked but no target (unexpected)");
			return;
		}

		// End the lock outright once the target dies, rather than leaving
		// VATS Locked on a corpse until the player notices and presses the
		// hotkey themselves — Alexander's call, 2026-08-22. Auto-advancing
		// to a new target (if oxygen/AP allows) was also floated but needs
		// the O2-cost system this project doesn't have yet; not attempted
		// here, ForceOff only for now.
		{
			std::uint32_t boolBits = 0;
			if (SafeRead(reinterpret_cast<const std::byte*>(state.actor.get()) + GameOffsets::kBoolBits, &boolBits, sizeof(boolBits)) &&
				(boolBits & GameOffsets::kActorDeadBit) != 0) {
				Controller::Get().ForceOff();
				return;
			}
		}

		const char* partLabel = "SUIT";
		switch (state.bodyPart) {
		case BodyPart::kHelmet:
			partLabel = "HELMET";
			break;
		case BodyPart::kPack:
			partLabel = "PACK";
			break;
		case BodyPart::kSuit:
		default:
			break;
		}
		DrawIfVisible(state.actor.get(), state.bodyPart, partLabel, kLockedColor, /*a_showValue*/ true);

		// RE::Actor::GetActorKnowledge was investigated 2026-08-22 as a
		// possible live "can the player currently see this target" signal
		// (the game's own AI/stealth detection bookkeeping). Dead end:
		// CommonLibSF's IDs.h has `GetActorKnowledge{ 0 }` — Address
		// Library ID 0, meaning the function was named/documented by the
		// community but its real address was never actually resolved for
		// this game version. Calling it hard-fails immediately with a
		// CommonLibSF dialog ("Failed to find offset for Address Library
		// ID! Invalid ID: 0") rather than crashing blind — confirmed
		// in-game. Not usable without someone finding the real address
		// with a disassembler. See commonlibsf-unmapped-ids memory.
	}
}
