// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An effect in a file: a swirling emitter whose force reads a vector
// field, a grid of the host's named in the file through an asset table.
// The def is written as bytes, read back into a def that writes the same
// bytes again, and made into an effect beside the one made from the def
// in code; an instance of each, stepped alike, writes the same records to
// the byte. A damaged copy is refused at the hash that no longer matches.
// Given a path it also writes the file there. It returns 0 when the round
// trip is exact and the damage is caught.

// Before every include: the C library's fopen without MSVC's warning.
#define _CRT_SECURE_NO_WARNINGS

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/file.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SIDE     8
#define CAPACITY 256

static float s_field[SIDE * SIDE * SIDE * 3];
static unsigned char s_file[16384];
static unsigned char s_again[16384];
static _Alignas(16) unsigned char s_memory[16384];
static float s_records[2][CAPACITY][7];

// A swirl about Y, (-z, 0, x) over a box of SIDE points a side.
static mvfxGridId MakeField(mvfxContext* context)
{
    for (int32_t z = 0; z < SIDE; ++z)
    {
        for (int32_t y = 0; y < SIDE; ++y)
        {
            for (int32_t x = 0; x < SIDE; ++x)
            {
                float* v = &s_field[3 * ((z * SIDE + y) * SIDE + x)];
                v[0] = -((float)z / (SIDE - 1) - 0.5f);
                v[1] = 0.0f;
                v[2] = (float)x / (SIDE - 1) - 0.5f;
            }
        }
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = SIDE;
    def.size[1] = SIDE;
    def.size[2] = SIDE;
    def.components = 3;
    def.values = s_field;
    mvfxGridId grid = {0};
    if (mvfxCreateGrid(context, &def, &grid) != mvfx_success)
    {
        printf("the grid was refused\n");
    }
    return grid;
}

// Writes an instance's records after two seconds.
static bool Run(mvfxContext* context, mvfxEffectId effect, mvfxLayoutId layout, float (*records)[7],
                int64_t* countOut)
{
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    def.seed = 7;
    mvfxInstanceId instance = {0};
    bool ok = mvfxCreateInstance(context, &def, &instance) == mvfx_success;
    for (int32_t step = 0; ok && step < 120; ++step)
    {
        ok = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
    }
    const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
    return ok &&
           mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, records,
                                       CAPACITY, countOut) == mvfx_success &&
           mvfxDestroyInstance(context, instance) == mvfx_success;
}

static bool Save(const char* path, int64_t size)
{
    FILE* file = fopen(path, "wb");
    bool written = file != NULL && fwrite(s_file, 1, (size_t)size, file) == (size_t)size;
    written = file != NULL && fclose(file) == 0 && written;
    printf(written ? "wrote %s\n" : "cannot write %s\n", path);
    return written;
}

int main(int argc, char** argv)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        printf("no context\n");
        return 1;
    }
    // The emitter: a ball of particles carried round by the field and
    // fading out.
    static const mvfxAlphaKey fade[2] = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    static const mvfxColorKey orange[1] = {{0.0f, 1.0f, 0.5f, 0.1f}};
    const mvfxForce swirl = {.kind = mvfx_forceVectorField,
                             .strength = {.kind = mvfx_sourceRange, .range = {4.0f, 4.0f}},
                             .grid = MakeField(context),
                             .boxSize = {4.0f, 4.0f, 4.0f},
                             .boxRotation = {0.0f, 0.0f, 0.0f, 1.0f}};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = CAPACITY;
    emitter.rate = 100.0f;
    emitter.lifetime = mvfxMakeRangeSource(1.5f, 2.0f);
    emitter.shape.kind = mvfx_shapeSphere;
    emitter.shape.radius = 1.0f;
    emitter.shape.thickness = 0.5f;
    emitter.velocity.speed = (mvfxRange){0.2f, 0.5f};
    emitter.forces = &swirl;
    emitter.forceCount = 1;
    emitter.overLife.color.enable = true;
    emitter.overLife.color.gradient = (mvfxGradient){
        .colorKeys = orange, .colorKeyCount = 1, .alphaKeys = fade, .alphaKeyCount = 2};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    // The grid as the file names it.
    const mvfxEffectAsset assets[1] = {
        {.kind = mvfx_assetGrid, .name = "swirl", .nameLength = 5, .grid = swirl.grid}};

    // Write, read back, write again: the same bytes.
    int64_t size = 0;
    int64_t againSize = 0;
    int64_t needed = 0;
    mvfxEffectDef read;
    bool ok =
        mvfxWriteEffect(&def, assets, 1, s_file, sizeof s_file, &size, NULL).result ==
            mvfx_success &&
        mvfxReadEffect(s_file, size, assets, 1, s_memory, sizeof s_memory, &needed, &read, NULL)
                .result == mvfx_success &&
        mvfxWriteEffect(&read, assets, 1, s_again, sizeof s_again, &againSize, NULL).result ==
            mvfx_success &&
        againSize == size && memcmp(s_file, s_again, (size_t)size) == 0;
    printf("the file: %lld bytes, its def read back in %lld bytes of memory, written again %s\n",
           (long long)size, (long long)needed, ok ? "to the same bytes" : "differently");

    // An effect from the def in code and one from the file, stepped alike.
    static const mvfxLayoutField fields[2] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordColor, .format = mvfx_formatFloat32, .components = 4, .offset = 12},
    };
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = fields;
    layoutDef.fieldCount = 2;
    layoutDef.stride = 28;
    mvfxLayoutId layout = {0};
    mvfxEffectId fromCode = {0};
    mvfxEffectId fromFile = {0};
    int64_t counts[2] = {0, 0};
    ok = ok && mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success &&
         mvfxCreateEffect(context, &def, &fromCode, NULL) == mvfx_success &&
         mvfxCreateEffectFromBytes(context, s_file, size, assets, 1, &fromFile, NULL).result ==
             mvfx_success &&
         Run(context, fromCode, layout, s_records[0], &counts[0]) &&
         Run(context, fromFile, layout, s_records[1], &counts[1]);
    bool same = ok && counts[0] == counts[1] && counts[0] > 0 &&
                memcmp(s_records[0], s_records[1], (size_t)counts[0] * sizeof s_records[0][0]) == 0;
    printf("two seconds of each: %lld and %lld particles, %s\n", (long long)counts[0],
           (long long)counts[1], same ? "the same to the byte" : "different");

    // A damaged copy: one bit flipped halfway in.
    memcpy(s_again, s_file, (size_t)size);
    s_again[size / 2] ^= 0x10u;
    mvfxEffectId damaged = {0};
    mvfxFileResult refused =
        mvfxCreateEffectFromBytes(context, s_again, size, assets, 1, &damaged, NULL);
    bool caught = refused.result == mvfx_errorFormat && refused.offset >= 0;
    printf("a bit flipped at byte %lld: refused at byte %lld, the header's hash of the "
           "payload, which no longer matches\n",
           (long long)(size / 2), (long long)refused.offset);
    mvfxDestroyContext(context);

    ok = same && caught && (argc < 2 || Save(argv[1], size));
    printf(ok ? "the file round trip is exact\n" : "the file round trip failed\n");
    return ok ? 0 : 1;
}
