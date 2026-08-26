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
}
