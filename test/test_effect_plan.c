// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The compiled form: values copied, capacities padded, attribute arrays
// aligned and apart.

#include "effect.h"
#include "test_harness.h"

// The attributes each emitter below stores: the always present 0 to 9;
// size 10 to 12; color 13 to 16; angle and spin 17 and 18; orientation and
// angular velocity 19 to 25; frame 26; a drag multiplier 27; custom from
// 28.
static const uint64_t s_expected[6] = {
    0x3FFu,
    0x3FFu | 0x1C00u,
    0x3FFu | 0x1E000u | 0x60000u,
    0x3FFu | 0x3F80000u | 0x4000000u,
    0x3FFu | ((uint64_t)0x7u << 28u) | ((uint64_t)0x1u << 32u),
    0x3FFu | 0x1C00u | 0x1E000u | 0x60000u | 0x3F80000u | 0x4000000u | ((uint64_t)0x1u << 27u) |
        ((uint64_t)0x7u << 28u) | ((uint64_t)0x1u << 32u),
};

static void TestPlans(void)
{
    static const int32_t capacities[] = {1, 15, 16, 17, 1000, MVFX_MAX_EMITTER_CAPACITY};
    mvfxEmitterDef emitters[6];
    for (int32_t i = 0; i < 6; ++i)
    {
        emitters[i] = mvfxDefaultEmitterDef();
        emitters[i].capacity = capacities[i];
        emitters[i].rate = (float)i * 3.0f;
        emitters[i].lifetime = mvfxRangeSource(0.5f, 2.0f + (float)i);
    }
    // Each optional value stored only when set: none on the first; size on
    // the second; color and a sprite's spin (with its angle) on the third;
    // an angular velocity (with its orientation) and a frame on the fourth;
    // two custom attributes of 3 and 1 on the fifth; everything, with a
    // drag multiplier, on the last.
    mvfxVectorSource set = {.kind = mvfx_sourceRange};
    mvfxCustomAttribute custom[2] = {{.components = 3}, {.components = 1, .initial = set}};
    emitters[1].size = set;
    emitters[2].color = set;
    emitters[2].spin = mvfxRangeSource(0.0f, 1.0f);
    emitters[3].angularVelocity = set;
    emitters[3].frame = (mvfxFrameStart){mvfx_frameFixed, 0, 0};
    emitters[4].custom = custom;
    emitters[4].customCount = 2;
    emitters[5].size = set;
    emitters[5].color = set;
    emitters[5].angle = mvfxRangeSource(0.0f, 1.0f);
    emitters[5].orientationMode = mvfx_orientationRandom;
    emitters[5].frame = (mvfxFrameStart){mvfx_frameRandom, 2, 9};
    emitters[5].motion.dragScale = mvfxRangeSource(0.5f, 2.0f);
    emitters[5].custom = custom;
    emitters[5].customCount = 2;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 6;
    CHECK(mvfxIsEffectDefValid(&def), "valid");
    mvfxAllocator allocator = {0};
    struct mvfxMesh* meshes[MVFX_MAX_EMITTERS] = {0};
    mvfxEffect* effect = mvfxCompileEffect(&def, meshes, &allocator);
    CHECK(effect != NULL && effect->emitterCount == 6, "compiled");
    for (int32_t i = 0; i < 6; ++i)
    {
        const mvfxCompiledEmitter* e = &effect->emitters[i];
        CHECK(e->capacity == capacities[i], "capacity copied");
        CHECK(e->paddedCapacity % MVFX_ATTRIBUTE_PADDING == 0 && e->paddedCapacity >= e->capacity &&
                  e->paddedCapacity < e->capacity + MVFX_ATTRIBUTE_PADDING,
              "padded to the next multiple of 16");
        CHECK(e->rate == (float)i * 3.0f && e->lifetime.max == 2.0f + (float)i, "values copied");
        CHECK(e->plan.present[0] == s_expected[i] && e->plan.present[1] == 0,
              "the attributes the emitter uses");
        size_t arrayBytes = (size_t)e->paddedCapacity * sizeof(float);
        int32_t stored = 0;
        for (int32_t a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
        {
            if (!mvfxHasAttribute(&e->plan, a))
            {
                continue;
            }
            ++stored;
            CHECK(e->plan.offsets[a] % MVFX_ATTRIBUTE_ALIGNMENT == 0, "each array aligned");
            CHECK(e->plan.offsets[a] + arrayBytes <= e->plan.blockBytes, "inside the block");
            for (int32_t b = a + 1; b < MVFX_ATTRIBUTE_COUNT; ++b)
            {
                if (!mvfxHasAttribute(&e->plan, b))
                {
                    continue;
                }
                bool apart = e->plan.offsets[a] + arrayBytes <= e->plan.offsets[b] ||
                             e->plan.offsets[b] + arrayBytes <= e->plan.offsets[a];
                CHECK(apart, "arrays do not overlap");
            }
        }
        CHECK(e->plan.blockBytes == (size_t)stored * arrayBytes, "no room for the unused");
    }
    mvfxReleaseEffect(effect, &allocator);
}

int main(void)
{
    TestPlans();
    return s_failures == 0 ? 0 : 1;
}
