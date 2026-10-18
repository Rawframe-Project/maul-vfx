// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Grids: their life on a context and every refusal, the limit, new values,
// trilinear sampling that reproduces a linear field, each border, a
// constant axis, binary16 storage, three components read with the
// general loop's bits, new values for a box of points and
// the points changed since a step, an effect keeping its grid busy, and
// the view of a grid as stored.

#include "context.h"
#include "grid.h"
#include "grid_api.h"
#include "half.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

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

    uint64_t misuse = mvfxGetContextMisuse(context);
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
    CHECK(mvfxGetContextMisuse(context) == misuse + 11, "each counted");
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

    // The context's grid limit is from 1 to 65,536.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* refused = NULL;
    contextDef.limits.grids = 0;
    CHECK(mvfxCreateContext(&contextDef, &refused) == mvfx_errorInvalid, "a limit of 0");
    contextDef.limits.grids = 65537;
    CHECK(mvfxCreateContext(&contextDef, &refused) == mvfx_errorInvalid, "past 65,536");
    contextDef.limits.grids = 65536;
    CHECK(mvfxCreateContext(&contextDef, &refused) == mvfx_success, "65,536");
    mvfxDestroyContext(refused);
}

// The value at point (x, y, z) of a 4 by 3 by 2 grid of two components
// before and after a box is set: 10 and 50 times its index, and the
// component (exact in binary16).
static float Before(int32_t i, int32_t c)
{
    return 10.0f * (float)i + (float)c;
}

static float After(int32_t i, int32_t c)
{
    return 50.0f * (float)i + (float)c;
}

