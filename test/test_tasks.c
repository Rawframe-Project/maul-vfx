// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The host's task hooks through the main step: a gas stepped on hosts that
// split the work in other ways, run the ranges last first or first last,
// each range under a thread's hostile float setting, and return a handle
// or NULL, gives the bits it gives serially. Every enqueue hands at least
// one item in ranges of at least one, finish gets the handle enqueue
// returned, once, and no NULL handle is waited on.

#include "test_fenv.h"
#include "test_harness.h"

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"

enum
{
    SIDE = 32,
    STEPS = 20
};

// A host's task system: it runs the ranges on the calling thread as a
// worker would, under the hostile setting, giving the thread its own back
// after.
typedef struct Host
{
    // Ranges last first, else first last.
    bool reverse;
    // Each range minRange items and then 0, 1 or 2 more, else minRange.
    bool uneven;
    // Returns NULL, as a host that ran every range before returning may.
    bool returnNull;
    int32_t enqueued;
    int32_t finished;
    // Enqueues of no items or ranges under one, and finishes of another
    // handle than the last enqueue's.
    int32_t wrong;
    // The handle the last enqueue returned, until it is finished.
    void* pending;
} Host;

static char s_handles[8];

// Cuts [0, itemCount) into the host's ranges: their starts, in order.
static int32_t Cut(const Host* host, int32_t itemCount, int32_t minRange, int32_t* starts,
                   int32_t most)
{
    int32_t count = 0;
    for (int32_t start = 0; start < itemCount && count < most; ++count)
    {
        starts[count] = start;
        start += minRange + (host->uneven ? count % 3 : 0);
    }
    return count;
}

static void* Enqueue(mvfxTaskFunction* task, int32_t itemCount, int32_t minRange, void* taskContext,
                     void* userContext)
{
    Host* host = userContext;
    if (itemCount < 1 || minRange < 1)
    {
        host->wrong += 1;
        return NULL;
    }
    enum
    {
        MOST = 4096
    };
    static int32_t starts[MOST];
    int32_t count = Cut(host, itemCount, minRange, starts, MOST);
    TestFloatSetting thread = TestFloatGet();
    for (int32_t k = 0; k < count; ++k)
    {
        int32_t r = host->reverse ? count - 1 - k : k;
        int32_t end = r + 1 < count ? starts[r + 1] : itemCount;
        TestFloatSet(TestFloatHostile());
        task(starts[r], end, taskContext);
        TestFloatSet(thread);
    }
    host->pending = host->returnNull ? NULL : &s_handles[host->enqueued % 8];
    host->enqueued += 1;
    return host->pending;
}

static void Finish(void* userTask, void* userContext)
{
    Host* host = userContext;
    host->wrong += userTask == NULL || userTask != host->pending ? 1 : 0;
    host->pending = NULL;
    host->finished += 1;
}

// A hash of a burning smoke gas's density and temperature after every
// step, stepped on host's hooks, or serially for NULL.
static uint64_t Run(Host* host)
{
    static const mvfxGasSource source = {.shape = mvfx_gasBox,
                                         .center = {-0.6f, -1.0f, 0.0f},
                                         .halfExtents = {0.3f, 0.2f, 1.0f},
                                         .density = 1.0f,
                                         .temperature = 3.0f,
                                         .fuel = 1.0f};
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = SIDE;
    gas.size[1] = SIDE;
    gas.space = mvfx_gasWorld;
    gas.sources = &source;
    gas.sourceCount = 1;
    gas.heatLift = 0.7f;
    gas.vorticity = 1.5f;
    gas.advection = mvfx_gasMacCormack;
    gas.ignitionTemperature = 1.0f;
    gas.burnRate = 2.0f;
    gas.burnHeat = 3.0f;
    gas.burnExpansion = 0.3f;
    gas.densityDecay = 0.1f;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    if (host != NULL)
    {
        contextDef.enqueueTask = Enqueue;
        contextDef.finishTask = Finish;
        contextDef.userTaskContext = host;
    }
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    uint64_t hash = 0;
    static float density[SIDE * SIDE];
    static float temperature[SIDE * SIDE];
    for (int32_t s = 0; s < STEPS; ++s)
    {
        CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success, "step");
        CHECK(mvfxInstance_ReadGas(context, instance, 0, mvfx_gasDensity, density, SIDE * SIDE) ==
                      mvfx_success &&
                  mvfxInstance_ReadGas(context, instance, 0, mvfx_gasTemperature, temperature,
                                       SIDE * SIDE) == mvfx_success,
              "read");
        hash = mvfxHash64(hash, density, sizeof density);
        hash = mvfxHash64(hash, temperature, sizeof temperature);
    }
    mvfxDestroyContext(context);
    return hash;
}

static void TestHosts(void)
{
    uint64_t serial = Run(NULL);
    Host reverse = {.reverse = true};
    CHECK(Run(&reverse) == serial, "ranges last first, under a hostile setting: the serial bits");
    CHECK(reverse.enqueued > 0 && reverse.finished == reverse.enqueued && reverse.wrong == 0,
          "each handle finished once");
    Host uneven = {.uneven = true, .returnNull = true};
    CHECK(Run(&uneven) == serial, "uneven ranges first last: the serial bits");
    CHECK(uneven.enqueued > 0 && uneven.finished == 0 && uneven.wrong == 0,
          "no NULL handle finished");
}

int main(void)
{
    TestHosts();
    return s_failures == 0 ? 0 : 1;
}
