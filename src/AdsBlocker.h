#pragma once

namespace VATS
{
	// Ends an active VATS lock the instant the player manually starts
	// aiming down sights, instead of continuing to fight the engine over
	// *blocking* ADS itself. Four separate blocking approaches were tried
	// and abandoned (see git history / HANDOFF.md for the full trail):
	// an OS-level input swallow (detected the press fine, had zero effect
	// on the engine's own reaction), RE::PlayerCamera camera-state polling
	// (kIronSights never actually triggered during a real ADS),
	// RE::PlayerControls::PlayerIronSightsStartEvent registration (crashed
	// outright - "REL/IDDB.cpp(417): Failed to find offset for Address
	// Library ID! Invalid ID: 0", one of GetEventSource/RegisterSink isn't
	// mapped for 1.16.244.0), and USER_EVENT_FLAG::Fighting (blocked ADS by
	// holstering the weapon entirely - way too broad). Alexander's own
	// suggestion 2026-08-24: stop trying to block ADS and just drop the
	// lock instead - much simpler, and sidesteps every failure mode above.
	//
	// Detection reuses the one piece of the old approach that WAS proven to
	// work: a WH_MOUSE_LL low-level mouse hook reliably sees the real
	// button-down (the OS-swallow attempt's log confirmed the hook fired
	// correctly every time - the only failure was that swallowing the
	// input had no effect on Starfield's own reaction to it). Reacting
	// with Controller::ForceOff() instead of trying to swallow/redirect the
	// input sidesteps that whole class of problem: ForceOff() only ever
	// touches this mod's own atomics/mutex-guarded state (no engine calls
	// beyond the same CrosshairVisibility/CombatHudVisibility restore every
	// other unlock path already does), and is documented safe to call from
	// any thread - BackKeyInterceptor.cpp already calls it the same way,
	// from its own background hook thread.
	//
	// Only mouse buttons are recognized (Settings::adsButtonVK -
	// VK_RBUTTON/VK_MBUTTON/VK_XBUTTON1/VK_XBUTTON2), same limitation the
	// old adsReleaseKeyVK setting already had - must match Alexander's
	// actual ADS keybind by hand, no way to read it from the game's own
	// settings.
	class AdsBlocker
	{
	public:
		static void Start();
		static void Stop();

	private:
		static void ThreadProc(const std::stop_token& a_stop);

		static inline std::jthread m_thread;
	};
}
