// Provide efficient memory management.

#include "memory.h"

namespace handwork
{
	// Memory Allocation Functions
	void *AllocAligned(size_t size) 
	{
		return _aligned_malloc(size, HANDWORK_L1_CACHE_LINE_SIZE);
	}

	void FreeAligned(void *ptr) 
	{
		if (!ptr) return;
		_aligned_free(ptr);
	}

}