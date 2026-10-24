// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The one place a zeroed allocator reaches the C library, and the
// checked layout of blocks. A host's allocator is host code, which runs in
// the host's own floating-point environment (src/fpenv.h).

#include "allocator.h"

#include "fpenv.h"
#include "invariant.h"

#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool mvfxIsAllocatorValid(const mvfxAllocator* allocator)
{
    return (allocator->alloc == nullptr) == (allocator->free == nullptr);
}

// The C library serves alignments up to max_align_t itself. A larger one
// is served from a block alignment bytes longer: the aligned address is
// the first one past a size_t's room, and its distance from the start of
// what malloc gave is kept just below it for the release.
static void* AllocateAligned(size_t size, size_t alignment)
{
    if (alignment <= alignof(max_align_t))
    {
        return malloc(size);
    }
    size_t total = 0;
    if (ckd_add(&total, size, alignment) || ckd_add(&total, total, sizeof(size_t)))
    {
        return nullptr;
    }
    unsigned char* raw = malloc(total);
    if (raw == nullptr)
    {
        return nullptr;
    }
    uintptr_t rawAddress = (uintptr_t)raw;
    uintptr_t first = rawAddress + sizeof(size_t);
    uintptr_t aligned = (first + (alignment - 1)) & ~(uintptr_t)(alignment - 1);
    size_t offset = (size_t)(aligned - rawAddress);
    unsigned char* memory = raw + offset;
    memcpy(memory - sizeof offset, &offset, sizeof offset);
    return memory;
}

static void ReleaseAligned(void* memory, size_t alignment)
{
    if (alignment <= alignof(max_align_t))
    {
        free(memory);
        return;
    }
    size_t offset = 0;
    unsigned char* aligned = memory;
    memcpy(&offset, aligned - sizeof offset, sizeof offset);
    free(aligned - offset);
}

static MVFX_NOINLINE void* HostAlloc(const mvfxAllocator* allocator, size_t size, size_t alignment)
{
    mvfxFloatHost host = mvfxFloatHostEnter();
    void* memory = allocator->alloc(size, alignment, allocator->context);
    mvfxFloatHostLeave(host);
    return memory;
}

static MVFX_NOINLINE void HostFree(const mvfxAllocator* allocator, void* memory, size_t size,
                                   size_t alignment)
{
    mvfxFloatHost host = mvfxFloatHostEnter();
    allocator->free(memory, size, alignment, allocator->context);
    mvfxFloatHostLeave(host);
}

void* mvfxAllocate(const mvfxAllocator* allocator, size_t size, size_t alignment)
{
    if (allocator->alloc != nullptr)
    {
        return HostAlloc(allocator, size, alignment);
    }
    return AllocateAligned(size, alignment);
}

void mvfxRelease(const mvfxAllocator* allocator, void* memory, size_t size, size_t alignment)
{
    if (memory == nullptr)
    {
        return;
    }
    if (allocator->free != nullptr)
    {
        HostFree(allocator, memory, size, alignment);
        return;
    }
    ReleaseAligned(memory, alignment);
}

size_t mvfxLayoutAdd(mvfxLayout* layout, size_t count, size_t itemSize, size_t alignment)
{
    MVFX_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0);
    size_t padded = 0;
    size_t bytes = 0;
    size_t end = 0;
    if (layout->overflow || ckd_add(&padded, layout->size, alignment - 1) ||
        ckd_mul(&bytes, count, itemSize))
    {
        layout->overflow = true;
        return 0;
    }
    size_t offset = padded & ~(alignment - 1);
    if (ckd_add(&end, offset, bytes))
    {
        layout->overflow = true;
        return 0;
    }
    layout->size = end;
    if (alignment > layout->alignment)
    {
        layout->alignment = alignment;
    }
    return offset;
}
