#pragma once

namespace VATS
{
	// DISABLED 2026-08-24 - see the .cpp for the full story. Second attempt
	// at live target health, after three avStorage-based hypotheses were
	// tried and disproven (see HealthReader.h/HANDOFF.md - reading current
	// health straight from Actor::avStorage never tracked real damage;
	// HealthReader.cpp still contains that known-broken fallback, which is
	// what Overlay.cpp now falls back to unconditionally). This was meant
	// to read whatever native combat-HUD widget the game itself already
	// updates, by probing guessed AS variable paths under HUDMenu's
	// Scaleform tree - but two different attempts at that (a bare clip
	// reference, then named property paths) each hard-broke the game in a
	// different way (an instant silent crash, then a full system freeze),
	// both traced to Scaleform's own object-lifecycle machinery
	// (Value::ObjectInterface::*) never having been safely exercised by
	// this project before. GetLiveHealthPercent() is now a permanent no-op
	// (always returns false) until a fundamentally different, safer
	// approach exists - see the .cpp before touching this again.
	class HealthWidgetReader
	{
	public:
		// Returns true and fills a_outPercent (0..100) if a plausible live
		// health widget value was found. False if the probe found nothing
		// usable, or the HUD movie isn't available right now - caller
		// should fall back to HealthReader::GetActorHealth in that case.
		[[nodiscard]] static bool GetLiveHealthPercent(float& a_outPercent);
	};
}
