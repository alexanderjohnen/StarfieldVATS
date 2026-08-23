#pragma once

namespace VATS
{
	// Forces the player out of aim-down-sights while VATS is Locked -
	// reacts to RE::PlayerControls::PlayerIronSightsStartEvent, Bethesda's
	// own native "the player just started ADS" event (RE/E/Events.h),
	// rather than polling camera state (RE::CameraState::kIronSights never
	// actually triggered during a real ADS - confirmed empirically
	// 2026-08-23, an earlier version of this file polled for it) or
	// swallowing the button at the OS input level (confirmed not to stop
	// the engine's own reaction either, same class of problem
	// EngineInputLayer.h documents for the Tab-key interceptor). On the
	// event, both forces the camera back to first-person AND sends a
	// synthetic release of Settings::adsReleaseKeyVK (the same SendInput
	// technique VATSController.cpp's scanner-close logic already uses
	// successfully - a synthetic release goes through the normal OS input
	// pipeline and is indistinguishable from a real one, unlike suppressing
	// an event, which prior attempts found doesn't reliably stop
	// Starfield's own reaction).
	//
	// This is the first BSTEventSource<T>::RegisterSink call in this
	// project since RE::TESHitEvent's crashed on an unmapped Address
	// Library ID (see HitEventLogger.h, currently disabled for exactly that
	// reason) - a real "the game might not even launch" risk, not the
	// "safe on a miss" guarantee every other ADS experiment had. Tried
	// anyway (Alexander's call, 2026-08-23): PlayerIronSightsStartEvent/
	// EndEvent are far more commonly used by other aim-related mods than
	// the more exotic hit-event system, so more likely to be mapped - but
	// not guaranteed. If the game fails to launch or crashes immediately
	// after this deploys, comment out the Start() call in main.cpp the same
	// way HitEventLogger::Start() already is.
	//
	// Known gap: only fires for actual iron-sights weapons. Alexander
	// pointed out the Cutter uses a "focus mode" instead when its ADS
	// button is held (different crosshair, stronger beam, no real
	// zoom/iron-sights) - unclear whether this event fires for that case
	// too or is specific to real ADS; needs in-game confirmation with a
	// Cutter equipped.
	class AdsBlocker
	{
	public:
		static void Start();
	};
}
