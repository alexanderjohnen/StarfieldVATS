#include "Overlay.h"

#include "CameraProject.h"
#include "GameOffsets.h"
#include "HealthReader.h"
#include "SafeMem.h"
#include "WorldBoundProbe.h"
#include "Settings.h"
#include "Targeting.h"
#include "VATSController.h"
#include "CompanionShield.h"
#include "VatsResource.h"

#include "RE/U/UI.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <unordered_map>
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
		// Dimmed by VALUE, not by alpha. It used to be the locked colour
		// at alpha 140, and against the dark halo underneath every glyph
		// that reads as grey mud rather than as dim white - the halo shows
		// straight through the semi-transparent strokes and eats the
		// antialiased edges. Alexander, 2026-08-26: "sieht nicht richtig
		// weiss aus". A darker opaque tone reads as "dimmer" without
		// letting anything through it.
		constexpr ImU32 kAimColor = IM_COL32(186, 202, 208, 245);

		// Support sessions get their own colour for one reason: at a glance,
		// the player must never confuse "I am about to help this person" with
		// "I am about to shoot this person". Green reads as friendly against
		// the scanner own blue-white lettering without competing with it.
		constexpr ImU32 kSupportColor = IM_COL32(120, 235, 150, 240);

		// Dark outline drawn under every white stroke. Without it the box
		// reads fine against dark backdrops but vanishes completely against
		// Starfield's bright station-interior surfaces (confirmed in-game
		// 2026-08-22: telemetry said "drawn" the whole time, nothing was
		// visible on screen) — contrast against an unknown, HUD-brightness
		// background needs a shadow, not just more line thickness.
		constexpr ImU32 kOutline = IM_COL32(10, 14, 16, 200);

		// How often the pre-lock scan re-evaluates "what's under the
		// crosshair right now" (drives the "TARGETING (N)" hint while Off).
		// A full cell scan (Targeting.cpp) every single Present call
		// (~60/s) would both cost more than needed and flood the log with
		// its funnel telemetry line; 10 Hz is still imperceptibly instant.
		constexpr auto kAimScanInterval = std::chrono::milliseconds(100);

		// Starfield's own scanner draws a thin ring with the range set
		// beside it rather than a bracketed box - Alexander's call
		// 2026-08-26, replacing the FO4-style corner brackets this started
		// with. The brackets never looked like they belonged to this
		// game's HUD; the ring does, and it also gives the two bars
		// somewhere natural to live (see the arc gauges below).
		void DrawTargetRing(ImDrawList* a_dl, float a_cx, float a_cy, float a_r, ImU32 a_color)
		{
			// Dark pass underneath every stroke, same reasoning as the
			// brackets had: confirmed 2026-08-22 that a white line alone
			// vanishes completely against bright station interiors.
			a_dl->AddCircle(ImVec2{ a_cx, a_cy }, a_r, kOutline, 64, 4.0f);
			a_dl->AddCircle(ImVec2{ a_cx, a_cy }, a_r, a_color, 64, 2.0f);
		}

		// One gauge drawn as an arc on the ring. a_aMin -> a_aMax is the
		// sweep, and the fill always grows from a_aMin, so passing the
		// left-hand end first makes both gauges fill left-to-right no
		// matter which half of the ring they sit on.
		void DrawArcGauge(ImDrawList* a_dl, float a_cx, float a_cy, float a_r,
			float a_aMin, float a_aMax, float a_frac, ImU32 a_track, ImU32 a_fill, float a_thickness)
		{
			constexpr int kSegments = 48;
			const ImVec2  centre{ a_cx, a_cy };

			a_dl->PathArcTo(centre, a_r, a_aMin, a_aMax, kSegments);
			a_dl->PathStroke(kOutline, 0, a_thickness + 3.0f);
			a_dl->PathArcTo(centre, a_r, a_aMin, a_aMax, kSegments);
			a_dl->PathStroke(a_track, 0, a_thickness);

			if (a_frac > 0.0f) {
				a_dl->PathArcTo(centre, a_r, a_aMin, a_aMin + (a_aMax - a_aMin) * a_frac, kSegments);
				a_dl->PathStroke(a_fill, 0, a_thickness);
			}
		}

		void DrawCenteredText(ImDrawList* a_dl, float a_centerX, float a_y, const char* a_text, ImU32 a_color)
		{
			const auto   size = ImGui::CalcTextSize(a_text);
			const ImVec2 pos{ a_centerX - size.x * 0.5f, a_y };
			// Four cardinal offsets, not all eight. A full ring of dark
			// copies around small glyphs closes their counters and thickens
			// every stroke, which is half of why the text read as blocky;
			// the diagonals contribute almost nothing to legibility and
			// most of the mud. Softer than the ring under the ring strokes
			// for the same reason - a letter has far more edge per pixel
			// than a line does.
			constexpr ImU32 kTextShadow = IM_COL32(10, 14, 16, 165);
			const ImVec2    kOffsets[4]{ { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } };
			for (const auto& off : kOffsets) {
				a_dl->AddText(ImVec2{ pos.x + off.x, pos.y + off.y }, kTextShadow, a_text);
			}
			a_dl->AddText(pos, a_color, a_text);
		}

		// Fallback radius, used when the target's real size can't be
		// determined. Was a half-width/half-height pair when this was a
		// box; a ring only needs the one number.
		constexpr float kDefaultRadius = 36.0f;

		// a_value may be null - the pre-lock "TARGETING (N)" hint has no
		// distance readout, since Starfield's own scan reticle already
		// shows one.
		//
		// No "TARGET" caption any more (Alexander, 2026-08-26): the ring
		// plus the two gauges already say everything the word did, and the
		// scanner it is imitating does not label its own reticle either.
		void DrawTargetMarker(float a_px, float a_py, const char* a_label, const char* a_value, ImU32 a_color, float a_radius)
		{
			auto* dl = ImGui::GetForegroundDrawList();
			DrawTargetRing(dl, a_px, a_py, a_radius, a_color);

			if (a_value) {
				// Set beside the ring rather than inside it, the way the
				// scanner puts its range next to the reticle instead of
				// over the thing being scanned - which is the point: the
				// middle of the ring is where the target is.
				const float tickInner = a_radius + 3.0f;
				const float tickOuter = a_radius + 9.0f;
				dl->AddLine(ImVec2{ a_px + tickInner, a_py }, ImVec2{ a_px + tickOuter, a_py }, kOutline, 4.0f);
				dl->AddLine(ImVec2{ a_px + tickInner, a_py }, ImVec2{ a_px + tickOuter, a_py }, a_color, 2.0f);

				// Sitting ON the tick's height is not what the scanner
				// does - its range number rests just ABOVE the horizontal
				// line rather than straddling it (Alexander, comparing the
				// two side by side, 2026-08-26). One text height up.
				const auto   size = ImGui::CalcTextSize(a_value);
				const ImVec2 pos{ a_px + tickOuter + 4.0f, a_py - size.y - 1.0f };
				constexpr ImU32 kTextShadow = IM_COL32(10, 14, 16, 165);
				const ImVec2    kOffsets[4]{ { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } };
				for (const auto& off : kOffsets) {
					dl->AddText(ImVec2{ pos.x + off.x, pos.y + off.y }, kTextShadow, a_value);
				}
				dl->AddText(pos, a_color, a_value);
			} else if (a_label) {
				DrawCenteredText(dl, a_px, a_py - a_radius - 18.0f, a_label, a_color);
			}
		}

		// Both gauges are arcs sitting just OUTSIDE the ring, on opposite
		// halves: the target's health below, the player's VATS budget
		// above.
		//
		// The first version put the resource arc INSIDE the ring, so that
		// "mine inside, theirs outside" would tell the two apart without
		// captions. Dropped 2026-08-26 for the same reason the distance
		// readout was moved out of the ring in the first place: the middle
		// of the ring is where the TARGET is. Painting the player's own
		// budget across an enemy's chest puts one of the two things that
		// have nothing to do with each other on top of the other, and it
		// costs the ring its one job - being a clean window onto the
		// thing being aimed at. Starfield's own scanner reticle keeps its
		// interior empty too.
		//
		// Nothing is lost in telling them apart, because inside/outside
		// was the fourth cue and the other three still hold: opposite
		// halves of the ring, red against the HUD's off-white, and one
		// noticeably heavier than the other. Sharing a radius is what makes
		// them read as a matched pair rather than two things that happened
		// to land near the same circle.
		//
		// Angles: 0 is +x, and screen y grows downward, so PI/2 is the
		// BOTTOM of the ring and 3*PI/2 the top. Each gauge is given its
		// left-hand end first so both fill left-to-right.
		constexpr float kPi = 3.14159265358979323846f;

		[[nodiscard]] float GaugeRadius(float a_ringRadius, float a_scale)
		{
			return a_ringRadius + std::max(5.0f, 8.0f * a_scale);
		}

		// Plain health arc. The segmented boss/legendary variant (a row of
		// pips marking reserve health pools) was removed 2026-08-25: it
		// rested on an unverified guess that the legendaryRank actor value
		// drives that display, it never actually worked - the pip row
		// stayed rigid regardless of the target - and Alexander's call was
		// that it isn't worth another multi-day offset hunt. Drop rather
		// than keep dead decoration.
		void DrawHealthArc(ImDrawList* a_dl, float a_cx, float a_cy, float a_ringRadius, float a_current, float a_max, float a_scale)
		{
			const float frac = a_max > 0.0f ? std::clamp(a_current / a_max, 0.0f, 1.0f) : 0.0f;
			const float thickness = std::max(3.0f, 5.0f * a_scale);
			DrawArcGauge(a_dl, a_cx, a_cy, GaugeRadius(a_ringRadius, a_scale), kPi, 0.0f, frac,
				IM_COL32(50, 12, 12, 220), IM_COL32(210, 40, 40, 235), thickness);
		}

		// The VATS resource arc. Deliberately thinner than the health arc
		// and in the HUD's own off-white rather than a second saturated
		// colour, so the two read as "the target's" and "mine" at a glance
		// instead of competing. Turns amber as it runs low, since running
		// dry ends the lock outright and that is worth seeing coming.
		// The companion shield arc. Occupies the same top half and the same
		// radius as the VATS resource arc, because the two can never appear
		// together: the budget belongs to a combat lock, the shield to a
		// support session. One gauge per mode, in the same place, so the eye
		// learns one position rather than two.
		void DrawShieldArc(ImDrawList* a_dl, float a_cx, float a_cy, float a_ringRadius, float a_remaining, float a_capacity, float a_scale)
		{
			const float frac = a_capacity > 0.0f ? std::clamp(a_remaining / a_capacity, 0.0f, 1.0f) : 0.0f;
			const float thickness = std::max(2.0f, 3.0f * a_scale);
			DrawArcGauge(a_dl, a_cx, a_cy, GaugeRadius(a_ringRadius, a_scale), kPi, 2.0f * kPi, frac,
				IM_COL32(14, 30, 20, 210), kSupportColor, thickness);
		}

		void DrawResourceArc(ImDrawList* a_dl, float a_cx, float a_cy, float a_ringRadius, float a_current, float a_capacity, float a_scale)
		{
			const float frac = a_capacity > 0.0f ? std::clamp(a_current / a_capacity, 0.0f, 1.0f) : 0.0f;
			const float thickness = std::max(2.0f, 3.0f * a_scale);
			const ImU32 fill = frac < 0.25f ? IM_COL32(240, 170, 60, 240) : kLockedColor;
			DrawArcGauge(a_dl, a_cx, a_cy, GaugeRadius(a_ringRadius, a_scale), kPi, 2.0f * kPi, frac,
				IM_COL32(18, 26, 28, 210), fill, thickness);
		}

		// Converts a Windows virtual-key code to a short displayable label
		// for the hotkey hint. Covers letters/digits and F1-F24 (found
		// 2026-08-22: Alexander rebound iActivationKey to F17/0x80 -
		// VK_F1..VK_F24 are one contiguous range, 0x70-0x87, so a single
		// bounds check covers all of them) — anything else falls back to
		// "?" rather than showing garbage. Writes into a_out (must be at
		// least 4 bytes: "F24\0") rather than returning a single char,
		// since F-key labels are multi-character.
		void VKToDisplayLabel(std::uint32_t a_vk, char (&a_out)[8])
		{
			if ((a_vk >= 'A' && a_vk <= 'Z') || (a_vk >= '0' && a_vk <= '9')) {
				a_out[0] = static_cast<char>(a_vk);
				a_out[1] = '\0';
				return;
			}
			constexpr std::uint32_t kVK_F1 = 0x70;
			constexpr std::uint32_t kVK_F24 = 0x87;
			if (a_vk >= kVK_F1 && a_vk <= kVK_F24) {
				std::snprintf(a_out, sizeof(a_out), "F%u", a_vk - kVK_F1 + 1);
				return;
			}
			std::snprintf(a_out, sizeof(a_out), "?");
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
			VATS_TRACE("[overlay] formID=0x{:08X} {}", a_formID, a_detail);
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
		bool ResolveOnScreen(RE::Actor* a_actor, float& a_outSx, float& a_outSy, float& a_outDist)
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
			pos = WorldBoundProbe::GetAimPoint(a_actor, pos);

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

		// --- Box-centring diagnostic (Settings::debugAimMarkers) ---
		//
		// Three attempts to centre the target box have all adjusted the
		// vertical aim point, and all three were guesses, because nobody
		// had yet measured which of the two candidate causes is actually
		// at work. This draws both candidates on screen at once so a
		// single look settles it:
		//
		//   FEET   - the actor's own origin, projected. Nothing is added
		//            to it, so if this cross does not sit on the target's
		//            feet, the PROJECTION is wrong (FOV value or FOV axis)
		//            and no aim-point tuning can ever fix the box.
		//   SPHERE - the raw bounding-sphere centre. If FEET is right and
		//            this one is not on the target's midriff, the sphere
		//            itself is off-centre for that skeleton (a weapon or
		//            backpack pulling it sideways would show here as a
		//            HORIZONTAL offset, which every theory so far has
		//            assumed away).
		//   AIM    - the lifted point the box is actually drawn at. If
		//            FEET and SPHERE are both right and only this one is
		//            high or low, the lift is the whole problem and
		//            fAimPointRadiusFactor is the right knob after all.
		void DrawCross(ImDrawList* a_dl, float a_x, float a_y, ImU32 a_color, const char* a_label)
		{
			constexpr float kArm = 11.0f;
			a_dl->AddLine(ImVec2{ a_x - kArm, a_y }, ImVec2{ a_x + kArm, a_y }, kOutline, 4.0f);
			a_dl->AddLine(ImVec2{ a_x, a_y - kArm }, ImVec2{ a_x, a_y + kArm }, kOutline, 4.0f);
			a_dl->AddLine(ImVec2{ a_x - kArm, a_y }, ImVec2{ a_x + kArm, a_y }, a_color, 2.0f);
			a_dl->AddLine(ImVec2{ a_x, a_y - kArm }, ImVec2{ a_x, a_y + kArm }, a_color, 2.0f);
			DrawCenteredText(a_dl, a_x, a_y + kArm + 2.0f, a_label, a_color);
		}

		void DrawAimDiagnostics(RE::Actor* a_actor)
		{
			RE::NiPoint3 feet{};
			if (!SafeRead(reinterpret_cast<const std::byte*>(a_actor) + GameOffsets::kLocation, &feet, sizeof(feet))) {
				return;
			}
			RE::NiPoint3 centre{};
			const bool   haveCentre = WorldBoundProbe::GetBoundCenter(a_actor, centre);
			const RE::NiPoint3 aim = WorldBoundProbe::GetAimPoint(a_actor, feet);

			float fx = 0.0f, fy = 0.0f, cx = 0.0f, cy = 0.0f, ax = 0.0f, ay = 0.0f;
			const bool haveFeetScreen = WorldToScreen(feet, fx, fy);
			const bool haveCentreScreen = haveCentre && WorldToScreen(centre, cx, cy);
			const bool haveAimScreen = WorldToScreen(aim, ax, ay);

			const auto& io = ImGui::GetIO();
			auto*       dl = ImGui::GetForegroundDrawList();
			if (haveFeetScreen) {
				DrawCross(dl, fx * io.DisplaySize.x, fy * io.DisplaySize.y, IM_COL32(90, 240, 120, 240), "FEET");
			}
			if (haveCentreScreen) {
				DrawCross(dl, cx * io.DisplaySize.x, cy * io.DisplaySize.y, IM_COL32(90, 180, 255, 240), "SPHERE");
			}
			if (haveAimScreen) {
				DrawCross(dl, ax * io.DisplaySize.x, ay * io.DisplaySize.y, IM_COL32(255, 100, 215, 240), "AIM");
			}

			// Logged on movement rather than per frame - same reasoning as
			// every other diagnostic here: log volume has affected timing
			// in this project before. The offset from screen centre is
			// logged alongside because it is the discriminator for the FOV
			// axis: a wrong axis produces error proportional to it, a
			// wrong aim point does not.
			if (!haveFeetScreen) {
				return;
			}
			struct LastLogged
			{
				float x;
				float y;
			};
			static std::unordered_map<std::uint32_t, LastLogged> s_last;
			const std::uint32_t                                  formID = a_actor->GetFormID();
			const auto                                           it = s_last.find(formID);
			if (it != s_last.end() &&
				std::abs(it->second.x - fx) < 0.01f && std::abs(it->second.y - fy) < 0.01f) {
				return;
			}
			s_last[formID] = LastLogged{ fx, fy };
			Log::Cap(s_last);

			const auto& settings = Settings::Get();
			VATS_TRACE("[VATS] aimdiag: formID=0x{:08X} feet=({:.3f},{:.3f}) sphere=({:.3f},{:.3f}) aim=({:.3f},{:.3f}) offCentre=({:+.3f},{:+.3f}) display={:.0f}x{:.0f} fov={:.2f} horizontal={}",
				formID, fx, fy,
				haveCentreScreen ? cx : -1.0f, haveCentreScreen ? cy : -1.0f,
				haveAimScreen ? ax : -1.0f, haveAimScreen ? ay : -1.0f,
				fx - 0.5f, fy - 0.5f,
				io.DisplaySize.x, io.DisplaySize.y,
				settings.cameraFovDegrees, settings.cameraFovIsHorizontal);
		}

		// Draws the box for an already-resolved on-screen position (see
		// ResolveOnScreen). Kept separate so the hint text and the box can
		// share one resolution per frame instead of computing it twice.
		void DrawIfVisible(RE::Actor* a_actor, const char* a_label, ImU32 a_color, bool a_showValue)
		{
			float sx = 0.0f;
			float sy = 0.0f;
			float dist = -1.0f;
			if (!ResolveOnScreen(a_actor, sx, sy, dist)) {
				return;
			}

			// Hit-chance readout removed entirely 2026-08-25 (Alexander's
			// call). The roll system it reported on was removed earlier the
			// same day, leaving it hardcoded to "100%" - a number that
			// looked like a live calculation while conveying nothing. Not
			// worth keeping a placeholder on screen for a mechanic that may
			// or may not come back; distance alone is the useful readout.
			char          value[32] = "--";
			bool          haveHealth = false;
			HealthReading hp{};
			if (dist >= 0.0f) {
				std::snprintf(value, sizeof(value), "%.0f m", dist);

				// Restored 2026-08-25 (see HealthReader.h) - avStorage-based
				// read only, the native-widget probe (HealthWidgetReader)
				// stays deleted: decompile-proven the value it wanted isn't
				// reachable via any GetVariable path, and it froze the PC
				// twice. This path was never disproven, only never
				// confirmed on the visible bar - GetActorHealth's own
				// diagnostic log line settles that on the next test.
				if (Settings::Get().showTargetHealth) {
					haveHealth = GetActorHealth(a_actor, hp);
				}
			}

			// Flash the box red on a hit, or show "MISS" above it, for a
			// short window after AimAssist.cpp's SteeringLoop rolls -
			// Alexander's request: visible feedback for the roll itself,
			// not just the numeric hit-chance readout. Read via Controller
			// since the roll happens on AimAssist's own background thread.
			constexpr auto   kShotResultWindow = std::chrono::milliseconds(900);
			constexpr ImU32  kHitColor = IM_COL32(230, 45, 45, 240);
			const auto       shotResult = Controller::Get().GetLastShotResult();
			const bool       showShotFlash = shotResult.valid &&
			                            (std::chrono::steady_clock::now() - shotResult.time) < kShotResultWindow;

			const auto& io = ImGui::GetIO();
			const float px = sx * io.DisplaySize.x;
			const float py = sy * io.DisplaySize.y;

			// FIXED size, not scaled to the target's projected size
			// (2026-08-26). Fallout 4's and 76's VATS displays are both
			// fixed, and Alexander's observation from watching them is the
			// argument: not only does it never look wrong there, it never
			// even reads as "bigger now, smaller now".
			//
			// This reverses the distance scaling added on 2026-08-25, and
			// it is worth being precise about why that scaling existed:
			// the old fixed box framed a torso at 5m and was wider than
			// the whole actor at 15m, which reads as the box growing while
			// the target shrinks. That complaint was real - but it is a
			// complaint about a FRAME. It only applies while the marker
			// claims to enclose the target, because only then is there
			// something for its size to disagree with.
			//
			// So the marker stops claiming it. At 22px it is a marker on
			// the target rather than a box around it: about 5% of a human's
			// on-screen height at 3m and still comfortably inside the
			// silhouette at 30m. Nothing to mismatch, nothing to notice.
			//
			// Three settings die with the scaling - fTargetBoxScale,
			// fBoxMaxSizeDistanceMeters, fBoxMinSizeDistanceMeters - and
			// with them every complaint from today that was a scaling
			// artefact: "too big up close", "grows before it shrinks",
			// "frozen while the target keeps growing". A constant cannot
			// have any of them.
			const float radius = std::max(4.0f, Settings::Get().targetMarkerRadius);
			DrawTargetMarker(px, py, a_label, a_showValue ? value : nullptr, (showShotFlash && shotResult.hit) ? kHitColor : a_color, radius);

			if (Settings::Get().debugAimMarkers) {
				DrawAimDiagnostics(a_actor);
			}

			auto* dl = ImGui::GetForegroundDrawList();

			if (showShotFlash && !shotResult.hit) {
				DrawCenteredText(dl, px, py - radius - 34.0f, "MISS", kHitColor);
			}

			// Both gauges scale with the ring, so the cluster stays one
			// coherent element rather than a shrinking ring with full-size
			// bars stapled to it.
			//
			// The bHealthBarBelowBox / bResourceBarBelowBox settings are
			// gone with the stacked layout that needed them: the arcs have
			// only one sensible arrangement each (see DrawHealthArc), and
			// a setting that can only hold one value is just a place for a
			// wrong value to hide.
			// Arcs follow the marker's configured size, so raising
			// fTargetMarkerRadius keeps the cluster proportioned.
			const float hudScale = radius / kDefaultRadius;

			if (haveHealth) {
				DrawHealthArc(dl, px, py, radius, hp.current, hp.max, hudScale);
			}

			// This function draws BOTH a combat lock and a support session, so
			// each gauge has to say which one it belongs to. The VATS budget is
			// only ever spent by a lock - it was being drawn around companions
			// too until 2026-08-29, which Alexander spotted from the other end:
			// he asked how the resource arc and the shield arc could ever
			// overlap, since they belong to different modes. They cannot, and
			// the overlap I had worked around by pushing the shield out to its
			// own radius was really this missing check.
			const bool locked = Controller::Get().GetMode() == VATSMode::kLocked;

			const auto& resource = VatsResource::Get().GetState();
			if (locked && Settings::Get().vatsResourceEnabled && resource.capacity > 0.0f) {
				DrawResourceArc(dl, px, py, radius, resource.current, resource.capacity, hudScale);
			}

			// Only on the actor who actually carries it - a companion shield
			// around an enemy ring would put the number on the wrong person.
			// Shares the resource arc radius now that the two are mutually
			// exclusive, which is where it belonged in the first place: same
			// half of the ring, same distance, one per mode.
			const auto shield = CompanionShield::Get().GetState();
			if (!locked && shield.remaining > 0.0f && shield.actor && shield.actor.get() == a_actor) {
				DrawShieldArc(dl, px, py, radius, shield.remaining, shield.capacity, hudScale);
			}
		}
	}

	void Draw()
	{
		// The shield ticks FIRST, before any of the early returns below.
		// Its whole point is that it runs while the player is off fighting,
		// so it must not depend on a support session being open, on a lock
		// being held, or on no menu being up. This is the only per-frame
		// callback the mod has, so it is where the clock lives.
		{
			static auto s_lastTick = std::chrono::steady_clock::now();
			const auto  now = std::chrono::steady_clock::now();
			const float dt = std::chrono::duration<float>(now - s_lastTick).count();
			s_lastTick = now;
			// A loading screen or an alt-tab can leave a gap of many seconds
			// between frames; clamping stops one of those from eating a whole
			// shield in a single step.
			CompanionShield::Get().Tick(std::min(dt, 0.5f));
		}

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
		// memory, loadingmenu.swf). "FavoritesMenu" added 2026-08-25,
		// Alexander's request - same best-effort status as "StarMap": no
		// direct RTTI class under that exact name, inferred from the real,
		// mapped `FavoritesMenu_AssignQuickkey`/`FavoritesMenu_UseQuickkey`
		// event names (Events.h) following this project's other menus'
		// naming convention - unconfirmed in-game, degrades to a no-op if
		// wrong.
		if (auto* ui = RE::UI::GetSingleton()) {
			const bool blockingMenuOpen = ui->menusVisible ||
			                               ui->IsMenuOpen("DataMenu") ||
			                               ui->IsMenuOpen("StarMap") ||
			                               ui->IsMenuOpen("DialogueMenu") ||
			                               ui->IsMenuOpen("LoadingMenu") ||
			                               ui->IsMenuOpen("PauseMenu") ||
			                               ui->IsMenuOpen("FavoritesMenu");
			if (blockingMenuOpen) {
				Controller::Get().ForceOff("blocking menu open");
				return;
			}

			// Message boxes (Starfield's "MessageBoxMenu" - the standard
			// Bethesda-engine popup used for e.g. the Scanner's own Social
			// Skills tutorial tip) are NOT in the blockingMenuOpen list
			// above on purpose - screenshot-confirmed 2026-08-23 that our
			// overlay drew right over one, but ending the whole lock every
			// time a tutorial tip pops up mid-scan would be overkill (it's
			// a brief, transient popup within the same interaction, not a
			// context switch like PauseMenu/DataMenu). Skip drawing only -
			// leave Controller state untouched - so the overlay simply
			// reappears once the box closes.
			if (ui->IsMenuOpen("MessageBoxMenu")) {
				return;
			}
		}

		const auto state = Controller::Get().GetOverlayState();

		// The unconditional "VATS: OFF/LOCKED" corner readout was removed
		// 2026-08-25. It was added as a diagnostic on 2026-08-22 to settle
		// an investigation where the game's own dev console lagged a line
		// behind and was being read as ground truth - it did that job, and
		// the target box plus the resource bar now make the mode obvious
		// without a debug label sitting over the HUD. The log still records
		// every transition if it is ever needed again.

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
		static RE::NiPointer<RE::Actor> s_cachedFriend;

		if (state.mode == VATSMode::kOff) {
			const auto now = std::chrono::steady_clock::now();
			if (now - s_lastScan >= kAimScanInterval) {
				s_lastScan = now;
				s_cachedPick = GetCrosshairActivationTarget();
				// The friendly half of the same pick. Resolved alongside
				// rather than instead of, because the hint below has to tell
				// the player WHICH of the two things the button will do -
				// otherwise pointing at a companion looks identical to
				// pointing at nothing, and the support feature is invisible
				// until someone presses the key on a guess.
				s_cachedFriend = s_cachedPick ? nullptr : GetCrosshairTeammate();
			}
		} else {
			s_cachedPick = nullptr;
			s_cachedFriend = nullptr;
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
			RE::Actor* hintActor = s_cachedPick ? s_cachedPick.get() : s_cachedFriend.get();
			const bool  hintIsFriend = !s_cachedPick && s_cachedFriend;
			if (hintActor && ResolveOnScreen(hintActor, sx, sy, dist)) {
				char keyLabel[8];
				VKToDisplayLabel(Settings::Get().activationKeyVK, keyLabel);
				char hint[32];
				std::snprintf(hint, sizeof(hint), hintIsFriend ? "SUPPORT (%s)" : "TARGETING (%s)", keyLabel);
				const auto& io = ImGui::GetIO();
				auto*       dl = ImGui::GetForegroundDrawList();
				auto*       font = ImGui::GetFont();
				// Native size. The 1.6x was there to make the 13px bitmap
				// font big enough to sit beside the scanner's own "RANGE
				// 100M", by scaling the atlas up - which is what made it
				// blocky. The atlas is now built at 20px instead.
				const float fontSize = ImGui::GetFontSize();
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

		// Diagnostic probe REMOVED 2026-08-24 (was here briefly, see git
		// history) - confirmed its job: the engine overwrites
		// currentCombatTarget continuously, every single frame, not just
		// occasionally. That meant the probe's "log only on change" throttle
		// never actually throttled anything once CombatTargetOverride::
		// Refresh() started fighting it (see that file) - it was logging
		// essentially every frame for the whole Locked duration, real log-
		// spam/I-O overhead this project doesn't normally accept (every
		// other per-frame diagnostic here is throttled to ~10Hz or on a
		// genuine low-frequency change). Suspected contributor to a real
		// gameplay regression Alexander reported the same test session
		// (far fewer shots getting redirected than in earlier builds) -
		// removed together with disabling Refresh() below rather than left
		// in "just in case."

		if (state.mode == VATSMode::kOff) {
			// The bar refills only while VATS is off - Alexander's design,
			// so hopping straight into a new lock never comes free.
			VatsResource::Get().TickIdle();

			// No "off" log line here. LogIfChanged dedupes on
			// (outcome, formID), and this one fired every frame with
			// formID=0 while the pre-lock crosshair scan was logging its
			// own outcome for a real formID in the same frame - each call
			// reset the other's "last logged" state, so both looked like a
			// change every time. That produced hundreds of identical lines
			// per second and buried everything else in the log. The line
			// carried no information anyway: Controller already logs every
			// LOCKED and OFF transition.
			return;
		}

		if (state.mode == VATSMode::kSupport) {
			// Support sessions share the marker and the health readout with a
			// combat lock, and nothing else. Everything below this branch is
			// wrong for a companion: the VATS bar must not be billed for
			// helping someone, the projectile override must not touch the
			// weapon, and the death check would end the session by hopping to
			// the "next target", which is a combat idea.
			//
			// The bar still refills, same as when off - a support session is
			// not a VATS lock and should not cost the player one.
			VatsResource::Get().TickIdle();

			if (!state.actor) {
				Controller::Get().ForceOff("support target gone");
				return;
			}

			DrawIfVisible(state.actor.get(), "SUPPORT", kSupportColor, /*a_showValue*/ true);

			// The action, spelled out under the marker. Added 2026-08-28
			// because the first in-game test stalled here: the session drew
			// correctly, the companion was hurt, and Alexander asked "do I
			// have to do something? I see no input" - and he was right, there
			// was nothing on screen saying what the button now does. The log
			// confirmed it: not one press arrived after the session opened.
			//
			// A mode whose action is invisible is worse than no mode. Once
			// there is more than one action this line becomes the selected
			// entry of the menu, so it is also the right place structurally.
			float px = 0.0f, py = 0.0f, pdist = 0.0f;
			if (ResolveOnScreen(state.actor.get(), px, py, pdist)) {
				char keyLabel[8];
				VKToDisplayLabel(Settings::Get().activationKeyVK, keyLabel);

				// A downed companion reads zero health - that is what put them
				// down. Naming the action for what it will actually look like
				// costs one comparison and removes the moment of doubt about
				// whether healing a body on the floor does anything at all.
				HealthReading hp{};
				const bool    haveHp = GetActorHealth(state.actor.get(), hp);
				const bool    down = haveHp && hp.current <= 0.0f;
				char prompt[32];
				const bool full = hp.max > 0.0f && hp.current >= hp.max - 0.5f;
				std::snprintf(prompt, sizeof(prompt),
					down ? "REVIVE (%s)" : (full ? "SHIELD (%s)" : "HEAL (%s)"), keyLabel);

				// Second line, because a key that does two things has to say so.
				// Without it "hold to leave" is a piece of knowledge that exists
				// only in the INI, and the session would look like it has no way
				// out - which is exactly the hole the first test fell into.
				char exitHint[40];
				std::snprintf(exitHint, sizeof(exitHint), "HOLD %s TO EXIT", keyLabel);

				const auto& io = ImGui::GetIO();
				DrawCenteredText(ImGui::GetForegroundDrawList(),
					px * io.DisplaySize.x,
					py * io.DisplaySize.y + Settings::Get().targetMarkerRadius + 14.0f,
					prompt, kSupportColor);
			}
			return;
		}

		// Locked

		if (!state.actor) {
			LogIfChanged(DrawOutcome::kNoTarget, 0, "locked but no target (unexpected)");
			return;
		}

		// VATS resource: spend budget for the damage dealt to the locked
		// target, and end the lock when it runs dry - same handling as a
		// dead target, and the bar then refills while Off. Alexander's
		// design, see VatsResource.h.
		if (!VatsResource::Get().TickLocked(state.actor.get())) {
			Controller::Get().ForceOff("VATS budget exhausted");
			return;
		}

		// End the lock outright once the target dies, rather than leaving
		// VATS Locked on a corpse until the player notices and presses the
		// hotkey themselves — Alexander's call, 2026-08-22.
		//
		// Death is detected from health, not from Actor::boolBits. The bit
		// check that lived here from 2026-08-22 until 2026-08-25 never once
		// fired: a locked target's boolBits reads the same 0x12A021A2
		// whether it is alive or lying dead on the floor, and a raw dump of
		// the surrounding memory showed the struct layout is fine, so it is
		// CommonLibSF's BOOL_BITS enum values that don't match this game
		// (kSetOnDeath, 1<<23, is set on living actors too). Rather than
		// keep hunting for the right bit, use the health value that now
		// actually works - confirmed in-game reading 87.27 -> 16.17 -> 1.12
		// -> -13.93 across a real kill, going negative on overkill damage.
		{
			HealthReading hp{};
			if (GetActorHealth(state.actor.get(), hp) && hp.current <= 0.0f) {
				// With budget left, hop to the next enemy rather than
				// dropping out of VATS entirely - Alexander's request. The
				// resource system is what makes this fair: each hop still
				// has to be paid for in damage dealt, so a chain of kills
				// ends on its own once the bar runs out.
				//
				// TryAdvanceOrHold, not a single attempt: at the instant of
				// death the player is still aiming at the body that just
				// dropped, so an immediate crosshair-based pick can only
				// fail. It holds the lock open briefly instead, giving them
				// time to swing onto the next target. Draws nothing while
				// waiting - a box on a corpse would be worse than none.
				if (VatsResource::Get().GetState().current > 0.0f && Controller::Get().TryAdvanceOrHold()) {
					return;
				}

				VATS_LOG("[VATS] target died (health={:.2f}), forcing lock off", hp.current);
				Controller::Get().ForceOff("target died");
				return;
			}
		}

		// CombatTargetOverride's Engage()/Disengage() (VATSController.cpp's
		// Lock/Unlock) now run their own dedicated background thread - see
		// CombatTargetOverride.h for the two earlier per-render-frame
		// attempts that didn't work and why. Nothing to call from here
		// anymore.

		DrawIfVisible(state.actor.get(), "TARGET", kLockedColor, /*a_showValue*/ true);

		// Same exit hint the support session carries. A combat lock used to
		// end on a tap, so it never needed one; now that it ends on a hold,
		// leaving is a gesture nothing on screen would otherwise mention.
		{
			float lx = 0.0f, ly = 0.0f, ldist = 0.0f;
			if (ResolveOnScreen(state.actor.get(), lx, ly, ldist)) {
				char keyLabel[8];
				VKToDisplayLabel(Settings::Get().activationKeyVK, keyLabel);
				char exitHint[40];
				std::snprintf(exitHint, sizeof(exitHint), "HOLD %s TO EXIT", keyLabel);
				const auto& io = ImGui::GetIO();
				DrawCenteredText(ImGui::GetForegroundDrawList(),
					lx * io.DisplaySize.x,
					ly * io.DisplaySize.y + Settings::Get().targetMarkerRadius + 14.0f,
					exitHint, kAimColor);
			}
		}

		// Re-check the equipped weapon every frame while Locked
		// (2026-08-25) - see Controller::SyncProjectileOverride's comment.
		// A weapon switch mid-lock used to leave the NEW weapon un-flipped
		// (still hitscan) until the player toggled VATS off and back on;
		// confirmed from a real session's log. No-op unless the equipped
		// weapon actually changed since the last check.
		Controller::Get().SyncProjectileOverride();

		// worldBound telemetry, verbose only - it re-reads the bounding
		// sphere the draw path already resolved, purely to log it.
		if (Log::Verbose()) {
			WorldBoundProbe::LogIfChanged(state.actor.get());
		}

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
