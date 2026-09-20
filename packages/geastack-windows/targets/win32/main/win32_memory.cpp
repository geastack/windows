// SPDX-License-Identifier: Apache-2.0
#include "memory.h"

#include <cstdlib>
#include <malloc.h>
#include <new>

namespace gea::framework::memory {

void *Allocator::allocatePreferSpiram(std::size_t size, std::size_t alignment)
{
	if (size == 0) return nullptr;
	// The framework asks for default alignment everywhere it frees through
	// this allocator; an over-aligned request gets the CRT's aligned block,
	// which its callers release through the matching aligned free.
	if (alignment <= alignof(std::max_align_t)) return std::malloc(size);
	return _aligned_malloc(size, alignment);
}

void *Allocator::reallocatePreferSpiram(void *ptr, std::size_t size)
{
	if (size == 0) {
		std::free(ptr);
		return nullptr;
	}
	return std::realloc(ptr, size);
}

void Allocator::free(void *ptr) noexcept
{
	std::free(ptr);
}

}  // namespace gea::framework::memory
