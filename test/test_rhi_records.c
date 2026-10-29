// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Records of planned instances written on a real device (lavapipe on CI)
// beside the CPU path's, compared after every step (rhi_harness.h): every
// field and format, values exact on both paths, the orders (youngest
// first, sorted by place); and the records defs the part refuses. Skips
// (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "emitter_step.h"
#include "half.h"
#include "instance.h"
#include "instance_api.h"
#include "rhi_harness.h"
#include "test_harness.h"

#include "kernels/depth_fill_container.h"
#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void TestRecords(Gpu* gpu, mvfxGpu* kernels)
{
    // Records of every field the GPU writes, in every format, with gaps no
    // field covers: a world-space emitter under a moving instance, its
    // values over life (a colour by a gradient at the speed with alpha
    // over life, a size an axis at the age, two custom values scaled by
    // curves of their own), a flipbook over life; then in the instance's
    // space, frames at a rate,
    // the buffer holding fewer records than the live particles.
    static const mvfxCurveKey keys[2] = {
        {.time = 0, .value = 1, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 0.25f, .interpolation = mvfx_interpolationLinear}};
    static const mvfxColorKey colors[2] = {{0, 1, 0.5f, 0}, {1, 0, 0.25f, 1}};
    static const mvfxAlphaKey alphas[2] = {{0, 1}, {1, 0.5f}};
    const mvfxCurve curve = {.keys = keys, .keyCount = 2};
    static const mvfxCurveKey rising[2] = {
        {.time = 0, .value = 0, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 3, .interpolation = mvfx_interpolationLinear}};
    const mvfxCustomAttribute custom[2] = {
        {.components = 2,
         .initial = {.kind = mvfx_sourceRange, .min = {-1, 0}, .max = {1, 3}},
         .scaledOverLife = true,
         .overLife = {.value = 2, .curve = curve}},
        {.components = 1,
         .initial = {.kind = mvfx_sourceRange, .min = {0.5f}, .max = {1}},
         .scaledOverLife = true,
         .overLife = {.value = 1, .curve = {.keys = rising, .keyCount = 2}}}};
    static const mvfxLayoutField fields[14] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordSize, .format = mvfx_formatFloat16, .components = 3, .offset = 12},
        {.field = mvfx_recordColor,
         .format = mvfx_formatUnorm8,
         .components = 4,
         .srgb = true,
         .offset = 20},
        {.field = mvfx_recordAngle, .format = mvfx_formatSnorm16, .components = 1, .offset = 24},
        {.field = mvfx_recordOrientation,
         .format = mvfx_formatSnorm8,
         .components = 4,
         .offset = 28},
        {.field = mvfx_recordVelocity, .format = mvfx_formatFloat32, .components = 3, .offset = 32},
        {.field = mvfx_recordAge, .format = mvfx_formatUnorm16, .components = 1, .offset = 44},
        {.field = mvfx_recordFrame, .format = mvfx_formatUint32, .components = 1, .offset = 48},
        {.field = mvfx_recordCustom, .format = mvfx_formatFloat32, .components = 2, .offset = 52},
        {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 60},
        {.field = mvfx_recordMesh, .format = mvfx_formatFloat32, .components = 1, .offset = 64},
        {.field = mvfx_recordColor, .format = mvfx_formatUnorm16, .components = 3, .offset = 68},
        {.field = mvfx_recordFrame, .format = mvfx_formatFloat32, .components = 1, .offset = 76},
        {.field = mvfx_recordCustom,
         .format = mvfx_formatFloat32,
         .components = 1,
         .custom = 1,
         .offset = 84}};
    mvfxLayoutDef layout = mvfxDefaultLayoutDef();
    layout.fields = fields;
    layout.fieldCount = 14;
    layout.stride = 88;
    mvfxEmitterDef emitter = Sprite();
    emitter.space = mvfx_spaceWorld;
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.2f, 0.3f}, .max = {0.4f, 0.5f, 0.6f}};
    emitter.color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.2f, 0.2f, 0.2f, 1}, .max = {1, 1, 1, 1}};
    emitter.angle = (mvfxScalarSource){.kind = mvfx_sourceRange, .range = {-0.5f, 0.5f}};
    emitter.spin = (mvfxScalarSource){.kind = mvfx_sourceRange, .range = {-0.2f, 0.2f}};
    emitter.orientationMode = mvfx_orientationRandom;
    emitter.frame = (mvfxFrameStart){.mode = mvfx_frameRandom, .first = 0, .last = 3};
    emitter.meshIndex = (mvfxFrameStart){.mode = mvfx_frameRandom, .first = 1, .last = 7};
    emitter.custom = custom;
    emitter.customCount = 2;
    emitter.overLife = (mvfxOverLife){
        .color = {.enabled = true,
                  .input = mvfx_overSpeed,
                  .speedRange = {0.5f, 4},
                  .gradient = {.colorKeys = colors,
                               .colorKeyCount = 2,
                               .alphaKeys = alphas,
                               .alphaKeyCount = 2},
                  .scaleAlpha = true,
                  .alpha = {.value = 1, .curve = curve}},
        .size = {.enabled = true,
                 .perAxis = true,
                 .size = {{.value = 2, .curve = curve},
                          {.value = 1},
                          {.value = 3, .curve = curve}}},
        .frame = {.animation = mvfx_frameOverLife, .frameCount = 16, .cycles = 2.5f, .loop = true}};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .pace = 0.1,
                   .layout = &layout,
                   .recordCapacity = CAPACITY},
            "records of every field, in the world's space");
    emitter.space = mvfx_spaceLocal;
    emitter.overLife.frame = (mvfxFrameOverLife){
        .animation = mvfx_frameAtRate, .frameCount = 12, .frameRate = 30, .loop = false};
    emitter.overLife.color.input = mvfx_overAge;
    emitter.overLife.size.input = mvfx_overSpeed;
    emitter.overLife.size.speedRange[0] = 1;
    emitter.overLife.size.speedRange[1] = 3;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .layout = &layout,
                   .recordCapacity = 60},
            "records in the instance's space, fewer than the particles");
}