static void TestSetValuesIn(void)
{
    // In both formats: the points from (1, 1, 0), 2 by 2 by 1 of them, set
    // anew and the rest kept; the points so changed since the last step
    // gathered (the union of two boxes set before a step, the first box
    // set after one alone, all of them for new values everywhere); bad
    // boxes, counts and values refused whole and counted.
    for (int32_t format = 0; format < 2; ++format)
    {
        mvfxContext* context = NewContext(4);
        static float values[48];
        for (int32_t i = 0; i < 24; ++i)
        {
            values[2 * i] = Before(i, 0);
            values[2 * i + 1] = Before(i, 1);
        }
        mvfxGridDef def = mvfxDefaultGridDef();
        def.size[0] = 4;
        def.size[1] = 3;
        def.size[2] = 2;
        def.components = 2;
        def.format = (mvfxGridFormat)format;
        def.values = values;
        mvfxGridId id = {0};
        CHECK(mvfxCreateGrid(context, &def, &id) == mvfx_success, "created");
        const mvfxGrid* grid = mvfxContextGrid(context, id);
        uint64_t created = grid->serial;
        static const int32_t first[3] = {1, 1, 0};
        static const int32_t size[3] = {2, 2, 1};
        float box[8];
        for (int32_t y = 0; y < 2; ++y)
        {
            for (int32_t x = 0; x < 2; ++x)
            {
                int32_t i = (1 + y) * 4 + 1 + x;
                box[2 * (y * 2 + x)] = After(i, 0);
                box[2 * (y * 2 + x) + 1] = After(i, 1);
            }
        }
        CHECK(mvfxGrid_SetValuesIn(context, id, first, size, box, 8) == mvfx_success, "set");
        bool right = true;
        for (int32_t i = 0; i < 24; ++i)
        {
            int32_t x = i % 4;
            int32_t y = i / 4 % 3;
            int32_t z = i / 12;
            bool inside = x >= 1 && x <= 2 && y >= 1 && y <= 2 && z == 0;
            const float u[3] = {(float)x / 3.0f, (float)y / 2.0f, (float)z};
            float out[4];
            right = right && mvfxSampleGrid(grid, u, mvfx_borderNone, out) &&
                    out[0] == (inside ? After(i, 0) : Before(i, 0)) &&
                    out[1] == (inside ? After(i, 1) : Before(i, 1));
        }
        CHECK(right, "the box set, the rest kept");
        static const int32_t corner[3] = {3, 0, 1};
        static const int32_t one[3] = {1, 1, 1};
        static const float two[2] = {1, 2};
        CHECK(mvfxGrid_SetValuesIn(context, id, corner, one, two, 2) == mvfx_success, "a point");
        CHECK(grid->since == created && grid->first[0] == 1 && grid->first[1] == 0 &&
                  grid->first[2] == 0 && grid->end[0] == 4 && grid->end[1] == 3 &&
                  grid->end[2] == 2 && grid->serial == created + 2,
              "both boxes changed since the step");
        CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success, "a step");
        uint64_t seen = grid->serial;
        CHECK(mvfxGrid_SetValuesIn(context, id, first, size, box, 8) == mvfx_success, "again");
        CHECK(grid->since == seen && grid->first[0] == 1 && grid->first[1] == 1 &&
                  grid->first[2] == 0 && grid->end[0] == 3 && grid->end[1] == 3 &&
                  grid->end[2] == 1,
              "after a step, the box alone");
        CHECK(mvfxGrid_SetValues(context, id, values, 48) == mvfx_success, "all");
        CHECK(grid->first[0] == 0 && grid->end[0] == 4 && grid->end[1] == 3 && grid->end[2] == 2,
              "all of them");
        uint64_t misuse = mvfxGetContextMisuse(context);
        static const int32_t before[3] = {-1, 0, 0};
        static const int32_t past[3] = {3, 0, 0};
        static const int32_t none[3] = {0, 1, 1};
        float nan[8] = {0};
        nan[5] = (float)NAN;
        uint64_t serial = grid->serial;
        bool refused =
            mvfxGrid_SetValuesIn(context, id, before, size, box, 8) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, past, size, box, 8) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, first, none, box, 0) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, first, size, box, 6) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, first, size, nan, 8) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, NULL, size, box, 8) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, first, NULL, box, 8) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, first, size, NULL, 8) == mvfx_errorInvalid;
        CHECK(refused && mvfxGetContextMisuse(context) == misuse + 8 && grid->serial == serial,
              "bad boxes refused and counted");
        CHECK(mvfxGrid_SetValuesIn(NULL, id, first, size, box, 8) == mvfx_errorInvalid,
              "no context");
        CHECK(mvfxDestroyGrid(context, id) == mvfx_success, "destroyed");
        CHECK(mvfxGrid_SetValuesIn(context, id, first, size, box, 8) == mvfx_errorStale, "stale");
        mvfxDestroyContext(context);
    }
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

    // Places past any range, as a particle flown past the float range
    // brings: every border reads inside the grid (the sanitizers watch).
    static const float wild[6] = {INFINITY, -INFINITY, NAN, 1e30f, -1e30f, 3.4e38f};
    bool held = true;
    for (int32_t i = 0; i < 6; ++i)
    {
        for (mvfxGridBorder border = 0; border <= mvfx_borderRepeat; ++border)
        {
            const float at[3] = {wild[i], wild[(i + 1) % 6], wild[(i + 2) % 6]};
            bool within = mvfxSampleGrid(grid, at, border, out);
            held = held && isfinite(out[0]) && out[0] >= 0.0f && out[0] <= 7.0f &&
                   (border != mvfx_borderNone || !within);
        }
    }
    CHECK(held, "wild places read inside the grid");

    // A 2x3x4 grid of x + 10 y + 100 z: each axis's stride its own.
    static float uneven[24];
    for (int32_t p = 0; p < 24; ++p)
    {
        uneven[p] = (float)(p % 2) + 10.0f * (float)((p / 2) % 3) + 100.0f * (float)(p / 6);
    }
    mvfxGridDef box = mvfxDefaultGridDef();
    box.size[0] = 2;
    box.size[1] = 3;
    box.size[2] = 4;
    box.values = uneven;
    CHECK(mvfxCreateGrid(context, &box, &id) == mvfx_success, "an uneven grid");
    const float at[3] = {0.5f, 0.25f, 0.5f};
    CHECK(mvfxSampleGrid(mvfxContextGrid(context, id), at, mvfx_borderNone, out) &&
              out[0] == 0.5f + 5.0f + 150.0f,
          "each axis's stride");

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

