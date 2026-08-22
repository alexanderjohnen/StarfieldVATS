#pragma once

namespace VATS
{
	// The core VATS mechanic: while Locked and the fire button (left
	// mouse, hardcoded for now) is held, repeatedly asks ProjectileTracker
	// to find and redirect the player's own freshly-fired round(s)
	// in-flight, toward (hit) or away from (miss) the locked target, for
	// as long as the button stays down. The camera/weapon itself is never
	// touched - Starfield's own fire-rate timer still decides when shots
	// actually happen (single or automatic), and vanilla ballistics/
	// damage/hit-reactions/death still run on the (redirected) round, no
	// engine calls of our own for any of that. Replaces an earlier
	// SendMouseMove-based camera-steering design (removed 2026-08-22 per
	// Alexander - the weapon/camera should never visibly snap onto the
	// target, only the round itself should curve mid-flight). Hit/miss is
	// still rolled once per button-hold, not once per bullet (no reliable
	// way yet to learn a weapon's fire rate to sync re-rolls to individual
	// shots) - every shot in one held-trigger burst currently shares the
	// same outcome. See starfield-vats-mod-design memory, 2026-08-22, for
	// why this general shape (real vanilla shot, just steered) was chosen
	// over directly writing damage into ActorValueStorage (would need an
	// unmapped/risky engine function and bypasses the whole hit-reaction/
	// death pipeline) and over an original single-click-intercept-and-
	// resynthesize design (silently only ever assisted the first shot of
	// an automatic burst).
	//
	// Deliberately does NOT check wall/geometry occlusion - only whether
	// the target is currently in the camera's field of view at all (reuses
	// UI::WorldToScreen, same as the HUD box). True occlusion needs a
	// raycast CommonLibSF doesn't expose; known v1 limitation, same
	// pattern this project has followed before (targeting itself shipped
	// without LOS first, gained it later via a different mechanism).
	class AimAssist
	{
	public:
		static void Start();
		static void Stop();

	private:
		static void ThreadProc(const std::stop_token& a_stop);

		static inline std::jthread m_thread;
	};
}
