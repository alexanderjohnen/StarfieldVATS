#pragma once

#include <atomic>
#include <cstddef>

// Level-gated logging wrapper around REX's log macros.
//
// Why a wrapper and not just REX's own level filter: REX::Impl::Log
// formats the message with std::vformat BEFORE anything sees the level
// (see REX/LOG.h) - every call allocates a std::string and runs the whole
// format pass even if the sink then throws it away. That cost is paid on
// the render thread (Overlay's per-frame telemetry) and inside the 2ms
// projectile scan, which is exactly where this project has already
// measured log volume changing gameplay behaviour (see
// CombatTargetOverride.h and ProjectileTracker.cpp's "rejected silently"
// comment). The check therefore has to happen at the CALL SITE, before
// the arguments are formatted - hence macros rather than functions.
//
// Levels (INI: [Debug] iLogLevel):
//   0 kOff     - nothing at all, not even errors
//   1 kErrors  - warnings and errors only
//   2 kNormal  - + state transitions and one-time findings (default)
//   3 kVerbose - + per-shot and per-frame diagnostics
//
// The default is kNormal rather than kVerbose: everything that fires more
// than a few times per second is VATS_TRACE, so a normal play session
// writes a handful of lines per lock instead of a stream. Turn it up to 3
// when a specific probe needs to be read, not as a matter of course.
namespace VATS::Log
{
	enum Level : int
	{
		kOff = 0,
		kErrors = 1,
		kNormal = 2,
		kVerbose = 3,
	};

	// Read from the render thread, the game thread, three input-hook
	// threads and every detached steering thread; written once from
	// Settings::Load. Relaxed is right - there is nothing to order
	// against, a call that reads a stale level for a few microseconds
	// after a reload just logs one line too many or too few.
	inline std::atomic<int> g_level{ kNormal };

	[[nodiscard]] inline bool At(int a_min) noexcept
	{
		return g_level.load(std::memory_order_relaxed) >= a_min;
	}

	// For skipping the WORK behind a diagnostic, not just its log line -
	// WorldBoundProbe re-reads the bounding sphere the draw path already
	// resolved, purely to have something to print.
	[[nodiscard]] inline bool Verbose() noexcept { return At(kVerbose); }
}

#define VATS_ERROR(...) \
	do {                                     \
		if (::VATS::Log::At(::VATS::Log::kErrors)) { REX::ERROR(__VA_ARGS__); } \
	} while (false)

#define VATS_WARN(...) \
	do {                                     \
		if (::VATS::Log::At(::VATS::Log::kErrors)) { REX::WARN(__VA_ARGS__); } \
	} while (false)

#define VATS_LOG(...) \
	do {                                     \
		if (::VATS::Log::At(::VATS::Log::kNormal)) { REX::INFO(__VA_ARGS__); } \
	} while (false)

#define VATS_TRACE(...) \
	do {                                     \
		if (::VATS::Log::At(::VATS::Log::kVerbose)) { REX::INFO(__VA_ARGS__); } \
	} while (false)

namespace VATS::Log
{
	// Every "log this once per actor" / "log only on change" throttle in
	// this project is a function-static map or set keyed by a formID or a
	// raw pointer, and none of them had an eviction rule: an entry went in
	// the first time a thing was seen and stayed for the rest of the
	// process. Over a long session that is unbounded growth by
	// construction - projectile pointers in particular arrive at one or
	// more per shot fired, and
	// dynamically-created actors get fresh 0xFF-prefixed formIDs that
	// never repeat.
	//
	// Clearing the whole cache rather than evicting the oldest entry is
	// deliberate: these are diagnostics, so the only consequence of
	// forgetting is that a handful of one-shot lines can appear a second
	// time, and the alternative (an LRU) would cost more code and more
	// per-call work than the thing it guards.
	template <class T>
	void Cap(T& a_cache, std::size_t a_max = 512)
	{
		if (a_cache.size() > a_max) {
			a_cache.clear();
		}
	}
}
