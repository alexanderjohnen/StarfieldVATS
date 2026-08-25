#include "VatsResource.h"

#include "HealthReader.h"
#include "Settings.h"

#include <algorithm>

namespace VATS
{
	VatsResource& VatsResource::Get()
	{
		static VatsResource instance;
		return instance;
	}

	void VatsResource::RefreshFromPlayer()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* avList = RE::ActorValue::GetSingleton();
		if (!player || !avList || !avList->health || !avList->oxygen) {
			m_valid = false;
			return;
		}

		float fullHealth = 0.0f;
		float fullOxygen = 0.0f;
		const bool haveHealth = GetActorBaseValue(player, avList->health, fullHealth) && fullHealth > 0.0f;
		const bool haveOxygen = GetActorBaseValue(player, avList->oxygen, fullOxygen) && fullOxygen > 0.0f;
		if (!haveHealth) {
			// Without a capacity there is no bar to draw and nothing to
			// spend; the caller degrades to "resource system off" rather
			// than to a bar that is silently always empty.
			m_valid = false;
			REX::WARN("[VATS] resource: could not read the player's full health - resource bar disabled this session");
			return;
		}

		const auto& settings = Settings::Get();

		const float newCapacity = fullHealth * settings.vatsCapacityPerHealth;
		// Oxygen only sets the refill RATE. If it can't be read, fall back
		// to refilling the whole bar over a fixed span rather than never
		// refilling at all - a stuck-empty bar would be a far worse failure
		// than a slightly wrong rate.
		m_refillPerSecond = haveOxygen ? fullOxygen * settings.vatsRefillPerOxygen
		                               : newCapacity / 20.0f;

		// Preserve how full the bar was across a capacity change, so a perk
		// or gear swap that raises max health doesn't hand the player a
		// free instant refill (or, on a decrease, a bar reading over 100%).
		const float fraction = m_capacity > 0.0f ? std::clamp(m_current / m_capacity, 0.0f, 1.0f) : 1.0f;
		m_capacity = newCapacity;
		m_current = m_capacity * fraction;
		m_valid = true;

		REX::INFO("[VATS] resource: capacity={:.0f} (full health {:.0f} x {:.2f}), refill={:.1f}/s (full oxygen {:.0f} x {:.2f}), current={:.0f}",
			m_capacity, fullHealth, settings.vatsCapacityPerHealth, m_refillPerSecond, fullOxygen, settings.vatsRefillPerOxygen, m_current);
	}

	void VatsResource::OnLockStart()
	{
		RefreshFromPlayer();
		m_haveLastTargetHealth = false;
		m_lastTargetFormID = 0;
	}

	bool VatsResource::TickLocked(RE::Actor* a_target)
	{
		if (!Settings::Get().vatsResourceEnabled) {
			return true;
		}
		if (!m_valid) {
			// Never read the player's stats successfully - don't let a
			// diagnostic failure silently start cancelling locks.
			return true;
		}

		m_haveLastTick = false;  // idle refill restarts its clock after any lock

		HealthReading hp{};
		if (a_target && GetActorHealth(a_target, hp)) {
			const std::uint32_t formID = a_target->GetFormID();
			if (m_haveLastTargetHealth && formID == m_lastTargetFormID) {
				const float damage = m_lastTargetHealth - hp.current;
				if (damage > 0.0f) {
					m_current = std::max(0.0f, m_current - damage * Settings::Get().vatsCostPerDamage);
				}
			}
			m_lastTargetHealth = hp.current;
			m_lastTargetFormID = formID;
			m_haveLastTargetHealth = true;
		}

		if (m_current <= 0.0f) {
			REX::INFO("[VATS] resource: budget exhausted (capacity={:.0f}) - ending lock", m_capacity);
			return false;
		}
		return true;
	}

	void VatsResource::TickIdle()
	{
		if (!Settings::Get().vatsResourceEnabled || !m_valid) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		if (!m_haveLastTick) {
			m_lastTick = now;
			m_haveLastTick = true;
			return;
		}

		const float seconds = std::chrono::duration<float>(now - m_lastTick).count();
		m_lastTick = now;
		if (m_current >= m_capacity || seconds <= 0.0f) {
			return;
		}

		// Guard against a huge delta after a loading screen or a paused
		// game handing the player a full bar for free... which, on
		// reflection, is exactly what would happen anyway once enough real
		// time passed, so cap it at a plausible frame gap instead of
		// discarding it.
		m_current = std::min(m_capacity, m_current + m_refillPerSecond * std::min(seconds, 1.0f));
	}

	VatsResource::State VatsResource::GetState() const
	{
		return State{ m_current, m_capacity, m_valid && Settings::Get().vatsResourceEnabled };
	}
}
