#include "CompanionSupport.h"

#include "ActorValueProbe.h"
#include "HealthReader.h"
#include "Settings.h"
#include "Targeting.h"

namespace VATS
{
	void CompanionSupport::RequestHeal()
	{
		const auto* tasks = SFSE::GetTaskInterface();
		if (!tasks) {
			VATS_ERROR("[support] task interface unavailable, cannot heal");
			return;
		}
		tasks->AddTask([]() {
			HealCrosshairTeammate();
		});
	}

	void CompanionSupport::HealCrosshairTeammate()
	{
		auto target = GetCrosshairTeammate();
		if (!target) {
			// Deliberately logged rather than silent: on a first outing the
			// interesting question is not "did it heal" but "did it even
			// find anybody", and those two failures look identical from the
			// player's side.
			VATS_LOG("[support] no live companion under the crosshair");
			return;
		}

		const std::uint32_t formID = target->GetFormID();

		// Read before and after through the path that is already trusted.
		// This is what turns the press into a measurement: if before and
		// after are identical, RestoreActorValue did not do what its name
		// says on this build, and that is worth knowing immediately rather
		// than inferring from a health bar.
		HealthReading before{};
		const bool    haveBefore = GetActorHealth(target.get(), before);

		const float amount = Settings::Get().supportHealAmount;
		if (!TryRestoreHealth(target.get(), amount)) {
			VATS_WARN("[support] formID=0x{:08X}: RestoreActorValue not reachable (no verified ActorValueOwner)", formID);
			return;
		}

		HealthReading after{};
		const bool    haveAfter = GetActorHealth(target.get(), after);

		VATS_LOG("[support] healed formID=0x{:08X} by {:.0f}: {:.2f} -> {:.2f} (max {:.0f}, read ok={}/{})",
			formID, amount,
			haveBefore ? before.current : -1.0f,
			haveAfter ? after.current : -1.0f,
			haveAfter ? after.max : -1.0f,
			haveBefore, haveAfter);
	}
}
