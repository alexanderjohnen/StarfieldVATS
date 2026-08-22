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
	}

	bool WorldToScreen(const RE::NiPoint3& a_worldPos, float& a_outX, float& a_outY)
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
		const RE::NiPoint3 camPos = world.translate;

		// Rows of the camera's own rotation matrix — an orthonormal basis.
		// Row 1 = forward, row 0 = lateral: both empirically proven during
		// the targeting work (see Targeting.h history). Row 2 = up, by
		// elimination (the only row left in an orthonormal 3x3) — not yet
		// independently proven, but if the sign or forward/right/up
		// assignment is off, the visible symptom is a mirrored or
		// upside-down box, trivially correctable, not a silent failure.
		RE::NiPoint3 fwd{ world.rotate.entry[1].x, world.rotate.entry[1].y, world.rotate.entry[1].z };
		RE::NiPoint3 right{ world.rotate.entry[0].x, world.rotate.entry[0].y, world.rotate.entry[0].z };
		RE::NiPoint3 up{ world.rotate.entry[2].x, world.rotate.entry[2].y, world.rotate.entry[2].z };
		if (!Normalize(fwd) || !Normalize(right) || !Normalize(up)) {
			return false;
		}

		const RE::NiPoint3 diff{ a_worldPos.x - camPos.x, a_worldPos.y - camPos.y, a_worldPos.z - camPos.z };
		const float        fwdComp = Dot(diff, fwd);
		if (fwdComp <= 1.0f) {
			return false;  // behind camera or degenerately close
		}

		const float rightComp = Dot(diff, right);
		const float upComp = Dot(diff, up);

		// Deliberately NOT ImGui's io.DisplaySize: this function is called
		// from non-render threads (AimAssist's steering loop), and ImGui's
		// context doesn't even exist until the Present hook has run once.
		// Crashed 2026-08-22 (Crashlog 18-14-27) in a session where the
		// Present hook failed to install and GetIO() hit a null context.
		// GetDisplaySize is plain atomics, valid from any thread, and
		// false simply means the swapchain doesn't exist yet.
		float displayW = 0.0f;
		float displayH = 0.0f;
		if (!GetDisplaySize(displayW, displayH)) {
			return false;
		}
		const float aspect = displayW / displayH;

		// Bethesda convention: the configured FOV is horizontal (matches
		// Starfield's own "fFPGeometryFOV:Camera" setting).
		const float fovXRad = static_cast<float>(Settings::Get().cameraFovDegrees) * kDegToRad;
		const float tanHalfFovX = std::tan(fovXRad * 0.5f);
		const float tanHalfFovY = tanHalfFovX / aspect;

		const float ndcX = (rightComp / fwdComp) / tanHalfFovX;
		const float ndcY = (upComp / fwdComp) / tanHalfFovY;

		a_outX = (ndcX + 1.0f) * 0.5f;
		a_outY = (1.0f - ndcY) * 0.5f;
		return true;
	}
}