static void TestExactRecords(Gpu* gpu, mvfxGpu* kernels)
{
    // Records whose values are the same binary32 on both paths (particles
    // at rest, values drawn at birth, none over life), so every word must
    // be the CPU path's: colours past 1, a channel at one of the CPU path's
    // sRGB steps, sizes at a half float's ties, angles and orientations in
    // snorm, custom values past [0, 1] in unorm, the age, a byte field
    // ending inside its word before a gap.
    float steps[MVFX_SRGB_STEPS];
    mvfxSrgbSteps(steps);
    static const mvfxLayoutField fields[8] = {
        {.field = mvfx_recordColor, .format = mvfx_formatUnorm8, .components = 3},
        {.field = mvfx_recordColor,
         .format = mvfx_formatUnorm8,
         .components = 4,
         .srgb = true,
         .offset = 4},
        {.field = mvfx_recordSize, .format = mvfx_formatFloat16, .components = 3, .offset = 8},
        {.field = mvfx_recordAngle, .format = mvfx_formatSnorm16, .components = 1, .offset = 16},
        {.field = mvfx_recordOrientation,
         .format = mvfx_formatSnorm8,
         .components = 4,
         .offset = 20},
        {.field = mvfx_recordCustom, .format = mvfx_formatUnorm16, .components = 2, .offset = 24},
        {.field = mvfx_recordAge, .format = mvfx_formatUnorm16, .components = 1, .offset = 28},
        {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 32}};
    mvfxLayoutDef layout = mvfxDefaultLayoutDef();
    layout.fields = fields;
    layout.fieldCount = 8;
    layout.stride = 36;
    const mvfxCustomAttribute custom[1] = {
        {.components = 2,
         .initial = {.kind = mvfx_sourceRange, .min = {-0.5f, 0}, .max = {1.5f, 1}}}};
    mvfxEmitterDef emitter = Sprite();
    emitter.velocity.speed = (mvfxRange){0, 0};
    emitter.motion.gravity[1] = 0;
    emitter.color = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                       .min = {0, steps[99], 0.5f, 0},
                                       .max = {1.5f, steps[99], 1.25f, 1}};
    // 1 + 3 2^-11 and 3 + 3 2^-10 halfway between half floats, the lower
    // odd: rounded to even, up.
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                      .min = {1.00146484375f, 3.0029296875f, 0.1f},
                                      .max = {1.00146484375f, 3.0029296875f, 0.7f}};
    emitter.angle = (mvfxScalarSource){.kind = mvfx_sourceRange, .range = {-1.5f, 1.5f}};
    emitter.orientationMode = mvfx_orientationRandom;
    emitter.custom = custom;
    emitter.customCount = 1;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .layout = &layout,
                   .recordCapacity = CAPACITY,
                   .exact = true},
            "records exact on both paths");
}

