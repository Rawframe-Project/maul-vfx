// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The scene's state hashed after every step against values committed here
// (record mvfx-0001: the same bits on every platform). CI runs it on every
// platform of its matrix; one hash differing anywhere fails. A change meant
// to change results updates the values and says so in the changelog. Run
// it with --print to print the values this build computes. The scene runs
// twice: its effects created from their defs, then through effect files,
// each def written, read and written again to the same bytes, and created
// from them, to the same values. With --seeds <dir> it writes those files
// there, as the effect file fuzz target's first seeds.

// Before every include: the C library's fopen without MSVC's warning.
#define _CRT_SECURE_NO_WARNINGS

#include "vfx_scene.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint64_t s_golden[SCENE_STEP_COUNT] = {
    0x9AC522F46FC1510Eu, 0x724E22E0303554DEu, 0xFF9111EDCAD0092Bu, 0x1E4FA46342506518u,
    0x98D7BCBA9AF36B4Au, 0xC6C5274B5254CF5Cu, 0x6F84540826B01135u, 0x12D9FA4DDE720B40u,
    0xBFAABB43FF7A3B0Bu, 0x7AEF0C6A1201F411u, 0xFABE161476C8302Du, 0xE55B5D92991A219Bu,
    0x6C057B9532E41F42u, 0x27A4F426B92EE563u, 0xD7E2F7781904AE27u, 0x5EF9DC7E41E49BEFu,
    0xA9F63B9C92975E50u, 0xA6DBC768C6EBF76Eu, 0x359C67B23E547E5Bu, 0xAA316B24E68EC9B5u};

// Bytes of memory aligned to 16, by hand, freed through the block.
static void* Aligned(int64_t bytes, void** block)
{
    *block = malloc((size_t)bytes + 16);
    CHECK(*block != NULL, "memory");
    return (void*)(((uintptr_t)*block + 15) & ~(uintptr_t)15);
}

// A def written as a file, its size in size.
static uint8_t* Written(const mvfxEffectDef* def, int64_t* size)
{
    mvfxFileResult measured = mvfxWriteEffect(def, s_sceneAssets, s_sceneAssetCount, NULL, 0, size);
    CHECK(measured.result == mvfx_errorCapacity && *size > 0, "the file measured");
    uint8_t* bytes = malloc((size_t)*size);
    CHECK(bytes != NULL, "file memory");
    int64_t written = 0;
    mvfxFileResult result =
        mvfxWriteEffect(def, s_sceneAssets, s_sceneAssetCount, bytes, *size, &written);
    CHECK(result.result == mvfx_success && written == *size, "the file written");
    return bytes;
}

// Creates an effect through a file: the def written, read and written
// again to the same bytes, then created from them.
// Where --seeds writes the files, and how many it has written.
static const char* s_seedDir;
static int32_t s_seedCount;

static void WriteSeed(const uint8_t* bytes, int64_t size)
{
    char path[4096];
    int length = snprintf(path, sizeof path, "%s/scene-%d.mvfx", s_seedDir, (int)s_seedCount++);
    CHECK(length > 0 && (size_t)length < sizeof path, "the seed's path");
    FILE* file = fopen(path, "wb");
    CHECK(file != NULL, "the seed opened");
    if (file != NULL)
    {
        CHECK(fwrite(bytes, 1, (size_t)size, file) == (size_t)size, "the seed written");
        CHECK(fclose(file) == 0, "the seed closed");
    }
}

static mvfxResult ThroughFile(mvfxContext* context, const mvfxEffectDef* def,
                              mvfxEffectId* effectOut)
{
    int64_t size = 0;
    uint8_t* bytes = Written(def, &size);
    if (s_seedDir != NULL)
    {
        WriteSeed(bytes, size);
    }
    int64_t needed = 0;
    mvfxEffectDef read;
    mvfxFileResult result =
        mvfxReadEffect(bytes, size, s_sceneAssets, s_sceneAssetCount, NULL, 0, &needed, &read);
    CHECK(result.result == mvfx_errorCapacity && needed > 0, "the def measured");
    void* block = NULL;
    void* memory = Aligned(needed, &block);
    result = mvfxReadEffect(bytes, size, s_sceneAssets, s_sceneAssetCount, memory, needed, &needed,
                            &read);
    CHECK(result.result == mvfx_success, "the file read");
    int64_t again = 0;
    uint8_t* rewritten = Written(&read, &again);
    CHECK(again == size && memcmp(bytes, rewritten, (size_t)size) == 0,
          "the def read writes the same bytes");
    result = mvfxCreateEffectFromBytes(context, bytes, size, s_sceneAssets, s_sceneAssetCount,
                                       effectOut);
    free(rewritten);
    free(block);
    free(bytes);
    return result.result;
}

static void RunScene(bool print)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    BuildScene(context);
    for (int32_t step = 0; step < SCENE_STEP_COUNT; ++step)
    {
        MoveScene(context, step);
        CHECK(mvfxContext_StepTicks(context, s_sceneSteps[step]) == mvfx_success, "step");
        uint64_t hash = HashScene(context);
        if (print)
        {
            printf("    0x%016" PRIX64 "u,\n", hash);
        }
        CHECK(hash == s_golden[step], "the state matches the committed hash");
    }
    mvfxDestroyContext(context);
}

int main(int argc, char** argv)
{
    s_seedDir = argc > 2 && strcmp(argv[1], "--seeds") == 0 ? argv[2] : NULL;
    RunScene(argc > 1 && strcmp(argv[1], "--print") == 0);
    s_sceneCreate = ThroughFile;
    RunScene(false);
    return s_failures == 0 ? 0 : 1;
}
