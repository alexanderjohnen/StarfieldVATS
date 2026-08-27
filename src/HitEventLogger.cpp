#include "HitEventLogger.h"

#include "VATSController.h"

namespace VATS
{
	namespace
	{
		class Sink : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent& a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				const std::uint32_t targetFormID = a_event.target ? a_event.target->GetFormID() : 0;
				const std::uint32_t causeFormID = a_event.cause ? a_event.cause->GetFormID() : 0;

				const auto lockedActor = Controller::Get().GetOverlayState().actor;
				const bool isLockedTarget = lockedActor && targetFormID == lockedActor->GetFormID();

				const auto& impactPos = a_event.hitData.impactData.location;

				// Distance from where the hit actually registered to
				// where the locked target actually is right now - the
				// direct answer to "did the impact land near the target,
				// or somewhere else (e.g. along the original, un-
				// redirected aim line)". Only meaningful when this hit IS
				// the locked target (isLockedTarget) - logged for every
				// hit regardless so a hit on something ELSE near the
				// expected time (a wall, a different actor) is visible
				// too, which is its own useful signal.
				float distanceToLockedTarget = -1.0f;
				if (lockedActor) {
					distanceToLockedTarget = impactPos.GetDistance(lockedActor->GetPosition());
				}

				VATS_LOG(
					"[VATS] hitevent: target=0x{:08X}{} cause=0x{:08X} limb={} material='{}' usesHitData={} "
					"projectileFormID=0x{:08X} impactPos=({:.1f},{:.1f},{:.1f}) distToLockedTarget={:.2f}",
					targetFormID, isLockedTarget ? " (LOCKED)" : "",
					causeFormID,
					a_event.hitData.damageLimb.underlying(),
					a_event.material.c_str(),
					a_event.usesHitData,
					a_event.projectileFormID,
					impactPos.x, impactPos.y, impactPos.z,
					distanceToLockedTarget);

				return RE::BSEventNotifyControl::kContinue;
			}
		};

		Sink s_sink;
	}

	void HitEventLogger::Start()
	{
		auto* source = RE::TESHitEvent::GetEventSource();
		if (!source) {
			VATS_WARN("[VATS] hit-event logger: GetEventSource() returned null, not registered");
			return;
		}
		source->RegisterSink(&s_sink);
		VATS_LOG("[VATS] hit-event logger registered");
	}
}
