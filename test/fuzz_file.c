// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the effect file reader: the bytes a program reads from disk or a
// network and hands the library. Most inputs have the header's size and
// hash set to match first, so mutations reach the sections rather than
// stopping at the seal; one in eight goes in as it is. A file that reads
// writes back to bytes that read and write again to themselves (a field
// that holds its default is read, and left out when written), and is
// created as an effect on a context holding the golden scene's grids,
// meshes and host module, which it names; a refusal must be one the
// reader documents, at an offset in the file, and creating from the bytes
// must refuse them alike. A file the def validator refuses is measured
// first, then refused at its effect section.

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

// The offset of a file's effect section, whose tag is 4 in its table, and
// its end in end.
static int64_t EffectSection(const uint8_t* bytes, int64_t* end)
{
    for (uint32_t i = 0; i < Get32(bytes, 12); ++i)
    {
        if (Get32(bytes, 24 + 12 * (size_t)i) == 4)
        {
            int64_t at = Get32(bytes, 24 + 12 * (size_t)i + 4);
            *end = at + Get32(bytes, 24 + 12 * (size_t)i + 8);
            return at;
        }
    }
    return -1;
}

// A refusal of a status: of it, and naming a field of this version (or
// none), whose path writes as text.
static void ExpectNamed(const mvfxEffectRefusal* refusal, mvfxResult status)
{
    char text[512];
    int64_t length = 0;
    Expect(refusal->result == status &&
           mvfxGetEffectFieldPath(refusal, text, sizeof text, &length) == mvfx_success &&
           (length == 0) == (refusal->tagCount == 0));
}

// The bytes a def writes, in written; malloc's, its size in size.
static uint8_t* Rewritten(const mvfxEffectDef* def, int64_t* size)
{
    Expect(mvfxWriteEffect(def, s_sceneAssets, s_sceneAssetCount, NULL, 0, size, NULL).result ==
           mvfx_errorCapacity);
    uint8_t* bytes = malloc((size_t)*size);
    Expect(bytes != NULL);
    int64_t written = 0;
    Expect(mvfxWriteEffect(def, s_sceneAssets, s_sceneAssetCount, bytes, *size, &written, NULL)
                   .result == mvfx_success &&
           written == *size);
    return bytes;
}

// Reads a file whose header is whole and sealed; checks what it gives.
static void ReadSealed(const uint8_t* bytes, int64_t size)
{
    int64_t needed = 0;
    mvfxEffectDef def;
    mvfxEffectRefusal refusal;
    mvfxEffectRefusal madeRefusal;
    mvfxFileResult result = mvfxReadEffect(bytes, size, s_sceneAssets, s_sceneAssetCount, NULL, 0,
                                           &needed, &def, &refusal);
    Expect(result.offset >= -1 && result.offset <= size);
    ExpectNamed(&refusal, result.result);
    mvfxEffectId effect = {0};
    if (result.result != mvfx_errorCapacity)
    {
        Expect(result.result == mvfx_errorFormat || result.result == mvfx_errorVersion ||
               result.result == mvfx_errorMissing);
        mvfxFileResult created = mvfxCreateEffectFromBytes(
            s_fuzzContext, bytes, size, s_sceneAssets, s_sceneAssetCount, &effect, &madeRefusal);
        Expect(created.result == result.result && created.offset == result.offset &&
               memcmp(&madeRefusal, &refusal, sizeof refusal) == 0);
        return;
    }
    void* block = malloc((size_t)needed + 16);
    Expect(block != NULL);
    void* memory = (void*)(((uintptr_t)block + 15) & ~(uintptr_t)15);
    result = mvfxReadEffect(bytes, size, s_sceneAssets, s_sceneAssetCount, memory, needed, &needed,
                            &def, &refusal);
    ExpectNamed(&refusal, result.result);
    // Measured before the def validator, which needs the def in memory:
    // a def it refuses lies at a field of the effect section, or at the
    // section itself for the def as a whole.
    int64_t end = 0;
    int64_t section = EffectSection(bytes, &end);
    if (result.result != mvfx_success)
    {
        Expect(result.result == mvfx_errorFormat && result.offset >= section &&
               result.offset < end && (refusal.tagCount > 0 || result.offset == section));
        mvfxFileResult created = mvfxCreateEffectFromBytes(
            s_fuzzContext, bytes, size, s_sceneAssets, s_sceneAssetCount, &effect, &madeRefusal);
        Expect(created.result == result.result && created.offset == result.offset &&
               memcmp(&madeRefusal, &refusal, sizeof refusal) == 0);
        free(block);
        return;
    }
    int64_t written = 0;
    uint8_t* again = Rewritten(&def, &written);
    void* second = malloc((size_t)needed + 16);
    Expect(second != NULL);
    void* secondMemory = (void*)(((uintptr_t)second + 15) & ~(uintptr_t)15);
    mvfxEffectDef reread;
    int64_t neededAgain = 0;
    Expect(mvfxReadEffect(again, written, s_sceneAssets, s_sceneAssetCount, secondMemory, needed,
                          &neededAgain, &reread, NULL)
               .result == mvfx_success);
    int64_t thirdSize = 0;
    uint8_t* third = Rewritten(&reread, &thirdSize);
    Expect(thirdSize == written && memcmp(third, again, (size_t)written) == 0);
    free(third);
    free(second);
    free(again);
    free(block);
    mvfxFileResult created = mvfxCreateEffectFromBytes(s_fuzzContext, bytes, size, s_sceneAssets,
                                                       s_sceneAssetCount, &effect, &madeRefusal);
    ExpectNamed(&madeRefusal, created.result);
    // What the context refuses (an asset that does not fit, the context
    // full) lies at the field that names it, or at no byte.
    Expect(created.result <= 0 &&
           (madeRefusal.tagCount == 0 ? created.offset == -1
                                      : created.offset >= section && created.offset < end));
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
        PutLittle(bytes + 16, mvfxHash64(MVFX_HASH_INIT, bytes + 24, size - 24), 8);
    }
    ReadSealed(bytes, (int64_t)size);
    free(bytes);
    return 0;
}
