// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The step's arithmetic against slower oracles: death ticks against a
// binary search over the removal's own test, and the life draw past 2^32
// births.

#include "context.h"
#include "emitter_step.h"
#include "instance.h"
#include "spawn.h"
#include "test_harness.h"

#include <string.h>

// A random number generator for the cases, apart from the library's.
static uint64_t s_state = 0x9E3779B97F4A7C15u;

static uint32_t NextCase(void)
{
    s_state = s_state * 6364136223846793005u + 1442695040888963407u;
    return (uint32_t)(s_state >> 33u);
}

// A float in [low, high), spread over its exponents too.
static float AnyFloat(float low, float high)
{
    float unit = (float)(NextCase() & 0xFFFFFFu) * 0x1p-24f;
    return low + (high - low) * unit * unit;
}

// Whether a particle born at birth with inverse life has ended by tick,
// as the step's removal tests it.
static bool HasEnded(int64_t tick, int64_t birth, float inverseLife)
{
    float age = (float)((double)(tick - birth) * 0x1p-20);
    return age * inverseLife >= 1.0f;
}

// The least tick after birth where it has ended, by bisection.
static int64_t DeathOracle(int64_t birth, float inverseLife)
{
    int64_t low = birth + 1;
    int64_t high = birth + 2;
    while (!HasEnded(high, birth, inverseLife))
    {
        high = birth + (high - birth) * 2;
    }
    while (low < high)
    {
        int64_t mid = low + (high - low) / 2;
        if (HasEnded(mid, birth, inverseLife))
        {
            high = mid;
        }
        else
        {
            low = mid + 1;
        }
    }
    return low;
}

static void TestDeathAgainstBisection(void)
{
    for (int32_t i = 0; i < 20000; ++i)
    {
        // Lives across the whole range, where a float age rounds coarsely
        // past 16 seconds.
        float life = i % 2 == 0 ? AnyFloat(1e-6f, 64.0f) : AnyFloat(1.0f, MVFX_MAX_LIFETIME);
        float inverseLife = 1.0f / life;
        int64_t birth = (int64_t)(NextCase() % 100000u);
        if (mvfxDeathTick(birth, inverseLife) != DeathOracle(birth, inverseLife))
        {
            CHECK(false, "the death tick matches the bisection");
            return;
        }
    }
}

// The instance a context holds in its first slot.
static mvfxInstance* FirstInstance(const mvfxContext* context)
{
    return context->instances.slots[0].object;
}

static mvfxContext* OneInstance(int32_t capacity, float rate, mvfxRange lifetime)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = capacity;
    emitter.rate = rate;
    emitter.lifetime = lifetime;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.seed = 77;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    return context;
}

static void TestLivesPastTwoToThe32(void)
{
    // Particles 2^32 to 2^32 + 7 draw other lives than particles 0 to 7,
    // though their serials' low words agree.
    mvfxRange lives = {1.0f, 100.0f};
    mvfxContext* early = OneInstance(64, 1000.0f, lives);
    mvfxContext* late = OneInstance(64, 1000.0f, lives);
    mvfxInstance* lateInstance = FirstInstance(late);
    const mvfxCompiledEmitter* emitter = &lateInstance->effect->emitters[0];
    uint64_t first = (uint64_t)1 << 32u;
    lateInstance->emitters[0].owed = first;
    lateInstance->emitters[0].rate.next = first;
    lateInstance->clock = mvfxRateBirthTick(emitter, 0, first) - 1;
    int64_t span = mvfxRateBirthTick(emitter, 0, 7);
    CHECK(mvfxContext_StepTicks(early, span) == mvfx_success, "early step");
    int64_t lateSpan = mvfxRateBirthTick(emitter, 0, first + 7) - lateInstance->clock;
    CHECK(mvfxContext_StepTicks(late, lateSpan) == mvfx_success, "late step");
    const mvfxInstance* earlyInstance = FirstInstance(early);
    CHECK(earlyInstance->emitters[0].count == 8 && lateInstance->emitters[0].count == 8,
          "eight each");
    const float* earlyLives = mvfxAttributeArray(earlyInstance, 0, mvfx_attributeInverseLife);
    const float* lateLives = mvfxAttributeArray(lateInstance, 0, mvfx_attributeInverseLife);
    const uint32_t* earlySerials = mvfxAttributeArray(earlyInstance, 0, mvfx_attributeSerial);
    const uint32_t* lateSerials = mvfxAttributeArray(lateInstance, 0, mvfx_attributeSerial);
    CHECK(memcmp(earlySerials, lateSerials, 8 * sizeof(uint32_t)) == 0, "the same low words");
    CHECK(memcmp(earlyLives, lateLives, 8 * sizeof(float)) != 0, "other lives");
    mvfxDestroyContext(early);
    mvfxDestroyContext(late);
}

int main(void)
{
    TestDeathAgainstBisection();
    TestLivesPastTwoToThe32();
    return s_failures == 0 ? 0 : 1;
}
