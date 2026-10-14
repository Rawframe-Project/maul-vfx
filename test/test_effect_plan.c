// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The compiled form: values copied, capacities padded, attribute arrays
// aligned and apart.

#include "effect.h"
#include "test_harness.h"

static void TestPlans(void)
{
    static const int32_t capacities[] = {1, 15, 16, 17, 1000, MVFX_MAX_EMITTER_CAPACITY};
    mvfxEmitterDef emitters[6];
    for (int32_t i = 0; i < 6; ++i)
    {
        emitters[i] = mvfxDefaultEmitterDef();
        emitters[i].capacity = capacities[i];
        emitters[i].rate = (float)i * 3.0f;
        emitters[i].lifetime = (mvfxRange){0.5f, 2.0f + (float)i};
    }
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 6;
    CHECK(mvfxIsEffectDefValid(&def), "valid");
    mvfxAllocator allocator = {0};
    mvfxEffect* effect = mvfxCompileEffect(&def, &allocator);
    CHECK(effect != NULL && effect->emitterCount == 6, "compiled");
    for (int32_t i = 0; i < 6; ++i)
    {
        const mvfxCompiledEmitter* e = &effect->emitters[i];
        CHECK(e->capacity == capacities[i], "capacity copied");
        CHECK(e->paddedCapacity % MVFX_ATTRIBUTE_PADDING == 0 && e->paddedCapacity >= e->capacity &&
                  e->paddedCapacity < e->capacity + MVFX_ATTRIBUTE_PADDING,
              "padded to the next multiple of 16");
        CHECK(e->rate == (float)i * 3.0f && e->lifetime.max == 2.0f + (float)i, "values copied");
        CHECK(e->plan.present == (1u << MVFX_ATTRIBUTE_COUNT) - 1u, "the fixed attributes");
        size_t arrayBytes = (size_t)e->paddedCapacity * sizeof(float);
        for (int32_t a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
        {
            CHECK(e->plan.offsets[a] % MVFX_ATTRIBUTE_ALIGNMENT == 0, "each array aligned");
            CHECK(e->plan.offsets[a] + arrayBytes <= e->plan.blockBytes, "inside the block");
            for (int32_t b = a + 1; b < MVFX_ATTRIBUTE_COUNT; ++b)
            {
                bool apart = e->plan.offsets[a] + arrayBytes <= e->plan.offsets[b] ||
                             e->plan.offsets[b] + arrayBytes <= e->plan.offsets[a];
                CHECK(apart, "arrays do not overlap");
            }
        }
    }
    mvfxReleaseEffect(effect, &allocator);
}

int main(void)
{
    TestPlans();
    return s_failures == 0 ? 0 : 1;
}
