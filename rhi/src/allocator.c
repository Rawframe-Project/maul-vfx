// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The one file of the part's sources that reaches the C library's memory.

#include "allocator.h"

#include <stdalign.h>
#include <stdlib.h>

bool mvfxRhiIsAllocatorValid(const mvfxAllocator* allocator)
{
    return (allocator->alloc == nullptr) == (allocator->free == nullptr);
}

void* mvfxRhiAllocate(const mvfxAllocator* allocator, size_t size, size_t alignment)
{
    if (allocator->alloc != nullptr)
    {
        return allocator->alloc(size, alignment, allocator->context);
    }
    return alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
}

void mvfxRhiRelease(const mvfxAllocator* allocator, void* memory, size_t size, size_t alignment)
{
    if (memory == nullptr)
    {
        return;
    }
    if (allocator->free != nullptr)
    {
        allocator->free(memory, size, alignment, allocator->context);
        return;
    }
    free(memory);
}
