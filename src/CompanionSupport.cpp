#include "CompanionSupport.h"

#include "ActorValueProbe.h"
#include "HealthReader.h"
#include "Settings.h"
#include "VATSController.h"

namespace VATS
{
	void CompanionSupport::RequestAction()
	{
		const auto* tasks = SFSE::GetTaskInterface();
		if (!tasks) {
			VATS_ERROR("[support] task interface unavailable, cannot act");
			return;
		}
		tasks->AddTask([]() {
			auto  state = Controller::Get().GetOverlayState();
			if (state.mode != VATSMode::kSupport || !state.actor) {
				return;  // session ended between the keypress and this task
			}
			HealActor(state.actor.get());
		});
	}

	void CompanionSupport::HealActor(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}
		const std::uint32_t formID = a_actor->GetFormID();

		// Read before and after through the path that is already trusted.
		// This is what turns the press into a measurement: if before and
		// after are identical, RestoreActorValue did not do what its name
		// says on this build, and that is worth knowing immediately rather
		// than inferring from a health bar.
		HealthReading before{};
		const bool    haveBefore = GetActorHealth(a_actor, before);

		const float amount = Settings::Get().supportHealAmount;
		if (!TryRestoreHealth(a_actor, amount)) {
			VATS_WARN("[support] formID=0x{:08X}: RestoreActorValue not reachable (no verified ActorValueOwner)", formID);
			return;
		}

		HealthReading after{};
		const bool    haveAfter = GetActorHealth(a_actor, after);

		VATS_LOG("[support] healed formID=0x{:08X} by {:.0f}: {:.2f} -> {:.2f} (max {:.0f}, read ok={}/{})",
			formID, amount,
			haveBefore ? before.current : -1.0f,
			haveAfter ? after.current : -1.0f,
			haveAfter ? after.max : -1.0f,
			haveBefore, haveAfter);
	}
}
