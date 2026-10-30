// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The force module against its own functions: a turbulence's push the
// curl noise where the particle is in the world, far from the origin; and
// an unturned box's and map's quick paths the general one's bits.

#include "context.h"
#include "grid_api.h"
#include "instance.h"
#include "instance_api.h"
#include "noise.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

static const mvfxNoise s_noise = {.frequency = 0.5f,
                                  .octaves = 2,
                                  .lacunarity = 2.0f,
                                  .gain = 0.5f,
                                  .scroll = {1.0f, 0.0f, -2.0f},
                                  .seed = 77u};

static bool Is(const float a[3], float x, float y, float z)
{
    float tolerance = 1e-4f * (1.0f + fabsf(x) + fabsf(y) + fabsf(z));
    return fabsf(a[0] - x) <= tolerance && fabsf(a[1] - y) <= tolerance &&
           fabsf(a[2] - z) <= tolerance;
}

static void TestCurl(void)
{
    // A world turbulence of strength 3 on a world emitter far out (the
    // anchor rebased): strength times the curl where the particle is in
    // the world, at the step's start, a second in.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxForce force = {.kind = mvfx_forceTurbulence, .strength = 3.0f, .noise = s_noise};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitter.forces = &force;
    emitter.forceCount = 1;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    static const double far[3] = {123456.0, -7000.0, 9.0};
    CHECK(mvfxInstance_SetTransform(context, instance, far, (const float[4]){0, 0, 0, 1}, 1.0f,
                                    true) == mvfx_success,
          "placed");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    static const double place[3] = {123456.5, -6999.25, 9.75};
    static const float rest[3] = {0.0f, 0.0f, 0.0f};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = rest};
    CHECK(mvfxInstance_Spawn(context, instance, 0, &request) == mvfx_success, "placed");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 1024) == mvfx_success, "step");
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    float a[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        a[k] = 1024.0f * ((const float*)mvfxAttributeArray(
                             live, 0, (mvfxAttribute)(mvfx_attributeVelocityX + k)))[0];
    }
    float curl[3];
    mvfxCurlNoise(&s_noise, 77u, place, 1.0, curl);
    CHECK(Is(a, 3.0f * curl[0], 3.0f * curl[1], 3.0f * curl[2]), "the curl in the world");
    CHECK(fabsf(curl[0]) + fabsf(curl[1]) + fabsf(curl[2]) > 0.1f, "a field, not nothing");
    mvfxDestroyContext(context);
}

// A field's acceleration and drag at x, the quick paths for an unturned
// box and map taken or not.
static void FieldAt(mvfxResolvedForce force, bool quick, const float x[3], float out[4])
{
    force.unturned = force.unturned && quick;
    force.mapUnturned = force.mapUnturned && quick;
    for (int32_t k = 0; k < 4; ++k)
    {
        out[k] = 0.0f;
    }
    mvfxForcesAt(&force, 1, x, 0.0, out, &out[3]);
}

static void TestUnturnedBits(void)
{
    // A 5x3x4 grid of three components, values of both signs and zeros.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static float values[3 * 60];
    for (uint32_t i = 0; i < 3 * 60; ++i)
    {
        values[i] = i % 7 == 0 ? 0.0f : (float)((i * 2654435761u) >> 8) * 0x1p-23f - 1.0f;
    }
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 5;
    gridDef.size[1] = 3;
    gridDef.size[2] = 4;
    gridDef.components = 3;
    gridDef.values = values;
    mvfxGridId id = {0};
    CHECK(mvfxCreateGrid(context, &gridDef, &id) == mvfx_success, "grid");
    const mvfxGrid* grid = mvfxContextGrid(context, id);
    // Maps: none, moved and scaled unturned, a quarter turn.
    const mvfxSpaceMap maps[3] = {
        {.rotation = {0, 0, 0, 1}, .sigma = 1.0f},
        {.before = {1.5, -2, 0.25}, .after = {-3, 0.5, 7}, .rotation = {0, 0, 0, 1}, .sigma = 2.5f},
        {.before = {0.5, 0, 0}, .rotation = {0, 0, 0.70710678f, 0.70710678f}, .sigma = 1.0f},
    };
    // Places: a lattice past the box's periods each way, its cells' edges,
    // and places not finite or far.
    static const float wild[7] = {INFINITY, -INFINITY, NAN, 1e30f, -3e38f, -0.0f, 0.0f};
    static const float zero[1][4] = {{0}};
    int32_t same = 0;
    int32_t count = 0;
    // Sources: a vector field under each border in an unturned box, a box
    // turned about Z alone, a box turned about every axis, and a turbulence,
    // whose place the quick paths must leave alone.
    static const float turns[3][4] = {
        {0, 0, 0, 1}, {0, 0, 0.70710678f, 0.70710678f}, {0.1f, 0.2f, 0.3f, 0.92736185f}};
    mvfxForce sources[10];
    int32_t sourceCount = 0;
    for (mvfxGridBorder border = 0; border <= mvfx_borderRepeat; ++border)
    {
        for (int32_t r = 0; r < 3; ++r)
        {
            sources[sourceCount++] =
                (mvfxForce){.kind = mvfx_forceVectorField,
                            .point = {0.25f, -1, 2},
                            .strength = -1.5f,
                            .boxSize = {2, 3, 1.5f},
                            .boxRotation = {turns[r][0], turns[r][1], turns[r][2], turns[r][3]},
                            .border = border};
        }
    }
    sources[sourceCount++] =
        (mvfxForce){.kind = mvfx_forceTurbulence,
                    .strength = 2.0f,
                    .noise = {.frequency = 1.0f, .octaves = 2, .lacunarity = 2.0f, .gain = 0.5f}};
    for (int32_t s = 0; s < sourceCount; ++s)
    {
        mvfxForce compiled = mvfxCompileForce(&sources[s]);
        bool box = sources[s].kind == mvfx_forceVectorField;
        for (int32_t m = 0; m < 3; ++m)
        {
            mvfxResolvedForce force = mvfxResolveForce(&compiled, &maps[m], zero, 0, grid);
            CHECK(!box || (force.unturned == (sources[s].boxRotation[2] == 0.0f) &&
                           force.mapUnturned == (m < 2)),
                  "the quick paths chosen");
            for (int32_t i = 0; i < 9 * 9 * 9 + 7 * 7 * 7; ++i)
            {
                float x[3];
                bool lattice = i < 729;
                for (int32_t k = 0, r = lattice ? i : i - 729; k < 3; ++k)
                {
                    x[k] = lattice ? (float)(r % 9) * 0.625f - 2.5f : wild[r % 7];
                    r /= lattice ? 9 : 7;
                }
                float quick[4];
                float general[4];
                FieldAt(force, true, x, quick);
                FieldAt(force, false, x, general);
                same += memcmp(quick, general, sizeof quick) == 0;
                ++count;
            }
        }
    }
    CHECK(same == count, "the quick paths the general one's bits");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestCurl();
    TestUnturnedBits();
    return s_failures == 0 ? 0 : 1;
}
