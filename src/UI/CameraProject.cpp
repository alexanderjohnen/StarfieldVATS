#include "CameraProject.h"

#include "D3DHook.h"
#include "Settings.h"

namespace VATS::UI
{
	namespace
	{
		constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

		[[nodiscard]] bool Normalize(RE::NiPoint3& a_v)
		{
			const float len = std::sqrt(a_v.x * a_v.x + a_v.y * a_v.y + a_v.z * a_v.z);
			if (len < 1.0e-4f) {
				return false;
			}
			a_v.x /= len;
			a_v.y /= len;
			a_v.z /= len;
			return true;
		}

		[[nodiscard]] float Dot(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
		{
			return a_a.x * a_b.x + a_a.y * a_b.y + a_a.z * a_b.z;
		}

		// Everything the projection needs, resolved once. Split out of
		// WorldToScreen when ProjectedRadiusPixels became a second caller:
		// both need the same camera basis, display size and FOV tangents,
		// and having two copies of that resolution drift apart is exactly
		// how the box size ended up disagreeing with the box position.
		struct CameraFrame
		{
			RE::NiPoint3 pos;
			RE::NiPoint3 fwd;
			RE::NiPoint3 right;
			RE::NiPoint3 up;
			float        displayW;
			float        displayH;
			float        tanHalfFovX;
			float        tanHalfFovY;
		};

		[[nodiscard]] bool GetCameraFrame(CameraFrame& a_out)
		{
			auto* playerCamera = RE::PlayerCamera::GetSingleton();
			if (!playerCamera) {
				return false;
			}
			auto* cameraRoot = playerCamera->cameraRoot.get();
			if (!cameraRoot) {
				return false;
			}

			const auto& world = cameraRoot->world;
			a_out.pos = world.translate;

			// Rows of the camera's own rotation matrix — an orthonormal basis.
			// Row 1 = forward, row 0 = lateral: both empirically proven during
			// the targeting work (see Targeting.h history). Row 2 = up, by
			// elimination (the only row left in an orthonormal 3x3) — not yet
			// independently proven, but if the sign or forward/right/up
			// assignment is off, the visible symptom is a mirrored or
			// upside-down box, trivially correctable, not a silent failure.
			a_out.fwd = RE::NiPoint3{ world.rotate.entry[1].x, world.rotate.entry[1].y, world.rotate.entry[1].z };
			a_out.right = RE::NiPoint3{ world.rotate.entry[0].x, world.rotate.entry[0].y, world.rotate.entry[0].z };
			a_out.up = RE::NiPoint3{ world.rotate.entry[2].x, world.rotate.entry[2].y, world.rotate.entry[2].z };
			if (!Normalize(a_out.fwd) || !Normalize(a_out.right) || !Normalize(a_out.up)) {
				return false;
			}

			// Deliberately NOT ImGui's io.DisplaySize: this is called from
			// non-render threads (AimAssist's steering loop), and ImGui's
			// context doesn't even exist until the Present hook has run once.
			// Crashed 2026-08-22 (Crashlog 18-14-27) in a session where the
			// Present hook failed to install and GetIO() hit a null context.
			// GetDisplaySize is plain atomics, valid from any thread, and
			// false simply means the swapchain doesn't exist yet.
			a_out.displayW = 0.0f;
			a_out.displayH = 0.0f;
			if (!GetDisplaySize(a_out.displayW, a_out.displayH)) {
				return false;
			}
			const float aspect = a_out.displayW / a_out.displayH;

			// Which axis the configured FOV describes is a setting, not an
			// assumption. It was assumed horizontal (Bethesda convention) from
			// the day this was written and never checked, and the failure mode
			// of getting it wrong is the exact symptom this project has been
			// chasing: the error is zero at the screen centre and grows toward
			// the edges, so a target being looked straight at looks fine while
			// one off to the side does not.
			//
			// Note also which game setting to match: "fFPWorldFOV" in
			// StarfieldPrefs.ini, NOT "fFPGeometryFOV" (the first-person
			// viewmodel FOV), which is what an earlier comment here named. On
			// Alexander's machine those read 89.6 and 90 — the in-game slider
			// writes the odd value, which is exactly why this reads as a float.
			const float fovRad = Settings::Get().cameraFovDegrees * kDegToRad;
			const float tanHalfFov = std::tan(fovRad * 0.5f);
			if (Settings::Get().cameraFovIsHorizontal) {
				a_out.tanHalfFovX = tanHalfFov;
				a_out.tanHalfFovY = tanHalfFov / aspect;
			} else {
				a_out.tanHalfFovY = tanHalfFov;
				a_out.tanHalfFovX = tanHalfFov * aspect;
			}
			return a_out.tanHalfFovX > 0.0f && a_out.tanHalfFovY > 0.0f;
		}
	}

	bool WorldToScreen(const RE::NiPoint3& a_worldPos, float& a_outX, float& a_outY)
	{
		CameraFrame frame{};
		if (!GetCameraFrame(frame)) {
			return false;
		}

		const RE::NiPoint3 diff{ a_worldPos.x - frame.pos.x, a_worldPos.y - frame.pos.y, a_worldPos.z - frame.pos.z };
		const float        fwdComp = Dot(diff, frame.fwd);
		if (fwdComp <= 1.0f) {
			return false;  // behind camera or degenerately close
		}

		const float ndcX = (Dot(diff, frame.right) / fwdComp) / frame.tanHalfFovX;
		const float ndcY = (Dot(diff, frame.up) / fwdComp) / frame.tanHalfFovY;

		a_outX = (ndcX + 1.0f) * 0.5f;
		a_outY = (1.0f - ndcY) * 0.5f;
		return true;
	}

	bool ProjectedRadiusPixels(const RE::NiPoint3& a_worldPos, float a_worldRadius, float& a_outPixels, float* a_outDepth)
	{
		if (!(a_worldRadius > 0.0f)) {
			return false;
		}

		CameraFrame frame{};
		if (!GetCameraFrame(frame)) {
			return false;
		}

		const RE::NiPoint3 diff{ a_worldPos.x - frame.pos.x, a_worldPos.y - frame.pos.y, a_worldPos.z - frame.pos.z };
		const float        fwdComp = Dot(diff, frame.fwd);
		if (fwdComp <= 1.0f) {
			return false;
		}

		// Depth along the view axis, not straight-line distance: that is
		// the quantity a perspective divide actually uses, so this is
		// strictly 1/depth and nothing else. Backing away from a target
		// can only ever make it smaller.
		a_outPixels = (a_worldRadius / fwdComp) / frame.tanHalfFovY * (frame.displayH * 0.5f);
		if (a_outDepth) {
			*a_outDepth = fwdComp;
		}
		return true;
	}
}
