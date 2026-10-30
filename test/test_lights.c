// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Lights: every n-th particle by serial carries one, the same particles
// from step to step as others die; its place relative to a view, its
// colour over life times the intensity (and the alpha where asked), its
// radius plus its largest size; a local emitter's in the world under a
// scaled instance; a buffer too small, the count alone, an emitter
// without lights; the def's checks and the call's refusals.

#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static Scene NewScene(const mvfxEmitterDef* emitters, int32_t count)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    Scene scene = {0};
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = count;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

static mvfxEmitterDef Lit(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 16;
    emitter.hostSpawnCapacity = 16;
    emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
    emitter.color = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                       .min = {1.0f, 0.5f, 0.25f, 0.5f},
                                       .max = {1.0f, 0.5f, 0.25f, 0.5f}};
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.5f, 2.0f, 1.0f, 0}, .max = {0.5f, 2.0f, 1.0f, 0}};
    emitter.light = (mvfxLightDef){
        .enabled = true, .every = 3, .intensity = 4.0f, .radius = 1.0f, .radiusBySize = 0.5f};
    return emitter;
}

// Spawns count particles at x = 10 + i, with lives from shortest.
static void Spawn(Scene* scene, int32_t count)
{
    double places[16 * 3] = {0};
    for (int32_t i = 0; i < count; ++i)
    {
        places[3 * i] = 10.0 + i;
    }
    mvfxSpawnRequest request = {.count = count, .positions = places};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "spawned");
}

static void TestLights(void)
{
    mvfxEmitterDef emitter = Lit();
    Scene scene = NewScene(&emitter, 1);
    Spawn(&scene, 8);
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    static const mvfxView view = {.origin = {10.0, -1.0, 0.0}};
    mvfxLight lights[4] = {0};
    int64_t count = 0;
    CHECK(mvfxInstance_WriteLights(scene.context, scene.instance, 0, &view, lights, 4, &count) ==
                  mvfx_success &&
              count == 3,
          "serials 0, 3 and 6");
    CHECK(lights[0].serial == 0 && lights[1].serial == 3 && lights[2].serial == 6 &&
              lights[1].position[0] == 3.0f && lights[1].position[1] == 1.0f &&
              lights[1].position[2] == 0.0f,
          "relative to the view");
    CHECK(lights[0].color[0] == 4.0f && lights[0].color[1] == 2.0f && lights[0].color[2] == 1.0f &&
              lights[0].radius == 2.0f,
          "colour times intensity; radius plus half the largest size");
    // A buffer of two takes the first; none, the count.
    mvfxLight few[2] = {0};
    CHECK(mvfxInstance_WriteLights(scene.context, scene.instance, 0, &view, few, 2, &count) ==
                  mvfx_success &&
              count == 3 && few[1].serial == 3,
          "a buffer too small");
    CHECK(mvfxInstance_WriteLights(scene.context, scene.instance, 0, &view, NULL, 0, &count) ==
                  mvfx_success &&
              count == 3,
          "the count alone");
    mvfxDestroyContext(scene.context);
}

static void TestStable(void)
{
    // Two groups of a quarter second's life an eighth apart: when the
    // first has died, the lights stay on the same serials of the second.
    mvfxEmitterDef emitter = Lit();
    emitter.lifetime = mvfxRangeSource(0.25f, 0.25f);
    emitter.light.alphaScales = true;
    Scene scene = NewScene(&emitter, 1);
    Spawn(&scene, 4);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "step");
    Spawn(&scene, 6);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 6) == mvfx_success,
          "the first four die");
    static const mvfxView view = {.origin = {0, 0, 0}};
    mvfxLight lights[4] = {0};
    int64_t count = 0;
    CHECK(mvfxInstance_WriteLights(scene.context, scene.instance, 0, &view, lights, 4, &count) ==
                  mvfx_success &&
              count == 2 && lights[0].serial == 6 && lights[1].serial == 9,
          "the same serials carry them");
    CHECK(lights[0].color[0] == 2.0f && lights[0].color[2] == 0.5f, "times the alpha");
    mvfxDestroyContext(scene.context);
}

static void TestLocal(void)
{
    // A local emitter under an instance at (100, 0, 0) at scale 2: a
    // particle at (1, 0, 0) of largest size 2 is at (102, 0, 0) with a
    // radius of 1 + 0.5 * 4.
    mvfxEmitterDef emitter = Lit();
    emitter.space = mvfx_spaceLocal;
    Scene scene = NewScene(&emitter, 1);
    static const double origin[3] = {100.0, 0.0, 0.0};
    static const float identity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, identity, 2.0f, true) ==
              mvfx_success,
          "placed");
    static const double place[3] = {1.0, 0.0, 0.0};
    mvfxSpawnRequest request = {.count = 1, .positions = place};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success,
          "spawned");
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    static const mvfxView view = {.origin = {0, 0, 0}};
    mvfxLight light = {0};
    int64_t count = 0;
    CHECK(mvfxInstance_WriteLights(scene.context, scene.instance, 0, &view, &light, 1, &count) ==
                  mvfx_success &&
              count == 1 && fabsf(light.position[0] - 102.0f) <= 1e-4f &&
              fabsf(light.radius - 3.0f) <= 1e-5f,
          "in the world");
    mvfxDestroyContext(scene.context);
}

