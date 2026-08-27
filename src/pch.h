#pragma once

#include "RE/Starfield.h"
#include "SFSE/SFSE.h"

// Force-included ahead of every translation unit in src/ (see xmake.lua),
// which is what lets VATS_LOG/VATS_TRACE be used anywhere without a
// per-file include. Must stay AFTER SFSE.h - the macros expand to REX::*.
#include "Log.h"
