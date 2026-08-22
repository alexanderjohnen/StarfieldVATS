#pragma once

namespace VATS
{
	// Polls the configured activation key on a background thread and hands
	// presses to the Controller. Deliberately simple for stage 1; will be
	// replaced by a proper BSInputEventUser hook once the input path is
	// established in stage 2.
	class HotkeyWatcher
	{
	public:
		static void Start();
		static void Stop();

	private:
		static void ThreadProc(const std::stop_token& a_stop);

		static inline std::jthread m_thread;
	};
}
