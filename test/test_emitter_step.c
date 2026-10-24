// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Lives as a host sees them: each particle's last step against a binary
// search over the removal's test, from lives below a tick to lives where a
// float age rounds coarsely, and the life draw past 2^32 births (reached
// with the test hook that sets an emitter's births owed).

#define PEEK_MOST 256

#include "particles.h"
#include "peek.h"
#include "test_harness.h"
#include "test_hooks.h"

#include "maul-vfx/instance.h"

#include <stdlib.h>
#include <string.h>

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

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    int64_t clock;
} Scene;

// An instance of one emitter that births only what the host spawns, its
// lives drawn from a range, every one peeked, its deaths written as
// events.
static Scene HostBirths(float shortest, float longest)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEmitterDef base = mvfxDefaultEmitterDef();
    base.capacity = PEEK_MOST;
    base.hostSpawnCapacity = PEEK_MOST;
    base.rate = 0.0f;
    base.lifetime = mvfxRangeSource(shortest, longest);
    base.events = mvfx_eventMaskDeath;
    base.eventCapacity = PEEK_MOST;
    mvfxEmitterDef emitter = Peeking(&base);
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.seed = 77;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

static void Spawn(const Scene* scene, int32_t count)
{
    static const double places[PEEK_MOST][3] = {{0}};
    mvfxSpawnRequest request = {.count = count, .positions = &places[0][0]};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "spawned");
}

// Each serial's death events so far and the last one's tick.
static int32_t s_deaths[PEEK_MOST];
static int64_t s_deathTicks[PEEK_MOST];

// Steps a scene to a tick, at most a second a step, gathering its deaths.
static void StepTo(Scene* scene, int64_t tick)
{
    while (scene->clock < tick)
    {
        int64_t step = tick - scene->clock;
        step = step < MVFX_TICKS_PER_SECOND ? step : MVFX_TICKS_PER_SECOND;
        CHECK(mvfxContext_StepTicks(scene->context, step) == mvfx_success, "step");
        scene->clock += step;
        const mvfxEvent* events = NULL;
        int32_t count = 0;
        CHECK(mvfxInstance_GetEvents(scene->context, scene->instance, 0, &events, &count) ==
                  mvfx_success,
              "events");
        for (int32_t e = 0; e < count; ++e)
        {
            uint32_t serial = events[e].serial % PEEK_MOST;
            s_deaths[serial] += events[e].kind == mvfx_eventDeath && !events[e].killed ? 1 : 2;
            s_deathTicks[serial] = events[e].tick;
        }
    }
}

static int CompareTicks(const void* a, const void* b)
{
    int64_t x = *(const int64_t*)a;
    int64_t y = *(const int64_t*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static void TestDeathAgainstBisection(void)
{
    // Lives below a tick, below a second, of seconds, and past 16 s where
    // a float age rounds coarsely: at each particle's death tick and the
    // one before, the ones alive are those the bisection has not ended;
    // each death's event at that tick.
    static const float ranges[4][2] = {
        {1e-7f, 1e-4f}, {1e-4f, 1.0f}, {1.0f, 64.0f}, {16.0f, 4096.0f}};
    for (int32_t r = 0; r < 4; ++r)
    {
        Scene scene = HostBirths(ranges[r][0], ranges[r][1]);
        memset(s_deaths, 0, sizeof s_deaths);
        Spawn(&scene, PEEK_MOST);
        StepTo(&scene, 1);
        // The peek again each step: keep the first's.
        static Peeked first;
        first = s_peeked;
        int32_t peeked = first.count;
        static int64_t deaths[PEEK_MOST];
        static int64_t ticks[2 * PEEK_MOST];
        bool born = peeked > PEEK_MOST / 2;
        for (int32_t i = 0; i < peeked; ++i)
        {
            uint32_t serial = first.serial[i];
            born = born && serial < PEEK_MOST && first.birth[i] == 0;
            deaths[serial % PEEK_MOST] = DeathOracle(first.birth[i], first.inverseLife[i]);
            ticks[2 * i] = deaths[serial % PEEK_MOST] - 1;
            ticks[2 * i + 1] = deaths[serial % PEEK_MOST];
        }
        CHECK(born, "born and peeked");
        qsort(ticks, (size_t)(2 * peeked), sizeof ticks[0], CompareTicks);
        bool matches = true;
        int32_t checked = 0;
        for (int32_t k = 0; matches && k <= 2 * peeked; ++k)
        {
            if (k < 2 * peeked)
            {
                if (ticks[k] < scene.clock || (k > 0 && ticks[k] == ticks[k - 1]))
                {
                    continue;
                }
                StepTo(&scene, ticks[k]);
            }
            TestParticle records[PEEK_MOST];
            int32_t count = ReadParticles(scene.context, scene.instance, 0, records, PEEK_MOST);
            bool alive[PEEK_MOST] = {false};
            for (int32_t j = 0; j < count; ++j)
            {
                alive[records[j].serial % PEEK_MOST] = true;
            }
            int32_t expected = 0;
            for (int32_t i = 0; i < peeked; ++i)
            {
                uint32_t serial = first.serial[i];
                bool due = deaths[serial] > scene.clock;
                expected += due ? 1 : 0;
                matches = matches && alive[serial] == due;
            }
            matches = matches && count == expected;
            checked += 1;
        }
        CHECK(matches && checked > 32, "the last step against the bisection");
        bool told = true;
        for (int32_t i = 0; i < peeked; ++i)
        {
            uint32_t serial = first.serial[i];
            told = told && s_deaths[serial] == 1 && s_deathTicks[serial] == deaths[serial];
        }
        CHECK(told, "each death's event at its tick");
        mvfxDestroyContext(scene.context);
    }
}

static void TestLivesPastTwoToThe32(void)
{
    // Particles 2^32 to 2^32 + 7 draw other lives than particles 0 to 7,
    // though their serials' low words agree.
    Scene early = HostBirths(1.0f, 100.0f);
    Spawn(&early, 8);
    StepTo(&early, 1);
    Peeked first = s_peeked;
    Scene late = HostBirths(1.0f, 100.0f);
    CHECK(mvfxTestSetOwed(late.context, late.instance, 0, (uint64_t)1 << 32u), "past 2^32");
    Spawn(&late, 8);
    StepTo(&late, 1);
    bool same = first.count == 8 && s_peeked.count == 8;
    int32_t alike = 0;
    for (int32_t i = 0; same && i < 8; ++i)
    {
        same = first.serial[i] == (uint32_t)i && s_peeked.serial[i] == (uint32_t)i;
        alike += first.inverseLife[i] == s_peeked.inverseLife[i] ? 1 : 0;
    }
    CHECK(same, "eight each, the same low words");
    CHECK(alike == 0, "other lives");
    mvfxDestroyContext(early.context);
    mvfxDestroyContext(late.context);
}

int main(void)
{
    TestDeathAgainstBisection();
    TestLivesPastTwoToThe32();
    return s_failures == 0 ? 0 : 1;
}
