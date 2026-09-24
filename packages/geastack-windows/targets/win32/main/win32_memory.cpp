// SPDX-License-Identifier: Apache-2.0
#include "memory.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace gea::framework::memory {

namespace {
struct BlockHeader {
	void *base;
	std::size_t size;
	std::size_t alignment;
};

BlockHeader *header(void *ptr)
{
	return static_cast<BlockHeader *>(ptr) - 1;
}
}  // namespace

void *Allocator::allocatePreferSpiram(std::size_t size, std::size_t alignment)
{
	if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) return nullptr;
	alignment = std::max(alignment, alignof(BlockHeader));
	const std::size_t padding = sizeof(BlockHeader) + alignment - 1;
	if (padding < alignment || size > std::numeric_limits<std::size_t>::max() - padding) return nullptr;
	void *base = std::malloc(size + padding);
	if (!base) return nullptr;
	const auto aligned = (reinterpret_cast<std::uintptr_t>(base) + padding) & ~static_cast<std::uintptr_t>(alignment - 1);
	void *ptr = reinterpret_cast<void *>(aligned);
	*header(ptr) = {base, size, alignment};
	return ptr;
}

void *Allocator::reallocatePreferSpiram(void *ptr, std::size_t size)
{
	if (!ptr) return allocatePreferSpiram(size, alignof(std::max_align_t));
	if (size == 0) {
		free(ptr);
		return nullptr;
	}
	const BlockHeader old = *header(ptr);
	void *next = allocatePreferSpiram(size, old.alignment);
	if (!next) return nullptr;
	std::memcpy(next, ptr, std::min(old.size, size));
	free(ptr);
	return next;
}

void Allocator::free(void *ptr) noexcept
{
	if (ptr) std::free(header(ptr)->base);
}

}  // namespace gea::framework::memory
