// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Grids: their life on a context and every refusal, the limit, new values,
// new values for a box of points, an effect keeping its grid busy, and
// the view of a grid as stored; trilinear sampling, read as a vector
// field's acceleration on a particle at rest, that reproduces a linear
// field, each border, a constant axis and binary16 storage. The sampler
// at places no particle can be put and the points changed since a step
// are tested in test_grid_internal.c.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/grid.h"
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

// A binary16 value as a float, exactly.
static float FromHalf(uint16_t h)
{
    int32_t exponent = (h >> 10) & 0x1F;
    float fraction = (float)(h & 0x3FF);
    float sign = (h & 0x8000) != 0 ? -1.0f : 1.0f;
    return exponent == 0 ? sign * ldexpf(fraction, -24)
                         : sign * ldexpf(1024.0f + fraction, exponent - 25);
}

// A grid's stored value at index i (its components' first is c).
static float Stored(const mvfxContext* context, mvfxGridId grid, int64_t i)
{
    mvfxGridView view;
    CHECK(mvfxGrid_GetView(context, grid, &view) == mvfx_success, "its view");
    return view.format == mvfx_gridFloat16 ? FromHalf(((const uint16_t*)view.values)[i])
                                           : ((const float*)view.values)[i];
}

// A vector field's vector at u, a place in its box's unit coordinates
// (the box of size 1 centered on the origin, so u is the place plus 0.5):
// a particle at rest put there, stepped 2^-10 s with no gravity, its
// velocity over the step, exactly so, an acceleration held over a power of
// two. The grid is free again after.
static void Field(mvfxContext* context, mvfxGridId grid, mvfxGridBorder border, const float u[3],
                  float out[3])
{
    mvfxForce force = {.kind = mvfx_forceVectorField,
                       .strength = 1.0f,
                       .grid = grid,
                       .boxSize = {1, 1, 1},
                       .boxRotation = {0, 0, 0, 1},
                       .border = border};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 1;
    emitter.hostSpawnCapacity = 1;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitter.motion.gravity[0] = 0.0f;
    emitter.motion.gravity[1] = 0.0f;
    emitter.motion.gravity[2] = 0.0f;
    emitter.forces = &force;
    emitter.forceCount = 1;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "a field");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    const double place[3] = {(double)u[0] - 0.5, (double)u[1] - 0.5, (double)u[2] - 0.5};
    static const float rest[3] = {0, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = rest};
    CHECK(mvfxInstance_Spawn(context, instance, 0, &request) == mvfx_success, "put");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 1024) == mvfx_success, "step");
    TestParticle record;
    CHECK(ReadParticles(context, instance, 0, &record, 1) == 1, "read");
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = record.velocity[k] * 1024.0f;
    }
    CHECK(mvfxDestroyInstance(context, instance) == mvfx_success &&
              mvfxDestroyEffect(context, effect) == mvfx_success,
          "done");
}

