#pragma once

namespace VATS::UI
{
	// Projects a world position to normalized screen coordinates
	// (0..1, origin top-left). Returns false if the point is behind the
	// camera.
	//
	// Implementation history: three separate strategies to locate and read
	// the engine's own NiCamera/view-projection matrix in memory all
	// failed (scene-graph children walk, embedded-pointer scan of
	// PlayerCamera and cameraRoot, both narrow and 8KB-wide) — not from
	// bad validation (cameraRoot's own vtable was independently confirmed
	// to exactly match the resolved NiNode address, proving the
	// identification method itself is sound), but because the render
	// camera simply isn't reachable from PlayerCamera/cameraRoot within
	// any practically-searchable byte range. See starfield-vats-ui-hook
	// project memory for the full trail.
	//
	// Current approach instead SELF-COMPUTES a standard perspective
	// projection from data that has been reliable since the very first
	// targeting test: cameraRoot->world's position/forward/lateral
	// vectors (rotation rows 1/0, empirically proven — see Targeting.h),
	// plus the "up" row (row 2, by elimination — orthonormal with the
	// other two), plus a configurable FOV (Settings::cameraFovDegrees).
	// No memory searching, no engine object identity to get wrong — pure
	// vector math against values already trusted. Won't be pixel-perfect
	// if Starfield's actual projection deviates from a plain pinhole
	// camera (unlikely to matter for HUD box placement), but it will not
	// silently fail to find anything the way the NiCamera hunt did.
	[[nodiscard]] bool WorldToScreen(const RE::NiPoint3& a_worldPos, float& a_outX, float& a_outY);

	// How many pixels one world-space radius at a_worldPos covers on
	// screen. Used to size the target box.
	//
	// This replaced projecting a second point one radius above the aim
	// point and measuring the pixel gap between the two. That method
	// inherited the aspect handling for free, which was the reason for it,
	// but it also inherited something unwanted: the gap between two
	// projected points depends on WHERE on screen they sit and on the
	// camera's pitch, not on distance alone. Alexander, 2026-08-26:
	// backing away from a target made the box grow first and only then
	// shrink. Expanding that gap for small radii gives
	//
	//   gap ~ (r / depth) * (cos(pitch) + (h / depth) * sin(pitch))
	//
	// where h is the aim point's offset from the view axis - a correction
	// term that is strongest at close range and vanishes with distance,
	// i.e. exactly a size that dips near and recovers as you back off.
	//
	// This computes the angular size directly instead: strictly 1/depth,
	// with no dependence on screen position or pitch. Closest is largest,
	// monotonically, the way the NPC itself behaves.
	[[nodiscard]] bool ProjectedRadiusPixels(const RE::NiPoint3& a_worldPos, float a_worldRadius, float& a_outPixels);

}
