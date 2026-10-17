// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part's memory: the host's allocator, or the C library's for a
// zeroed one.

#ifndef MAUL_VFX_RHI_SRC_ALLOCATOR_H
#define MAUL_VFX_RHI_SRC_ALLOCATOR_H

#include "maul-vfx/base.h"

#include <stdbool.h>
#include <stddef.h>

// Whether an allocator has both its functions or neither.
bool mvfxRhiIsAllocatorValid(const mvfxAllocator* allocator);

// size bytes aligned to alignment, a power of two; NULL when the allocator
// fails, or for a zeroed one an alignment the C library's does not give.
void* mvfxRhiAllocate(const mvfxAllocator* allocator, size_t size, size_t alignment);

// Returns memory mvfxRhiAllocate gave, with the same size and alignment.
// NULL does nothing.
void mvfxRhiRelease(const mvfxAllocator* allocator, void* memory, size_t size, size_t alignment);

#endif
