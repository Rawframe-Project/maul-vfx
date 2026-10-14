// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The step's arithmetic against slower oracles: births owed by a clock
// against a binary search over birth ticks, death ticks against a binary
// search over the removal's own test, and the life draw past 2^32 births.

#include "context.h"
#include "emitter_step.h"
#include "instance.h"
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

// The births owed by clock: the least k whose birth tick is past it,
// found by bisection over k.
static uint64_t OwedOracle(float rate, int64_t clock)
{
    uint64_t low = 0;
    uint64_t high = 1;
    while (mvfxBirthTick(rate, high) <= clock)
    {
        high *= 2;
    }
    while (low < high)
    {
        uint64_t mid = low + (high - low) / 2;
        if (mvfxBirthTick(rate, mid) <= clock)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }
    return low;
}

// Rates and clocks where the rounded estimate counts one birth short: the
// birth's quotient rounds to exactly the clock, found by a search.
static const struct
{
    float rate;
    int64_t clock;
} s_shortEstimates[] = {
    {0x1.9b7f58p-7f, 687740290641209},  {0x1.639a9cp-7f, 1564666968285868},
    {0x1.49fe3p-6f, 674597030182492},   {0x1.68b06ep-7f, 1202595487219710},
    {0x1.9c51d2p-7f, 1160156928346110},
};

static void TestOwedAgainstBisection(void)
{
    for (int32_t i = 0; i < (int32_t)(sizeof s_shortEstimates / sizeof s_shortEstimates[0]); ++i)
    {
        float rate = s_shortEstimates[i].rate;
        int64_t clock = s_shortEstimates[i].clock;
        CHECK(mvfxOwedBy(rate, clock) == OwedOracle(rate, clock), "a short estimate corrected");
    }
    static const float rates[] = {1.0f, 3.0f, 7.0f, 60.0f, 64.0f, 100.0f, 0.3f, 1e7f};
    for (int32_t i = 0; i < 4000; ++i)
    {
        float rate = i < 8 ? rates[i] : AnyFloat(0.01f, 2e4f);
        // Clocks near birth ticks are where the count steps.
        int64_t clock = (int64_t)(NextCase() % (8u * MVFX_TICKS_PER_SECOND));
        if (i % 3 == 0)
        {
            clock = mvfxBirthTick(rate, NextCase() % 5000u) + (int64_t)(NextCase() % 3u) - 1;
            clock = clock < 0 ? 0 : clock;
        }
        uint64_t owed = mvfxOwedBy(rate, clock);
        if (owed != OwedOracle(rate, clock))
        {
            CHECK(false, "owed matches the bisection");
            return;
        }
    }
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
    uint64_t first = (uint64_t)1 << 32u;
    lateInstance->emitters[0].owed = first;
    lateInstance->clock = mvfxBirthTick(1000.0f, first) - 1;
    int64_t span = mvfxBirthTick(1000.0f, 7);
    CHECK(mvfxContext_StepTicks(early, span) == mvfx_success, "early step");
    int64_t lateSpan = mvfxBirthTick(1000.0f, first + 7) - lateInstance->clock;
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
    TestOwedAgainstBisection();
    TestDeathAgainstBisection();
    TestLivesPastTwoToThe32();
    return s_failures == 0 ? 0 : 1;
}
