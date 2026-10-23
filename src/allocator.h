// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Memory through a def's allocator. A zeroed allocator means the C
// library's functions; for alignments beyond max_align_t they are asked
// for a larger block and the aligned part of it is handed out. A layout
// lays out a block's parts with checked arithmetic, so a size that
// overflows is refused instead of wrapping.

#ifndef MAUL_VFX_SRC_ALLOCATOR_H
#define MAUL_VFX_SRC_ALLOCATOR_H

#include "maul-vfx/base.h"

#include <stdbool.h>

// Whether an allocator is usable: both functions set, or neither.
bool mvfxIsAllocatorValid(const mvfxAllocator* allocator);

// size bytes aligned to alignment, a power of two, or NULL.
void* mvfxAllocate(const mvfxAllocator* allocator, size_t size, size_t alignment);

// Returns memory mvfxAllocate gave, with the same size and alignment.
// NULL does nothing.
void mvfxRelease(const mvfxAllocator* allocator, void* memory, size_t size, size_t alignment);

// A block being laid out: its size so far, its largest alignment, and
// whether a part did not fit in size_t.
typedef struct mvfxLayout
{
    size_t size;
    size_t alignment;
    bool overflow;
} mvfxLayout;

// Adds count items of itemSize bytes aligned to alignment, a power of
// two, and returns the part's offset (0 after an overflow).
size_t mvfxLayoutAdd(mvfxLayout* layout, size_t count, size_t itemSize, size_t alignment);

#endif
