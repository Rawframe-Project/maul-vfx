// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the effect file reader: the bytes a program reads from disk or a
// network and hands the library. Most inputs have the header's size and
// hash set to match first, so mutations reach the units rather than
// stopping at the seal; one in eight goes in as it is. A file that reads must write
// back to the same bytes and is created as an effect on a context holding
// the golden scene's grids, meshes and host module, which it names; a
// refusal must be one the reader documents, at an offset in the file, and
// creating from the bytes must refuse them alike. A file the def validator
// refuses is measured first, then refused at its effect unit.

#include "vfx_scene.h"

#include "maul-vfx/file.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

static mvfxContext* s_fuzzContext;

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

static void PutLittle(uint8_t* at, uint64_t value, int32_t bytes)
{
    for (int32_t k = 0; k < bytes; ++k)
    {
        at[k] = (uint8_t)(value >> (8 * k));
    }
}

static uint32_t Get32(const uint8_t* at, size_t offset)
{
    return (uint32_t)at[offset] | (uint32_t)at[offset + 1] << 8 | (uint32_t)at[offset + 2] << 16 |
           (uint32_t)at[offset + 3] << 24;
}

// Reads a file whose header is whole and sealed; checks what it gives.
static void ReadSealed(const uint8_t* bytes, int64_t size)
{
    int64_t needed = 0;
    mvfxEffectDef def;
    mvfxFileResult result =
        mvfxReadEffect(bytes, size, s_sceneAssets, s_sceneAssetCount, NULL, 0, &needed, &def);
    Expect(result.offset >= -1 && result.offset <= size);
    mvfxEffectId effect = {0};
    if (result.result != mvfx_errorCapacity)
    {
        Expect(result.result == mvfx_errorInvalid || result.result == mvfx_errorVersion ||
               result.result == mvfx_errorMissing);
        mvfxFileResult created = mvfxCreateEffectFromBytes(
            s_fuzzContext, bytes, size, s_sceneAssets, s_sceneAssetCount, &effect);
        Expect(created.result == result.result && created.offset == result.offset);
        return;
    }
    void* block = malloc((size_t)needed + 16);
    Expect(block != NULL);
    void* memory = (void*)(((uintptr_t)block + 15) & ~(uintptr_t)15);
    result = mvfxReadEffect(bytes, size, s_sceneAssets, s_sceneAssetCount, memory, needed, &needed,
                            &def);
    // Measured before the def validator, which needs the def in memory:
    // a def it refuses lies at the effect unit.
    if (result.result != mvfx_success)
    {
        Expect(result.result == mvfx_errorInvalid && result.offset == (int64_t)Get32(bytes, 76));
        mvfxFileResult created = mvfxCreateEffectFromBytes(
            s_fuzzContext, bytes, size, s_sceneAssets, s_sceneAssetCount, &effect);
        Expect(created.result == result.result && created.offset == result.offset);
        free(block);
        return;
    }
    uint8_t* again = malloc((size_t)size);
    Expect(again != NULL);
    int64_t written = 0;
    Expect(mvfxWriteEffect(&def, s_sceneAssets, s_sceneAssetCount, again, size, &written).result ==
           mvfx_success);
    Expect(written == size && memcmp(again, bytes, (size_t)size) == 0);
    free(again);
    free(block);
    mvfxFileResult created = mvfxCreateEffectFromBytes(s_fuzzContext, bytes, size, s_sceneAssets,
                                                       s_sceneAssetCount, &effect);
    Expect(created.offset == -1 && created.result <= 0);
    if (created.result == mvfx_success)
    {
        Expect(mvfxDestroyEffect(s_fuzzContext, effect) == mvfx_success);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size > ((size_t)1 << 22))
    {
        return 0;
    }
    if (s_fuzzContext == NULL)
    {
        mvfxContextDef def = mvfxDefaultContextDef();
        Expect(mvfxCreateContext(&def, &s_fuzzContext) == mvfx_success);
        BuildScene(s_fuzzContext);
        Expect(s_failures == 0);
    }
    uint8_t* bytes = malloc(size > 0 ? size : 1);
    Expect(bytes != NULL);
    if (size > 0)
    {
        memcpy(bytes, data, size);
    }
    // The low three bits of the hash's first byte, which sealing writes
    // over, choose: one input in eight goes in unsealed, so the size and
    // hash checks are fuzzed too.
    if (size >= 24 && (data[16] & 7) != 0)
    {
        PutLittle(bytes + 8, size, 4);
        PutLittle(bytes + 16, mvfxHash64(MVFX_HASH_INIT, bytes + 24, (int32_t)(size - 24)), 8);
    }
    ReadSealed(bytes, (int64_t)size);
    free(bytes);
    return 0;
}
