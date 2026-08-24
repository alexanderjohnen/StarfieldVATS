#pragma once

namespace VATS
{
	// Second attempt at live target health, after three avStorage-based
	// hypotheses were tried and disproven (see HealthReader.h/HANDOFF.md -
	// reading current health straight from Actor::avStorage never tracked
	// real damage; HealthReader.cpp still contains that known-broken
	// fallback). This instead reads whatever native combat-HUD widget the
	// game itself already updates when actor health changes, via
	// RE::UI::GetMenuMovie("HUDMenu")->asMovieRoot->GetVariable - the same
	// zero-new-REL::ID mechanism CrosshairVisibility/CombatHudVisibility
	// already use - instead of guessing at avStorage's internal layout
	// again.
	//
	// UNCONFIRMED as of 2026-08-24: the exact AS variable path holding the
	// live percentage isn't known, only clip *names* found via a strings
	// dump of hudmenu.gfx (see HANDOFF.md) - "EnemyHealthMeter_mc" nested
	// somewhere under "EnemyHealthHolder_mc". Probe() (in the .cpp) tries a
	// matrix of plausible parent paths x property-name suffixes, logs every
	// combination that resolves (path, type, value) so an in-game test can
	// pin down the true one, and best-effort auto-selects the first
	// numeric-typed hit as a working guess. Treat GetLiveHealthPercent()'s
	// result as unconfirmed until a real test shows the bar moving on a
	// real hit - if the guessed candidate is wrong, the probe's log output
	// is what tells us what's actually there for the next attempt.
	//
	// Separately unconfirmed: whether this widget even tracks the actor
	// VATS has Locked, as opposed to whatever the vanilla combat/AI system
	// considers the player's "current" target - nothing in this project
	// ever sets that, so if they differ, this widget may reflect a
	// different actor's health entirely, or none.
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
