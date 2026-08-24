#include "HealthWidgetReader.h"

namespace VATS
{
	// DISABLED 2026-08-24, second time this exact mechanism has hard-broken
	// Alexander's game in a row - see HealthWidgetReader.h for the full
	// history. First version's "" (bare clip) suffix crashed instantly
	// (a kDisplayObject Value's destructor calling an unexercised
	// ObjectRelease). Removing that suffix was NOT enough: the very next
	// deployed version, querying only named properties like ".percent" on
	// the same live widget, froze the entire game (and, per Alexander,
	// the whole PC's input until the process was killed via Task Manager -
	// consistent with a low-level-hook-adjacent or otherwise system-wide
	// stall, not a normal single-process crash) - the SFSE log cuts off
	// mid-probe with no further output at all, same "something well past
	// merely wrong" signature as the first crash. A named property is
	// evidently not a reliable guarantee of getting back a primitive
	// either - Scaleform/AS3 property access can itself resolve to another
	// object, a getter, or trigger runtime evaluation, and the underlying
	// object-lifecycle machinery this project would need to handle that
	// safely (Value::ObjectInterface::*) has now caused two different kinds
	// of hard failure on first real exercise.
	//
	// Given two severe failures in a row with no way to iterate locally
	// (every attempt costs Alexander a full crash/freeze cycle), this
	// mechanism is retired rather than guessed at a third time. Reading
	// HUDMenu's Scaleform tree for *primitive* leaf values (booleans via
	// `_visible`) remains fine and unrelated - CombatHudVisibility.cpp does
	// exactly that, ran to completion in both failing sessions without
	// issue, and is unaffected by this. What's specifically retired is
	// probing/reading *unknown, guessed* AS variable paths against a live
	// widget for anything beyond a `_visible`-style boolean toggle.
	//
	// Do not re-enable without a fundamentally different, safer approach -
	// e.g. a real Scaleform/SWF decompile of hudmenu.gfx to know the exact
	// variable and its real type ahead of time, instead of guessing paths
	// live against the running movie.
	bool HealthWidgetReader::GetLiveHealthPercent(float& /*a_outPercent*/)
	{
		return false;
	}
}
