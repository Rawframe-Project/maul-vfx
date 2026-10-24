// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The grid module's own sampler and bookkeeping where no public call
// reaches: places past any range (a particle flown past the float range
// brings them; no particle can be put there), three components read by
// their own unrolled sum with the general loop's bits (a vector field reads
// three, nothing public reads the general loop's four alike), and the box
// of points changed since a step, which a moving grid collider sweeps.
// Sampling by vector fields and the stored values are tested in
// test_grid.c.

#include "context.h"
#include "grid.h"
#include "grid_api.h"
#include "test_harness.h"

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

// A 2x2x2 grid of one component, each point's x + 2 y + 4 z.
static float s_linear[8];

static void TestWild(void)
{
    for (int32_t p = 0; p < 8; ++p)
    {
        s_linear[p] = (float)(p & 1) + 2.0f * (float)((p >> 1) & 1) + 4.0f * (float)((p >> 2) & 1);
    }
    mvfxContext* context = NewContext(1);
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 2;
    def.size[1] = 2;
    def.size[2] = 2;
    def.values = s_linear;
    mvfxGridId id = {0};
    CHECK(mvfxCreateGrid(context, &def, &id) == mvfx_success, "created");
    const mvfxGrid* grid = mvfxContextGrid(context, id);
    float out[4];
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

// The points changed since the last step, in both formats: the union of
// two boxes set before a step, the first box set after one alone, all of
// them for new values everywhere.
static void TestChangedSince(void)
{
    for (int32_t format = 0; format < 2; ++format)
    {
        mvfxContext* context = NewContext(1);
        static float values[48];
        for (int32_t i = 0; i < 48; ++i)
        {
            values[i] = (float)i;
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
        static const float box[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        CHECK(mvfxGrid_SetValuesIn(context, id, first, size, box, 8) == mvfx_success, "set");
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
        mvfxDestroyContext(context);
    }
}

int main(void)
{
    TestWild();
    TestThreeComponents();
    TestChangedSince();
    return s_failures == 0 ? 0 : 1;
}