static void TestSortedRecords(Gpu* gpu, mvfxGpu* kernels)
{
    // Records youngest first; back to front by distance in the world's
    // space over blocks of the sort's (thousands of particles in three
    // blocks, all live at times, so chunks of the digit counts' scan end
    // at each block); front to back by depth in the instance's space from
    // an eye amid the particles (depths below 0 and above), cut at fewer
    // records than particles; and keys all tied (every particle at one
    // place), which keep the pool's order exactly, back to front.
    static const mvfxLayoutField fields[5] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 12},
        {.field = mvfx_recordColor,
         .format = mvfx_formatUnorm8,
         .components = 4,
         .srgb = true,
         .offset = 16},
        {.field = mvfx_recordSize, .format = mvfx_formatFloat16, .components = 2, .offset = 20},
        {.field = mvfx_recordAge, .format = mvfx_formatUnorm16, .components = 1, .offset = 24}};
    mvfxLayoutDef layout = mvfxDefaultLayoutDef();
    layout.fields = fields;
    layout.fieldCount = 5;
    layout.stride = 28;
    mvfxEmitterDef emitter = Sprite();
    emitter.color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.2f, 0.2f, 0.2f, 1}, .max = {1, 1, 1, 1}};
    emitter.sortMode = mvfx_sortYoungestFirst;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .layout = &layout,
                   .recordCapacity = CAPACITY},
            "records youngest first");
    emitter.capacity = 3000;
    emitter.rate = 9000.0f;
    emitter.space = mvfx_spaceWorld;
    emitter.sortMode = mvfx_sortBackToFront;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .least = 2100,
                   .moving = true,
                   .pace = 0.1,
                   .layout = &layout,
                   .recordCapacity = 3000},
            "records back to front by distance, over blocks");
    emitter = Sprite();
    emitter.sortMode = mvfx_sortFrontToBack;
    emitter.sortMeasure = mvfx_sortByDepth;
    const mvfxView amid = {
        .origin = {3.5, -1.25, 2.0}, .eye = {0, 0.3, 0}, .forward = {0, 0, -1}, .up = {0, 1, 0}};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .layout = &layout,
                   .recordCapacity = 60,
                   .view = &amid},
            "records front to back by depth, cut");
    emitter.shape.radius = 0;
    emitter.velocity.speed = (mvfxRange){0, 0};
    emitter.motion.gravity[1] = 0;
    emitter.sortMode = mvfx_sortBackToFront;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .layout = &layout,
                   .recordCapacity = CAPACITY,
                   .tied = true},
            "records of tied keys in the pool's order");
}

// A records def of a resource and layout, the rest valid.
static mvfxGpuRecordsDef RecordsDef(mrhiResourceId records, mvfxLayoutId layout)
{
    mvfxGpuRecordsDef def = mvfxDefaultGpuRecordsDef();
    def.layout = layout;
    def.records = records;
    def.capacity = 8;
    return def;
}

