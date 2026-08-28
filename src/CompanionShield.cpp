#include "CompanionShield.h"

#include "ActorValueProbe.h"
#include "Settings.h"

#include <algorithm>

#include "RE/A/Actor.h"
#include "RE/A/ActorValues.h"

namespace VATS
{
	CompanionShield& CompanionShield::Get()
	{
		static CompanionShield instance;
		return instance;
	}

	void CompanionShield::ApplyResistance(RE::Actor* a_actor, float a_delta)
	{
		if (!a_actor || a_delta == 0.0f) {
			return;
		}
		auto* avList = RE::ActorValue::GetSingleton();
		if (!avList) {
			return;
		}

		// All three of Starfield's resistance types together, deliberately.
		// Honouring each item's own type would need a way to choose between
		// items, and the difference between ballistic and energy is rarely
		// the decision a player wants to make mid-fight. A flat shield is
		// legible; per-type would be bookkeeping.
		const RE::ActorValueInfo* types[]{
			avList->damageResist,
			avList->energyResist,
			avList->electromagneticDamageResist,
		};
		for (const auto* av : types) {
			if (av) {
				(void)TryModTemporary(a_actor, *av, a_delta);
			}
		}
	}

	void CompanionShield::Add(RE::Actor* a_actor, float a_seconds)
	{
		if (!a_actor || !(a_seconds > 0.0f)) {
			return;
		}
		const auto&            settings = Settings::Get();
		const std::scoped_lock lock(m_lock);

		// A different companion means the old one loses the bonus first.
		// Leaving it on would hand out a resistance nothing tracks and
		// nothing ever takes off again.
		if (m_applied && m_actor && m_actor.get() != a_actor) {
			ApplyResistance(m_actor.get(), -settings.shieldDamageResist);
			m_applied = false;
			m_remaining = 0.0f;
		}

		m_actor = RE::NiPointer<RE::Actor>(a_actor);
		m_remaining = std::min(m_remaining + a_seconds, settings.shieldMaxSeconds);

		if (!m_applied) {
			ApplyResistance(a_actor, settings.shieldDamageResist);
			m_applied = true;
		}
	}

	void CompanionShield::Tick(float a_deltaSeconds)
	{
		if (!(a_deltaSeconds > 0.0f)) {
			return;
		}
		const std::scoped_lock lock(m_lock);
		if (!m_applied) {
			return;
		}

		m_remaining -= a_deltaSeconds;
		if (m_remaining > 0.0f) {
			return;
		}

		// Expired. Take the bonus back off through the same verified path
		// it went on by - if the actor is gone the call degrades to doing
		// nothing, which is the right outcome anyway.
		ApplyResistance(m_actor.get(), -Settings::Get().shieldDamageResist);
		m_applied = false;
		m_remaining = 0.0f;
		m_actor.reset();
		VATS_LOG("[shield] expired");
	}

	CompanionShield::State CompanionShield::GetState() const
	{
		const std::scoped_lock lock(m_lock);
		return State{ m_actor, m_remaining, Settings::Get().shieldMaxSeconds };
	}
}