static void TestUnlit(void)
{
    // Without lights, or without colour and size: 0, and white of size 1.
    mvfxEmitterDef emitters[2] = {Lit(), Lit()};
    emitters[0].light.enabled = false;
    emitters[1].color.kind = mvfx_sourceNone;
    emitters[1].size.kind = mvfx_sourceNone;
    emitters[1].light.every = 1;
    Scene scene = NewScene(emitters, 2);
    static const double place[3] = {0.0, 0.0, 0.0};
    mvfxSpawnRequest request = {.count = 1, .positions = place};
    CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success &&
              mvfxInstance_Spawn(scene.context, scene.instance, 1, &request) == mvfx_success,
          "spawned");
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    static const mvfxView view = {.origin = {0, 0, 0}};
    mvfxLight light = {0};
    int64_t count = -1;
    CHECK(mvfxInstance_WriteLights(scene.context, scene.instance, 0, &view, &light, 1, &count) ==
                  mvfx_success &&
              count == 0,
          "no lights");
    CHECK(mvfxInstance_WriteLights(scene.context, scene.instance, 1, &view, &light, 1, &count) ==
                  mvfx_success &&
              count == 1 && light.color[0] == 4.0f && light.color[2] == 4.0f &&
              light.radius == 1.5f,
          "white, size 1");
    mvfxDestroyContext(scene.context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Lit();
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    int32_t refusals = 0;
    for (int32_t rule = 0; rule < 8; ++rule)
    {
        emitter.light = Lit().light;
        switch (rule)
        {
        case 0:
            emitter.light.every = 0;
            break;
        case 1:
            emitter.light.every = 65537;
            break;
        case 2:
            emitter.light.intensity = -1.0f;
            break;
        case 3:
            emitter.light.intensity = (float)NAN;
            break;
        case 4:
            emitter.light.radius = -0.5f;
            break;
        case 5:
            emitter.light.radius = 2e6f;
            break;
        case 6:
            emitter.light.radiusBySize = -0.5f;
            break;
        default:
            emitter.light.radiusBySize = (float)NAN;
            break;
        }
        // Past a named bound: mvfx_errorLimit.
        mvfxResult want = rule == 5 ? mvfx_errorLimit : mvfx_errorInvalid;
        refusals += mvfxCreateEffect(context, &def, &effect) == want ? 1 : 0;
    }
    CHECK(refusals == 8, "every check");
    emitter.light = (mvfxLightDef){.every = 0, .intensity = -1.0f};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "ignored when off");
    emitter.light = (mvfxLightDef){
        .enabled = true, .every = 65536, .intensity = 1e6f, .radius = 1e6f, .radiusBySize = 1e6f};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "the largest");
    mvfxDestroyContext(context);
}

// Whether every byte of a buffer is value.
static bool Filled(const void* buffer, size_t size, unsigned char value)
{
    const unsigned char* bytes = buffer;
    for (size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != value)
        {
            return false;
        }
    }
    return true;
}

static void TestRefusals(void)
{
    mvfxEmitterDef emitter = Lit();
    Scene scene = NewScene(&emitter, 1);
    mvfxContext* c = scene.context;
    mvfxInstanceId i = scene.instance;
    static const mvfxView view = {.origin = {0, 0, 0}};
    static const mvfxView far = {.origin = {0, -0x1p53, 0}};
    mvfxLight lights[1];
    int64_t count = 0;
    uint64_t misuse = mvfxGetContextMisuse(c);
    CHECK(mvfxInstance_WriteLights(c, i, 1, &view, lights, 1, &count) == mvfx_errorInvalid &&
              mvfxInstance_WriteLights(c, i, 0, NULL, lights, 1, &count) == mvfx_errorInvalid &&
              mvfxInstance_WriteLights(c, i, 0, &view, lights, 1, NULL) == mvfx_errorInvalid &&
              mvfxInstance_WriteLights(c, i, 0, &view, lights, -1, &count) == mvfx_errorInvalid &&
              mvfxInstance_WriteLights(c, i, 0, &view, NULL, 1, &count) == mvfx_errorInvalid &&
              mvfxInstance_WriteLights(c, i, 0, &far, lights, 1, &count) == mvfx_errorInvalid,
          "refused");
    CHECK(mvfxGetContextMisuse(c) == misuse + 6, "each counted");
    mvfxInstanceId stale = i;
    stale.generation += 1;
    CHECK(mvfxInstance_WriteLights(c, stale, 0, &view, lights, 1, &count) == mvfx_errorStale &&
              mvfxInstance_WriteLights(NULL, i, 0, &view, lights, 1, &count) == mvfx_errorInvalid,
          "stale, no context");
    // What a refusal leaves: the count 0 and the lights zeroed.
    mvfxLight held[2];
    memset(held, 0xEE, sizeof held);
    count = 7;
    CHECK(mvfxInstance_WriteLights(NULL, i, 0, &view, held, 2, &count) == mvfx_errorInvalid &&
              count == 0 && Filled(held, sizeof held, 0),
          "no context: the count 0, the lights zeroed");
    memset(held, 0xEE, sizeof held);
    count = 7;
    CHECK(mvfxInstance_WriteLights(c, stale, 0, &view, held, 2, &count) == mvfx_errorStale &&
              count == 0 && Filled(held, sizeof held, 0),
          "a stale instance: the lights zeroed");
    mvfxDestroyContext(c);
}

int main(void)
{
    TestLights();
    TestStable();
    TestLocal();
    TestUnlit();
    TestChecks();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
