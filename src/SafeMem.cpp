#include "SafeMem.h"

#include <cstring>

namespace VATS
{
	// NOTE: this function must contain no objects with destructors —
	// MSVC forbids __try in functions that require unwinding.
	bool SafeRead(const void* a_src, void* a_dst, std::size_t a_len) noexcept
	{
		__try {
			std::memcpy(a_dst, a_src, a_len);
			return true;
		} __except (1) {  // EXCEPTION_EXECUTE_HANDLER
			return false;
		}
	}

	bool SafeWrite(void* a_dst, const void* a_src, std::size_t a_len) noexcept
	{
		__try {
			std::memcpy(a_dst, a_src, a_len);
			return true;
		} __except (1) {  // EXCEPTION_EXECUTE_HANDLER
			return false;
		}
	}
}
