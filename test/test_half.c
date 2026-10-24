// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// binary32 to binary16 where a host meets it, against the oracle of
// half_reference.h: a binary16 grid's stored values on a prime stride of
// floats and every float near a half's value or halfway between two, and
// its edge cases; past 65,504, where a grid refuses, a record's binary16
// fields, which round to the largest half below 65,520 and to infinity
// from it. test_half_sweep checks every float a grid takes.

#include "half_reference.h"

#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

static HalfGrid s_grid;

static void TestEdgeCases(void)
{
    static const struct
    {
        float value;
        uint16_t half;
    } cases[] = {
        {0.0f, 0x0000u},       {-0.0f, 0x8000u},        {1.0f, 0x3C00u},
        {-2.0f, 0xC000u},      {65504.0f, 0x7BFFu},     {-65504.0f, 0xFBFFu},
        {0x1p-14f, 0x0400u},   {0x1p-24f, 0x0001u},     {0x1p-25f, 0x0000u},
        {0x1.8p-25f, 0x0001u}, {0x1.8p-24f, 0x0002u},   {0x1.002p0f, 0x3C00u},
        {0x1.006p0f, 0x3C02u}, {0x1.ffep-15f, 0x0400u}, {1e-30f, 0x0000u},
    };
    OpenHalfGrid(&s_grid);
    bool same = true;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        same = same && ReferenceHalf(cases[i].value) == cases[i].half;
        uint32_t bits = 0;
        memcpy(&bits, &cases[i].value, sizeof bits);
        CHECK(QueueHalf(&s_grid, bits), "taken by a grid");
    }
    FlushHalfGrid(&s_grid);
    CHECK(same, "the oracle's edge cases");
    CHECK(s_grid.differing == 0, "the grid's edge cases");
}

static void TestFloats(void)
{
    s_grid.checked = 0;
    s_grid.differing = 0;
    for (uint64_t bits = 0; bits <= 0xFFFFFFFFu; bits += 65521u)
    {
        (void)QueueHalf(&s_grid, (uint32_t)bits);
    }
    // Every float within 16 ulps of a half's value or of a point halfway
    // between two halves, of both signs, that a grid takes.
    for (uint32_t half = 0; half < 0x7C00u; ++half)
    {
        for (int32_t sign = 0; sign < 2; ++sign)
        {
            float lower = 0;
            uint32_t low = (half << 13u) + 0x38000000u;
            if (half < 0x0400u)
            {
                lower = (float)half * 0x1p-24f;
                memcpy(&low, &lower, sizeof low);
            }
            for (int32_t offset = -16; offset <= 16; ++offset)
            {
                uint32_t near = (uint32_t)((int64_t)low + offset) & 0x7FFFFFFFu;
                (void)QueueHalf(&s_grid, near | (sign != 0 ? 0x80000000u : 0u));
                uint32_t middle = low + (half < 0x0400u ? 0u : 0x1000u);
                (void)QueueHalf(&s_grid, ((uint32_t)((int64_t)middle + offset) & 0x7FFFFFFFu) |
                                             (sign != 0 ? 0x80000000u : 0u));
            }
        }
    }
    CloseHalfGrid(&s_grid);
    CHECK(s_grid.checked > 4000000 && s_grid.differing == 0, "every float the oracle's half");
}

// Past what a grid takes: values a custom attribute starts with, written
// as binary16 record fields.
static void TestPastTheLargestHalf(void)
{
    static const float values[8] = {65504.0f,  0x1.ffdfep15f, 65519.99f, 65520.0f,
                                    -65520.0f, 1e30f,         -1e30f,    0x1.ffep15f};
    static const uint16_t halves[8] = {0x7BFFu, 0x7BFFu, 0x7BFFu, 0x7C00u,
                                       0xFC00u, 0x7C00u, 0xFC00u, 0x7C00u};
    mvfxCustomAttribute custom[2];
    for (int32_t k = 0; k < 2; ++k)
    {
        custom[k] = (mvfxCustomAttribute){.components = 4, .initial = {.kind = mvfx_sourceRange}};
        for (int32_t c = 0; c < 4; ++c)
        {
            custom[k].initial.min[c] = values[4 * k + c];
            custom[k].initial.max[c] = values[4 * k + c];
        }
    }
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.custom = custom;
    emitter.customCount = 2;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "stepped");
    const mvfxLayoutField fields[2] = {
        {.field = mvfx_recordCustom, .format = mvfx_formatFloat16, .components = 4, .offset = 0},
        {.field = mvfx_recordCustom,
         .format = mvfx_formatFloat16,
         .components = 4,
         .custom = 1,
         .offset = 8}};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = fields;
    layoutDef.fieldCount = 2;
    layoutDef.stride = 16;
    mvfxLayoutId layout = {0};
    CHECK(mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success, "layout");
    static uint16_t records[64][8];
    mvfxView view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};
    int64_t count = 0;
    CHECK(mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, records, 64,
                                      &count) == mvfx_success &&
              count > 0,
          "written");
    bool same = true;
    for (int64_t i = 0; i < count; ++i)
    {
        for (int32_t c = 0; c < 8; ++c)
        {
            same = same && records[i][c] == halves[c] && ReferenceHalf(values[c]) == halves[c];
        }
    }
    CHECK(same, "the largest half below 65,520, infinity from it");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestEdgeCases();
    TestFloats();
    TestPastTheLargestHalf();
    return s_failures == 0 ? 0 : 1;
}
