// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Lights on a real device (lavapipe on CI) beside the CPU path's
// mvfxInstance_WriteLights, compared after every step (rhi_harness.h):
// the count, each light's serial, place, radius and colour, in the pool's
// order. Covered: every particle and every n-th, alpha scaling, colour
// and size over life under a moving, scaled instance in the world's
// space and the instance's, many blocks, a buffer smaller than the
// count, a serial step past the run's, an emitter without lights, and
// every malformed def refused.
// Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

#include "maul-rhi/resources.h"

#include <math.h>

static const mvfxCurveKey s_keys[2] = {
    {.time = 0, .value = 1, .interpolation = mvfx_interpolationLinear},
    {.time = 1, .value = 0.25f, .interpolation = mvfx_interpolationLinear}};
static const mvfxColorKey s_colors[2] = {{0, 1, 0.5f, 0}, {1, 0, 0.25f, 1}};
static const mvfxAlphaKey s_alphas[2] = {{0, 1}, {1, 0.5f}};

// A sprite carrying a light every n-th particle, its colour and size
// drawn and changed over its life.
static mvfxEmitterDef Lit(int32_t every, bool alphaScales)
{
    const mvfxCurve curve = {.keys = s_keys, .keyCount = 2};
    mvfxEmitterDef emitter = Sprite();
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.2f, 0.3f}, .max = {0.4f, 0.5f, 0.6f}};
    emitter.color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.2f, 0.2f, 0.2f, 1}, .max = {1, 1, 1, 1}};
    emitter.overLife = (mvfxOverLife){
        .color = {.enabled = true,
                  .input = mvfx_overAge,
                  .gradient = {.colorKeys = s_colors,
                               .colorKeyCount = 2,
                               .alphaKeys = s_alphas,
                               .alphaKeyCount = 2},
                  .scaleAlpha = true,
                  .alpha = {.value = 1, .curve = curve}},
        .size = {
            .enabled = true,
            .perAxis = true,
            .size = {{.value = 2, .curve = curve}, {.value = 1}, {.value = 3, .curve = curve}}}};
    emitter.light = (mvfxLightDef){.enabled = true,
                                   .every = every,
                                   .intensity = 2.5f,
                                   .alphaScales = alphaScales,
                                   .radius = 0.5f,
                                   .radiusBySize = 1.5f};
    return emitter;
}

static void TestEvery(Gpu* gpu, mvfxGpu* kernels)
{
    // Every particle in the world's space under a moving, scaled instance;
    // then every third with alpha scaling, in the instance's space.
    mvfxEmitterDef emitter = Lit(1, false);
    emitter.space = mvfx_spaceWorld;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .pace = 0.1,
                   .lights = true,
                   .recordCapacity = CAPACITY,
                   .leastLights = 100},
            "a light every particle, in the world's space");
    emitter = Lit(3, true);
    emitter.space = mvfx_spaceLocal;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .lights = true,
                   .recordCapacity = CAPACITY,
                   .leastLights = 30},
            "a light every third particle, alpha scaling, in the instance's space");
    // A step past every serial of the run: serial 0 alone carries one.
    emitter = Lit(65536, false);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .lights = true,
                   .recordCapacity = CAPACITY,
                   .leastLights = 1},
            "a light every 65,536th particle");
}

static void TestBlocks(Gpu* gpu, mvfxGpu* kernels)
{
    // Thousands live, so many blocks of 1,024, every other one lit; then a
    // buffer of five lights: the first five written, all counted.
    mvfxEmitterDef emitter = Lit(2, true);
    emitter.capacity = 8192;
    emitter.rate = 20000.0f;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .steps = 24,
                   .least = 3000,
                   .lights = true,
                   .recordCapacity = 8192,
                   .leastLights = 1500},
            "lights over many blocks");
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .steps = 12,
                   .least = 2000,
                   .lights = true,
                   .recordCapacity = 5,
                   .leastLights = 1000},
            "five lights of thousands");
}

static void TestNone(Gpu* gpu, mvfxGpu* kernels)
{
    // An emitter without lights: a count of 0 every step.
    mvfxEmitterDef emitter = Sprite();
    Compare(
        gpu, kernels,
        &(Run){.emitters = &emitter, .emitterCount = 1, .lights = true, .recordCapacity = CAPACITY},
        "an emitter without lights");
}

// The malformed lights defs: def 0 is valid, each after breaks one rule.
#define BAD_DEFS 9

static mvfxGpuLightsDef LightsDef(mrhiResourceId buffer, int32_t bad)
{
    mvfxGpuLightsDef def = mvfxDefaultGpuLightsDef();
    def.lights = buffer;
    def.capacity = 8;
    def.count = buffer;
    def.countOffset = 1000;
    switch (bad)
    {
    case 1:
        def.cookie = 0;
        break;
    case 2:
        def.emitter = -1;
        break;
    case 3:
        def.emitter = 1;
        break;
    case 4:
        def.lights = (mrhiResourceId){0};
        break;
    case 5:
        def.offset = 2;
        break;
    case 6:
        def.countOffset = 1002;
        break;
    case 7:
        def.capacity = -1;
        break;
    case 8:
        def.view.origin[1] = 0x1p53;
        break;
    case 9:
        def.view.origin[2] = (double)NAN;
        break;
    default:
        break;
    }
    return def;
}

// Opens a run of one emitter and counts the lights defs from..to-1
// refused as invalid in a frame.
static int32_t Refused(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, int32_t from,
                       int32_t to)
{
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(kernels, &(Run){.emitters = emitter, .emitterCount = 1}, &effect);
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 1024;
    bufferDef.usage = mrhi_bufferStorage;
    mrhiBufferId buffer = {0};
    mrhiResourceId resource = {0};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(sim != NULL && mrhiCreateBuffer(gpu->device, &bufferDef, &buffer) == mrhi_success &&
              mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success &&
              mrhiImportBuffer(gpu->device, buffer, &resource) == mrhi_success,
          "a buffer in a frame");
    int32_t refused = 0;
    for (int32_t i = from; i < to; ++i)
    {
        const mvfxGpuLightsDef def = LightsDef(resource, i);
        refused += mvfxGpuSim_AddLights(sim, &def) == mvfx_errorInvalid ? 1 : 0;
    }
    const mvfxGpuLightsDef valid = LightsDef(resource, 0);
    CHECK(mvfxGpuSim_AddLights(NULL, &valid) == mvfx_errorInvalid &&
              mvfxGpuSim_AddLights(sim, NULL) == mvfx_errorInvalid,
          "NULL refused");
    CHECK(mrhiDevice_DropFrame(gpu->device) == mrhi_success, "dropped");
    (void)mrhiDestroyBuffer(gpu->device, buffer);
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
    return refused;
}

static void TestRefused(Gpu* gpu, mvfxGpu* kernels)
{
    // Each malformed def refused; the valid def taken.
    mvfxEmitterDef emitter = Lit(1, false);
    CHECK(Refused(gpu, kernels, &emitter, 1, BAD_DEFS + 1) == BAD_DEFS, "lights defs refused");
    CHECK(Refused(gpu, kernels, &emitter, 0, 1) == 0, "the valid def taken");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestEvery(gpu, kernels);
    TestBlocks(gpu, kernels);
    TestNone(gpu, kernels);
    TestRefused(gpu, kernels);
}

int main(void)
{
    return RunSuite(Suite, &s_failures);
}
