#pragma once

namespace VATS
{
	// SEH-guarded memory read: copies a_len bytes from a_src into a_dst,
	// returning false instead of crashing if a_src is not readable. The
	// foundation of the offset-probing diagnostics — after three crashes
	// from unverified CommonLibSF struct offsets, no raw game-memory read
	// in probing code goes unguarded.
	[[nodiscard]] bool SafeRead(const void* a_src, void* a_dst, std::size_t a_len) noexcept;

	// Same guarantee, opposite direction: copies a_len bytes from a_src
	// into a_dst (the game-memory address), returning false instead of
	// crashing if a_dst is not writable. First used 2026-08-22 for the
	// projectile-redirect feature (ProjectileTracker.cpp) — the first
	// place this project writes into a live engine object rather than
	// only reading one. Guards against the address itself going bad
	// (e.g. the projectile despawning between being found and being
	// written to) — it does NOT provide synchronization against the
	// game's own simulation thread concurrently touching the same bytes;
	// see ProjectileTracker.cpp's comment on why that risk was accepted
	// for plain-float fields specifically.
	[[nodiscard]] bool SafeWrite(void* a_dst, const void* a_src, std::size_t a_len) noexcept;
}