// Three binary32 components (a vector field's) read by their own unrolled
// sum: the same bits as the general loop gives them in a grid of four.
static void TestThreeComponents(void)
{
    mvfxContext* context = NewContext(2);
    static float three[3 * 60];
    static float four[4 * 60];
    for (uint32_t p = 0; p < 60; ++p)
    {
        for (uint32_t c = 0; c < 4; ++c)
        {
            uint32_t i = 4 * p + c;
            float value = i % 7 == 0 ? 0.0f : (float)((i * 2654435761u) >> 8) * 0x1p-23f - 1.0f;
            four[i] = value;
            if (c < 3)
            {
                three[3 * p + c] = value;
            }
        }
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 5;
    def.size[1] = 3;
    def.size[2] = 4;
    def.components = 3;
    def.values = three;
    mvfxGridId threeId = {0};
    mvfxGridId fourId = {0};
    CHECK(mvfxCreateGrid(context, &def, &threeId) == mvfx_success, "three components");
    def.components = 4;
    def.values = four;
    CHECK(mvfxCreateGrid(context, &def, &fourId) == mvfx_success, "four components");
    int32_t same = 0;
    for (mvfxGridBorder border = 0; border <= mvfx_borderRepeat; ++border)
    {
        for (int32_t i = 0; i < 11 * 11 * 11; ++i)
        {
            const float u[3] = {(float)(i % 11) * 0.15f - 0.3f, (float)(i / 11 % 11) * 0.15f - 0.3f,
                                (float)(i / 121) * 0.15f - 0.3f};
            float a[4];
            float b[4];
            bool inA = mvfxSampleGrid(mvfxContextGrid(context, threeId), u, border, a);
            bool inB = mvfxSampleGrid(mvfxContextGrid(context, fourId), u, border, b);
            same += inA == inB && memcmp(a, b, 3 * sizeof a[0]) == 0;
        }
    }
    CHECK(same == 3 * 11 * 11 * 11, "the general loop's bits");
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
    uint64_t misuse = mvfxGetContextMisuse(context);
    CHECK(mvfxCreateEffect(context, &effectDef, &none) == mvfx_errorInvalid &&
              mvfxGetContextMisuse(context) == misuse + 1,
          "a vector field needs three components");
    mvfxDestroyContext(context);
}

static void TestView(void)
{
    // A view: the size, components, format, values as stored and their
    // bytes; a new serial at each change; the refusals, the view zeroed.
    mvfxContext* context = NewContext(4);
    mvfxGridDef def = LinearDef(3);
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(context, &def, &grid) == mvfx_success, "a grid");
    mvfxGridView view;
    CHECK(mvfxGrid_GetView(context, grid, &view) == mvfx_success && view.size[0] == 2 &&
              view.size[1] == 2 && view.size[2] == 2 && view.components == 3 &&
              view.format == mvfx_gridFloat32 && view.bytes == 96 &&
              memcmp(view.values, s_linear, 96) == 0,
          "a view as stored");
    uint64_t serial = view.serial;
    CHECK(mvfxGrid_SetValues(context, grid, s_linear, 24) == mvfx_success &&
              mvfxGrid_GetView(context, grid, &view) == mvfx_success && view.serial != serial,
          "a new serial for new values");
    serial = view.serial;
    static const int32_t first[3] = {0, 0, 0};
    static const int32_t size[3] = {1, 1, 1};
    CHECK(mvfxGrid_SetValuesIn(context, grid, first, size, s_linear, 3) == mvfx_success &&
              mvfxGrid_GetView(context, grid, &view) == mvfx_success && view.serial != serial,
          "a new serial for a box's");
    def.format = mvfx_gridFloat16;
    mvfxGridId half = {0};
    CHECK(mvfxCreateGrid(context, &def, &half) == mvfx_success &&
              mvfxGrid_GetView(context, half, &view) == mvfx_success && view.bytes == 48 &&
              ((const uint16_t*)view.values)[3] == mvfxHalfFromFloat(1.0f),
          "binary16 as stored");
    CHECK(mvfxGrid_GetView(NULL, grid, &view) == mvfx_errorInvalid && view.bytes == 0 &&
              mvfxGrid_GetView(context, grid, NULL) == mvfx_errorInvalid,
          "the view's refusals");
    CHECK(mvfxDestroyGrid(context, grid) == mvfx_success &&
              mvfxGrid_GetView(context, grid, &view) == mvfx_errorStale && view.bytes == 0,
          "a stale grid");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestLife();
    TestSampling();
    TestThreeComponents();
    TestSetValuesIn();
    TestBusy();
    TestView();
    return s_failures == 0 ? 0 : 1;
}
