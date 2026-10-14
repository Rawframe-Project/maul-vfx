// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Grids: their life on a context and every refusal, the limit, new values,
// trilinear sampling that reproduces a linear field, each border, a
// constant axis, binary16 storage, and an effect keeping its grid busy.

#include "context.h"
#include "grid.h"
#include "grid_api.h"
#include "half.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"

#include <math.h>

static mvfxContext* NewContext(int32_t grids)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    def.limits.grids = grids;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    return context;
}

// A 2x2x2 grid whose values are each point's coordinates (x + 2 y + 4 z
// for one component).
static float s_linear[24];

static mvfxGridDef LinearDef(int32_t components)
{
    for (int32_t p = 0; p < 8; ++p)
    {
        float x = (float)(p & 1);
        float y = (float)((p >> 1) & 1);
        float z = (float)((p >> 2) & 1);
        if (components == 1)
        {
            s_linear[p] = x + 2.0f * y + 4.0f * z;
        }
        else
        {
            s_linear[3 * p] = x;
            s_linear[3 * p + 1] = y;
            s_linear[3 * p + 2] = z;
        }
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 2;
    def.size[1] = 2;
    def.size[2] = 2;
    def.components = components;
    def.values = s_linear;
    return def;
}

static void TestLife(void)
{
    mvfxContext* context = NewContext(2);
    mvfxGridDef def = LinearDef(1);
    mvfxGridId a = {0};
    mvfxGridId b = {0};
    mvfxGridId c = {7, 7};
    CHECK(mvfxCreateGrid(context, &def, &a) == mvfx_success && mvfxGrid_IsValid(context, a),
          "created");
    CHECK(mvfxCreateGrid(context, &def, &b) == mvfx_success, "a second");
    CHECK(mvfxCreateGrid(context, &def, &c) == mvfx_errorLimit && c.index1 == 0, "the limit");
    CHECK(mvfxDestroyGrid(context, b) == mvfx_success && !mvfxGrid_IsValid(context, b),
          "destroyed");
    CHECK(mvfxDestroyGrid(context, b) == mvfx_errorStale, "stale");
    CHECK(mvfxDestroyGrid(NULL, a) == mvfx_errorInvalid, "no context");
    CHECK(!mvfxGrid_IsValid(NULL, a), "no context, not valid");

    uint64_t misuse = mvfxContext_GetMisuse(context);
    static const float nan[1] = {(float)NAN};
    static const float big[1] = {70000.0f};
    for (int32_t field = 0; field < 11; ++field)
    {
        mvfxGridDef bad = mvfxDefaultGridDef();
        static const float one[1] = {1.0f};
        bad.values = one;
        switch (field)
        {
        case 0:
            bad.cookie = 0;
            break;
        case 1:
            bad.size[1] = 0;
            break;
        case 2:
            bad.size[2] = MVFX_MAX_GRID_SIZE + 1;
            break;
        case 3:
            bad.size[0] = MVFX_MAX_GRID_SIZE;
            bad.size[1] = MVFX_MAX_GRID_SIZE;
            bad.size[2] = 2;
            break;
        case 4:
            bad.components = 0;
            break;
        case 5:
            bad.components = 5;
            break;
        case 6:
            bad.format = 2;
            break;
        case 7:
            bad.values = NULL;
            break;
        case 8:
            bad.values = nan;
            break;
        case 9:
            bad.format = mvfx_gridFloat16;
            bad.values = big;
            break;
        default:
            CHECK(mvfxCreateGrid(context, NULL, &c) == mvfx_errorInvalid, "no def");
            continue;
        }
        CHECK(mvfxCreateGrid(context, &bad, &c) == mvfx_errorInvalid && c.index1 == 0, "refused");
    }
    CHECK(mvfxContext_GetMisuse(context) == misuse + 11, "each counted");
    CHECK(mvfxCreateGrid(context, &def, NULL) == mvfx_errorInvalid, "no out-parameter");
    CHECK(mvfxCreateGrid(NULL, &def, &c) == mvfx_errorInvalid, "no context");

    // New values: refused whole when any is bad or the count is not the
    // grid's, else read by the next sample.
    float values[8] = {0};
    values[3] = (float)NAN;
    CHECK(mvfxGrid_SetValues(context, a, values, 8) == mvfx_errorInvalid, "a NaN");
    CHECK(mvfxGrid_SetValues(context, a, s_linear, 7) == mvfx_errorInvalid, "a short count");
    CHECK(mvfxGrid_SetValues(context, a, NULL, 8) == mvfx_errorInvalid, "no values");
    CHECK(mvfxGrid_SetValues(context, b, s_linear, 8) == mvfx_errorStale, "stale");
    CHECK(mvfxGrid_SetValues(NULL, a, s_linear, 8) == mvfx_errorInvalid, "no context");
    const mvfxGrid* live = mvfxContextGrid(context, a);
    float out[4];
    const float middle[3] = {0.5f, 0.5f, 0.5f};
    CHECK(mvfxSampleGrid(live, middle, mvfx_borderNone, out) && out[0] == 3.5f,
          "unchanged by refusals");
    values[3] = 8.0f;
    CHECK(mvfxGrid_SetValues(context, a, values, 8) == mvfx_success, "set");
    CHECK(mvfxSampleGrid(live, middle, mvfx_borderNone, out) && out[0] == 1.0f, "the new values");
    mvfxDestroyContext(context);
}

static void TestSampling(void)
{
    mvfxContext* context = NewContext(4);
    mvfxGridDef def = LinearDef(1);
    mvfxGridId id = {0};
    CHECK(mvfxCreateGrid(context, &def, &id) == mvfx_success, "created");
    const mvfxGrid* grid = mvfxContextGrid(context, id);
    float out[4];
    const float inside[3] = {0.25f, 0.5f, 0.75f};
    CHECK(mvfxSampleGrid(grid, inside, mvfx_borderNone, out) && out[0] == 4.25f && out[1] == 0.0f,
          "a linear field reproduced");
    const float outside[3] = {-1.0f, 2.0f, 0.5f};
    CHECK(!mvfxSampleGrid(grid, outside, mvfx_borderNone, out) && out[0] == 0.0f,
          "nothing outside");
    CHECK(mvfxSampleGrid(grid, outside, mvfx_borderClamp, out) && out[0] == 4.0f, "clamped");
    const float face[3] = {1.0f, 1.0f, 1.0f};
    CHECK(mvfxSampleGrid(grid, face, mvfx_borderNone, out) && out[0] == 7.0f, "on the far face");
    // Repeat: X's two points 0 and 1 a period, the last joining the first.
    static const float u[4] = {0.75f, 1.25f, -0.25f, 0.5f};
    static const float expected[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    bool tiled = true;
    for (int32_t i = 0; i < 4; ++i)
    {
        const float at[3] = {u[i], 0.0f, 0.0f};
        tiled = tiled && mvfxSampleGrid(grid, at, mvfx_borderRepeat, out) && out[0] == expected[i];
    }
    CHECK(tiled, "tiled");

    // One point along an axis: constant along it.
    static const float flat[4] = {1, 2, 3, 4};
    mvfxGridDef line = mvfxDefaultGridDef();
    line.size[0] = 4;
    line.values = flat;
    CHECK(mvfxCreateGrid(context, &line, &id) == mvfx_success, "a line");
    const float along[3] = {0.5f, 0.9f, 0.1f};
    CHECK(mvfxSampleGrid(mvfxContextGrid(context, id), along, mvfx_borderNone, out) &&
              out[0] == 2.5f,
          "constant across its single points");

    // Binary16: values rounded to nearest, read back exactly.
    static const float thirds[8] = {1.0f / 3.0f, 0, 0, 0, 0, 0, 0, 0};
    mvfxGridDef half = LinearDef(1);
    half.values = thirds;
    half.format = mvfx_gridFloat16;
    CHECK(mvfxCreateGrid(context, &half, &id) == mvfx_success, "a half grid");
    const float corner[3] = {0.0f, 0.0f, 0.0f};
    CHECK(mvfxSampleGrid(mvfxContextGrid(context, id), corner, mvfx_borderNone, out) &&
              out[0] == mvfxFloatFromHalf(mvfxHalfFromFloat(1.0f / 3.0f)) && out[0] != 1.0f / 3.0f,
          "kept as binary16");
    mvfxDestroyContext(context);
}

static void TestBusy(void)
{
    mvfxContext* context = NewContext(4);
    mvfxGridDef def = LinearDef(3);
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(context, &def, &grid) == mvfx_success, "created");
    mvfxForce force = {.kind = mvfx_forceVectorField,
                       .strength = 1.0f,
                       .grid = grid,
                       .boxSize = {1, 1, 1},
                       .boxRotation = {0, 0, 0, 1}};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.forces = &force;
    emitter.forceCount = 1;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId first = {0};
    mvfxEffectId second = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &first) == mvfx_success, "an effect");
    CHECK(mvfxCreateEffect(context, &effectDef, &second) == mvfx_success, "another");
    CHECK(mvfxDestroyGrid(context, grid) == mvfx_errorBusy, "busy while read");
    CHECK(mvfxDestroyEffect(context, first) == mvfx_success, "one gone");
    CHECK(mvfxDestroyGrid(context, grid) == mvfx_errorBusy, "busy while the other reads");
    CHECK(mvfxDestroyEffect(context, second) == mvfx_success, "both gone");
    CHECK(mvfxDestroyGrid(context, grid) == mvfx_success, "free once unread");

    mvfxEffectId none = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &none) == mvfx_errorStale, "a stale grid");
    def = LinearDef(1);
    CHECK(mvfxCreateGrid(context, &def, &grid) == mvfx_success, "a scalar grid");
    force.grid = grid;
    uint64_t misuse = mvfxContext_GetMisuse(context);
    CHECK(mvfxCreateEffect(context, &effectDef, &none) == mvfx_errorInvalid &&
              mvfxContext_GetMisuse(context) == misuse + 1,
          "a vector field needs three components");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestLife();
    TestSampling();
    TestBusy();
    return s_failures == 0 ? 0 : 1;
}
