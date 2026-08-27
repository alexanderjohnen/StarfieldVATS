#pragma once

#include <cstdint>
#include <functional>
#include <stop_token>

namespace VATS
{
	// The signature of a Win32 low-level hook procedure, spelled without
	// dragging <Windows.h> into every translation unit that includes this
	// header. On x64 this is exact rather than approximate: LRESULT/LPARAM
	// are both signed 64-bit, WPARAM is unsigned 64-bit, and CALLBACK
	// (__stdcall) collapses into the single native x64 calling convention,
	// so there is no ABI difference left to get wrong. The .cpp casts to
	// HOOKPROC.
	using LowLevelHookProc = std::intptr_t (*)(int, std::uintptr_t, std::intptr_t);

	enum class LowLevelHookKind
	{
		kMouse,     // WH_MOUSE_LL
		kKeyboard,  // WH_KEYBOARD_LL
	};

	// Owns the whole lifecycle of one low-level input hook: installs it,
	// pumps the messages that make it fire, runs the caller's deferred
	// work, and takes the hook back out again.
	//
	// Two properties matter here and neither is incidental, because a
	// low-level hook is not a cost this process pays privately.
	// SetWindowsHookEx(WH_MOUSE_LL / WH_KEYBOARD_LL) asks Windows to route
	// EVERY mouse and keyboard event on the entire desktop - in every
	// application, whether or not Starfield is focused or even visible -
	// through this process, and to WAIT for us before letting the event
	// reach the application it was actually meant for.
	//
	// 1. The pump BLOCKS waiting for input (MsgWaitForMultipleObjectsEx)
	//    instead of sleeping a fixed interval and only then looking. The
	//    three hook threads used to sleep 5ms per iteration, so each hook
	//    could sit on an event for up to 5ms before its callback ran, and
	//    with three hooks installed that is up to 15ms added to every
	//    keystroke and every mouse movement, everywhere on the machine,
	//    for as long as the game is running. Windows does not treat that
	//    as an error - LowLevelHooksTimeout defaults to 300ms, far above
	//    it - so nothing complains, the whole desktop simply feels
	//    sluggish. This is very likely the "PC wird nach einer Weile
	//    langsam" Alexander reported on 2026-08-27, which he saw with
	//    Starfield merely sitting in the BACKGROUND while he worked in
	//    another application - the one situation in which none of this
	//    mod's actual features are doing anything at all.
	//
	// 2. The hook is installed only while the game window has focus, and
	//    is taken out again the moment it loses focus. Every feature built
	//    on these hooks (fire detection, ADS, back key) already checks for
	//    focus before doing anything, so an unfocused hook could never
	//    have done anything except make other applications wait.
	void RunLowLevelHookPump(
		const std::stop_token&       a_stop,
		LowLevelHookKind             a_kind,
		LowLevelHookProc             a_proc,
		const char*                  a_name,
		const std::function<void()>& a_onPump,
		const std::function<void()>& a_onUninstall);
}
