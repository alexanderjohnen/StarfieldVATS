#include "AdsBlocker.h"

#include "Settings.h"
#include "VATSController.h"

#include <chrono>
#include <thread>

namespace VATS
{
	namespace
	{
		// RE::PlayerCamera::QCameraEquals(kIronSights)/SetCameraState
		// (RE/P/PlayerCamera.h) - a real, already-used CommonLibSF pair:
		// ForceFirstPerson()/ForceThirdPerson() in that same header are
		// thin wrappers around this exact SetCameraState call, so it's a
		// trusted primitive, not a fresh guess. Only
		// PlayerCamera::GetSingleton() involves a REL::ID at all (the
		// singleton pointer); QCameraEquals is a plain pointer comparison
		// (currentState == cameraStates[kIronSights]), safe to read from
		// any thread. SetCameraState is routed through the SFSE task
		// interface to run on the game thread, matching this project's
		// established caution around calling engine functions from an
		// arbitrary background thread (see VATSController.cpp's
		// CloseScannerIfOpen comment on why IsMenuOpen specifically was
		// once crash-prone off the game thread).
		//
		// Polls at a light cadence rather than reacting to an input event -
		// there's no reliable input-level signal for "about to ADS" (see
		// AdsBlocker.h), so this just continuously enforces "not in iron
		// sights while Locked" instead.
		constexpr auto kPollInterval = std::chrono::milliseconds(20);

		std::atomic<bool> s_wasBlocking{ false };  // throttles the log line to just the on/off edges

		void EnforceNoAds()
		{
			if (!Settings::Get().blockAdsWhileLocked || Controller::Get().GetMode() != VATSMode::kLocked) {
				s_wasBlocking.store(false, std::memory_order_relaxed);
				return;
			}

			auto* camera = RE::PlayerCamera::GetSingleton();
			if (!camera || !camera->QCameraEquals(RE::CameraState::kIronSights)) {
				s_wasBlocking.store(false, std::memory_order_relaxed);
				return;
			}

			if (!s_wasBlocking.exchange(true, std::memory_order_relaxed)) {
				REX::INFO("[VATS] ADS: camera entered iron-sights while Locked, forcing back to first-person");
			}

			auto* tasks = SFSE::GetTaskInterface();
			if (!tasks) {
				return;
			}
			tasks->AddTask([]() {
				// Re-check on the game thread - state may have changed
				// between the poll and this task actually running.
				auto* cam = RE::PlayerCamera::GetSingleton();
				if (cam && cam->QCameraEquals(RE::CameraState::kIronSights)) {
					cam->SetCameraState(RE::CameraState::kFirstPerson);
				}
			});
		}
	}

	void AdsBlocker::ThreadProc(const std::stop_token& a_stop)
	{
		REX::INFO("ADS blocker started");
		while (!a_stop.stop_requested()) {
			EnforceNoAds();
			std::this_thread::sleep_for(kPollInterval);
		}
	}

	void AdsBlocker::Start()
	{
		if (m_thread.joinable()) {
			return;
		}
		m_thread = std::jthread(&AdsBlocker::ThreadProc);
	}

	void AdsBlocker::Stop()
	{
		if (m_thread.joinable()) {
			m_thread.request_stop();
			m_thread.join();
		}
	}
}
