#pragma once

namespace VATS
{
	// SEH-guarded memory read: copies a_len bytes from a_src into a_dst,
	// returning false instead of crashing if a_src is not readable. The
	// foundation of the offset-probing diagnostics — after three crashes
	// from unverified CommonLibSF struct offsets, no raw game-memory read
	// in probing code goes unguarded.
	[[nodiscard]] bool SafeRead(const void* a_src, void* a_dst, std::size_t a_len) noexcept;
}