// A grid of three components, the first of them values (one a point),
// the others 0.
static mvfxGridId FirstOfThree(mvfxContext* context, const int32_t size[3], const float* values,
                               mvfxGridFormat format)
{
    static float three[3 * 64];
    int32_t points = size[0] * size[1] * size[2];
    for (int32_t p = 0; p < points; ++p)
    {
        three[3 * p] = values[p];
        three[3 * p + 1] = 0.0f;
        three[3 * p + 2] = 0.0f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    for (int32_t k = 0; k < 3; ++k)
    {
        def.size[k] = size[k];
    }
    def.components = 3;
    def.format = format;
    def.values = three;
    mvfxGridId id = {0};
    CHECK(mvfxCreateGrid(context, &def, &id) == mvfx_success, "a grid");
    return id;
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
    CHECK(mvfxCreateGrid(context, &def, &c) == mvfx_errorCapacity && c.index1 == 0,
          "the table full");
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
    bool kept = true;
    for (int32_t i = 0; i < 8; ++i)
    {
        kept = kept && Stored(context, a, i) == s_linear[i];
    }
    CHECK(kept, "unchanged by refusals");
    values[3] = 8.0f;
    CHECK(mvfxGrid_SetValues(context, a, values, 8) == mvfx_success, "set");
    bool set = true;
    for (int32_t i = 0; i < 8; ++i)
    {
        set = set && Stored(context, a, i) == values[i];
    }
    CHECK(set, "the new values");
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
        mvfxGridView view;
        CHECK(mvfxGrid_GetView(context, id, &view) == mvfx_success, "its view");
        uint64_t created = view.serial;
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
            right = right && Stored(context, id, 2 * i) == (inside ? After(i, 0) : Before(i, 0)) &&
                    Stored(context, id, 2 * i + 1) == (inside ? After(i, 1) : Before(i, 1));
        }
        CHECK(right, "the box set, the rest kept");
        static const int32_t corner[3] = {3, 0, 1};
        static const int32_t one[3] = {1, 1, 1};
        static const float two[2] = {1, 2};
        CHECK(mvfxGrid_SetValuesIn(context, id, corner, one, two, 2) == mvfx_success, "a point");
        CHECK(mvfxGrid_GetView(context, id, &view) == mvfx_success && view.serial == created + 2 &&
                  Stored(context, id, 2 * 15) == 1.0f && Stored(context, id, 2 * 15 + 1) == 2.0f,
              "a point set, a serial each");
        uint64_t misuse = mvfxGetContextMisuse(context);
        static const int32_t before[3] = {-1, 0, 0};
        static const int32_t past[3] = {3, 0, 0};
        static const int32_t none[3] = {0, 1, 1};
        float nan[8] = {0};
        nan[5] = (float)NAN;
        uint64_t serial = view.serial;
        bool refused =
            mvfxGrid_SetValuesIn(context, id, before, size, box, 8) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, past, size, box, 8) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, first, none, box, 0) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, first, size, box, 6) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, first, size, nan, 8) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, NULL, size, box, 8) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, first, NULL, box, 8) == mvfx_errorInvalid &&
            mvfxGrid_SetValuesIn(context, id, first, size, NULL, 8) == mvfx_errorInvalid;
        CHECK(refused && mvfxGetContextMisuse(context) == misuse + 8 &&
                  mvfxGrid_GetView(context, id, &view) == mvfx_success && view.serial == serial,
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
    static const int32_t cube[3] = {2, 2, 2};
    // s_linear as x + 2 y + 4 z.
    (void)LinearDef(1);
    mvfxGridId id = FirstOfThree(context, cube, s_linear, mvfx_gridFloat32);
    float out[3];
    const float inside[3] = {0.25f, 0.5f, 0.75f};
    Field(context, id, mvfx_borderNone, inside, out);
    CHECK(out[0] == 4.25f && out[1] == 0.0f, "a linear field reproduced");
    const float outside[3] = {-1.0f, 2.0f, 0.5f};
    Field(context, id, mvfx_borderNone, outside, out);
    CHECK(out[0] == 0.0f, "nothing outside");
    Field(context, id, mvfx_borderClamp, outside, out);
    CHECK(out[0] == 4.0f, "clamped");
    const float face[3] = {1.0f, 1.0f, 1.0f};
    Field(context, id, mvfx_borderNone, face, out);
    CHECK(out[0] == 7.0f, "on the far face");
    // Repeat: X's two points 0 and 1 a period, the last joining the first.
    static const float u[4] = {0.75f, 1.25f, -0.25f, 0.5f};
    static const float expected[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    bool tiled = true;
    for (int32_t i = 0; i < 4; ++i)
    {
        const float at[3] = {u[i], 0.0f, 0.0f};
        Field(context, id, mvfx_borderRepeat, at, out);
        tiled = tiled && out[0] == expected[i];
    }
    CHECK(tiled, "tiled");

    // A 2x3x4 grid of x + 10 y + 100 z: each axis's stride its own.
    static float uneven[24];
    for (int32_t p = 0; p < 24; ++p)
    {
        uneven[p] = (float)(p % 2) + 10.0f * (float)((p / 2) % 3) + 100.0f * (float)(p / 6);
    }
    static const int32_t box[3] = {2, 3, 4};
    id = FirstOfThree(context, box, uneven, mvfx_gridFloat32);
    const float at[3] = {0.5f, 0.25f, 0.5f};
    Field(context, id, mvfx_borderNone, at, out);
    CHECK(out[0] == 0.5f + 5.0f + 150.0f, "each axis's stride");

    // One point along an axis: constant along it.
    static const float flat[4] = {1, 2, 3, 4};
    static const int32_t line[3] = {4, 1, 1};
    id = FirstOfThree(context, line, flat, mvfx_gridFloat32);
    const float along[3] = {0.5f, 0.9f, 0.1f};
    Field(context, id, mvfx_borderNone, along, out);
    CHECK(out[0] == 2.5f, "constant across its single points");

    // Binary16: values rounded to nearest, read back exactly.
    static const float thirds[8] = {1.0f / 3.0f, 0, 0, 0, 0, 0, 0, 0};
    id = FirstOfThree(context, cube, thirds, mvfx_gridFloat16);
    const float corner[3] = {0.0f, 0.0f, 0.0f};
    Field(context, id, mvfx_borderNone, corner, out);
    CHECK(out[0] == FromHalf(0x3555u) && out[0] != 1.0f / 3.0f, "kept as binary16");

    // Subnormal halves of both signs and a negative normal one, read back
    // as the floats they are.
    static const float small[3] = {0x3p-24f, -0x5p-24f, -0.75f};
    mvfxGridDef one = mvfxDefaultGridDef();
    one.components = 3;
    one.format = mvfx_gridFloat16;
    one.values = small;
    CHECK(mvfxDestroyGrid(context, id) == mvfx_success &&
              mvfxCreateGrid(context, &one, &id) == mvfx_success,
          "a grid of one point");
    Field(context, id, mvfx_borderNone, corner, out);
    CHECK(out[0] == 0x3p-24f && out[1] == -0x5p-24f && out[2] == -0.75f,
          "subnormal and negative halves read exactly");
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
              ((const uint16_t*)view.values)[3] == 0x3C00u,
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
    TestSetValuesIn();
    TestBusy();
    TestView();
    return s_failures == 0 ? 0 : 1;
}