static void TestRecordsRefused(Gpu* gpu, mvfxGpu* kernels)
{
    // A records def refused: a NULL argument, a def without its cookie, an
    // emitter out of range, no buffer, an offset or count offset not a
    // multiple of 4, a negative capacity, a view's origin or eye out of
    // range or its directions not finite, a layout gone, a stride not a
    // multiple of 4 or a ribbon field; past the
    // passes an emitter takes in a frame, its limit. Then an emitter left
    // out at the instance's quality level: a count of 0, no record.
    mvfxEmitterDef emitter = Sprite();
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(kernels, &(Run){.emitters = &emitter, .emitterCount = 1}, &effect);
    static const mvfxLayoutField word = {
        .field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1};
    static const mvfxLayoutField bytes = {
        .field = mvfx_recordColor, .format = mvfx_formatUnorm8, .components = 2};
    static const mvfxLayoutField ribbon = {
        .field = mvfx_recordWidth, .format = mvfx_formatFloat32, .components = 1};
    mvfxLayoutDef def = mvfxDefaultLayoutDef();
    def.fieldCount = 1;
    def.stride = 4;
    mvfxLayoutId good = {0};
    mvfxLayoutId gone = {0};
    mvfxLayoutId uneven = {0};
    mvfxLayoutId width = {0};
    def.fields = &word;
    CHECK(mvfxCreateLayout(RunContext(), &def, &good) == mvfx_success &&
              mvfxCreateLayout(RunContext(), &def, &gone) == mvfx_success &&
              mvfxDestroyLayout(RunContext(), gone) == mvfx_success,
          "layouts");
    def.fields = &bytes;
    def.stride = 2;
    CHECK(mvfxCreateLayout(RunContext(), &def, &uneven) == mvfx_success, "an uneven stride");
    def.fields = &ribbon;
    def.stride = 4;
    CHECK(mvfxCreateLayout(RunContext(), &def, &width) == mvfx_success, "a ribbon field");
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 1024;
    bufferDef.usage = mrhi_bufferStorage | mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    mrhiResourceId resource = {0};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiCreateBuffer(gpu->device, &bufferDef, &buffer) == mrhi_success &&
              mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success &&
              mrhiImportBuffer(gpu->device, buffer, &resource) == mrhi_success,
          "a buffer");
    mvfxGpuRecordsDef bad[14];
    for (int32_t i = 0; i < 14; ++i)
    {
        bad[i] = RecordsDef(resource, good);
    }
    bad[0].cookie = 0;
    bad[1].emitter = 1;
    bad[2].emitter = -1;
    bad[3].records = (mrhiResourceId){0};
    bad[4].offset = 2;
    bad[5].countOffset = 6;
    bad[6].capacity = -1;
    bad[7].view.origin[1] = 0x1p53;
    bad[8].layout = uneven;
    bad[9].layout = width;
    bad[10].layout = gone;
    bad[11].view.eye[2] = -0x1p53;
    bad[12].view.forward[0] = NAN;
    bad[13].view.up[1] = INFINITY;
    int32_t refused = 0;
    for (int32_t i = 0; i < 14; ++i)
    {
        mvfxResult result = mvfxGpuSim_AddRecords(sim, &bad[i]);
        refused += result == (i == 10 ? mvfx_errorStale : mvfx_errorInvalid) ? 1 : 0;
    }
    const mvfxGpuRecordsDef valid = RecordsDef(resource, good);
    int32_t added = 0;
    for (int32_t p = 0; p < MVFX_GPU_RECORD_PASSES; ++p)
    {
        added += mvfxGpuSim_AddRecords(sim, &valid) == mvfx_success ? 1 : 0;
    }
    CHECK(refused == 14 && mvfxGpuSim_AddRecords(NULL, &valid) == mvfx_errorInvalid &&
              mvfxGpuSim_AddRecords(sim, NULL) == mvfx_errorInvalid &&
              added == MVFX_GPU_RECORD_PASSES &&
              mvfxGpuSim_AddRecords(sim, &valid) == mvfx_errorLimit,
          "records refused");
    CHECK(mrhiDevice_DropFrame(gpu->device) == mrhi_success, "dropped");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());

    // Emitter 0 left out at quality level 1: its count is 0, the fill
    // kept around it.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static const mvfxQualityLevel omitted = {
        .capacityScale = 1.0f, .rateScale = 1.0f, .omitted = 1u};
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    effectDef.qualityLevels = &omitted;
    effectDef.qualityLevelCount = 1;
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.planned = true;
    instanceDef.quality = 1;
    mvfxInstanceId planned = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &planned) == mvfx_success &&
              mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success,
          "a planned instance, stepped");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = planned;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, NULL) == mvfx_success, "a simulation");
    mvfxLayoutId layout = {0};
    def.fields = &word;
    CHECK(mvfxCreateLayout(context, &def, &layout) == mvfx_success, "a layout");
    static const uint32_t fill[4] = {RECORD_FILL * 0x01010101u, RECORD_FILL * 0x01010101u,
                                     RECORD_FILL * 0x01010101u, RECORD_FILL * 0x01010101u};
    mrhiPassId pass = {0};
    mrhiRequestId read = {0};
    bool recorded = mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success &&
                    mrhiImportBuffer(gpu->device, buffer, &resource) == mrhi_success;
    const mrhiAccess written[1] = {{.resource = resource, .kind = mrhi_accessCopyDestination}};
    mrhiPassDef passDef = mrhiDefaultPassDef();
    passDef.passClass = mrhi_passTransfer;
    passDef.accesses = written;
    passDef.accessCount = 1;
    passDef.neverCull = true;
    mvfxGpuRecordsDef records = RecordsDef(resource, layout);
    records.count = resource;
    records.countOffset = 4;
    recorded = recorded && mrhiAddPass(gpu->device, &passDef, &pass) == mrhi_success &&
               mvfxGpuSim_AddPasses(sim) == mvfx_success &&
               mvfxGpuSim_AddRecords(sim, &records) == mvfx_success &&
               mrhiDevice_CompileFrame(gpu->device) == mrhi_success &&
               mrhiPass_Begin(gpu->device, pass) == mrhi_success &&
               mrhiPass_WriteBuffer(gpu->device, pass, resource, 0, fill, sizeof fill) == mrhi_success &&
               mrhiPass_End(gpu->device, pass) == mrhi_success &&
               mvfxGpuSim_Record(sim) == mvfx_success && Finish(gpu);
    recorded = recorded && mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success &&
               mrhiImportBuffer(gpu->device, buffer, &resource) == mrhi_success;
    const mrhiAccess reading[1] = {{.resource = resource, .kind = mrhi_accessCopySource}};
    passDef.accesses = reading;
    uint32_t back[4] = {0};
    size_t taken = 0;
    recorded = recorded && mrhiAddPass(gpu->device, &passDef, &pass) == mrhi_success &&
               mrhiDevice_CompileFrame(gpu->device) == mrhi_success &&
               mrhiPass_Begin(gpu->device, pass) == mrhi_success &&
               mrhiPass_ReadBuffer(gpu->device, pass, resource, 0, sizeof back, &read) == mrhi_success &&
               mrhiPass_End(gpu->device, pass) == mrhi_success && Finish(gpu) &&
               mrhiRequest_TakeReadback(gpu->device, read, back, sizeof back, &taken) == mrhi_success;
    CHECK(recorded && back[0] == RECORD_FILL * 0x01010101u && back[1] == 0 &&
              back[2] == RECORD_FILL * 0x01010101u,
          "an emitter left out: a count of 0, no record");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
    (void)mrhiDestroyBuffer(gpu->device, buffer);
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestRecords(gpu, kernels);
    TestExactRecords(gpu, kernels);
    TestSortedRecords(gpu, kernels);
    TestRecordsRefused(gpu, kernels);
}

int main(void)
{
    return RunSuite(Suite, &s_failures);
}
