// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part's test harness (rhi_harness.h): the device, the kernels and
// the depth-writing pipeline, the run of an effect on a planned instance
// beside the CPU path, and its comparisons after every step: the live
// count, each particle's serial and its birth and death ticks equal, in
// the same order; its place and velocity within a tolerance (the GPU's
// floats are not the CPU's bits); the bounds; the records.

#include "rhi_harness.h"

#include "emitter_step.h"
#include "half.h"
#include "instance.h"
#include "instance_api.h"
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

// The longest a frame of the warm-up is waited for: a device's first
// frame may compile the kernels it dispatches (lavapipe does, without its
// shader cache, a minute on a loaded machine); and every frame after it,
// so a kernel that hangs fails in seconds.
#define WARM_WAIT_NS  300000000000ull
#define FRAME_WAIT_NS 20000000000ull
static uint64_t s_waitNs = WARM_WAIT_NS;

// The longest a frame after the warm-up took, in seconds.
static double s_longestFrame;

// The ticks a step of the run takes.
static int64_t s_stepTicks = STEP_TICKS;

static bool Open(Gpu* gpu)
{
    *gpu = (Gpu){0};
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    search.allowSoftware = true;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter;
    size_t count = 0;
    if (mrhiCreateInstance(&def, &gpu->instance) != mrhi_success ||
        mrhiRequestAdapters(gpu->instance, &search, &request) != mrhi_success ||
        mrhiNextInstanceNotification(gpu->instance, &record) != mrhi_success ||
        record.outcome != mrhi_success ||
        mrhiGetAdapters(gpu->instance, &adapter, 1, &count) != mrhi_success || count == 0)
    {
        return false;
    }
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    // Room for an emitter of 2^17: its plan's births uploaded, its
    // particles read back.
    deviceDef.deviceLimits.frameUploadBytes = 16u << 20u;
    deviceDef.deviceLimits.readbackBytes = 16u << 20u;
    return mrhiCreateDevice(gpu->instance, &deviceDef, &gpu->device, &request) == mrhi_success &&
           mrhiNextInstanceNotification(gpu->instance, &record) == mrhi_success &&
           record.outcome == mrhi_success;
}

static void Close(Gpu* gpu)
{
    if (gpu->device != NULL)
    {
        mrhiDestroyDevice(gpu->device);
    }
    if (gpu->instance != NULL)
    {
        mrhiDestroyInstance(gpu->instance);
    }
}

static double Seconds(void)
{
    struct timespec now = {0};
    (void)timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// Polls the device, ten seconds at most, for the object's pipelines.
static bool AwaitReady(Gpu* gpu, mvfxGpu* kernels)
{
    double deadline = Seconds() + 10.0;
    while (!mvfxGpu_IsReady(kernels) && Seconds() < deadline)
    {
        mrhiDeviceNotification record;
        while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
        {
            (void)mvfxGpu_Notify(kernels, &record);
        }
    }
    return mvfxGpu_IsReady(kernels);
}

// Submits the frame recorded and waits for it, its notifications drained.
bool Finish(Gpu* gpu)
{
    mrhiRequestId token = {0};
    double start = Seconds();
    bool done = mrhiSubmitFrame(gpu->device, &token) == mrhi_success &&
                mrhiWaitFrame(gpu->device, token, s_waitNs) == mrhi_success;
    if (s_waitNs == FRAME_WAIT_NS && Seconds() - start > s_longestFrame)
    {
        s_longestFrame = Seconds() - start;
    }
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
    {
    }
    return done;
}

// The test's depth-writing pipeline, and a run's depth image on the GPU:
// its pixels in a buffer (the width's bits first), rendered into a depth
// texture at every frame, and its normals, four binary32 a pixel, written
// into a normal texture.
static mrhiShaderId s_fillShader;
static mrhiGraphicsPipelineId s_fill;
static const mvfxDepthImage* s_depth;
static mrhiBufferId s_depthBuffer;
static mrhiTextureId s_depthTexture;
static mrhiTextureId s_normalTexture;
static float s_depthWords[1 + DEPTH_SIDE * DEPTH_SIDE];
static float s_normalWords[4 * DEPTH_SIDE * DEPTH_SIDE];
static mrhiPassId s_depthPasses[2];
static mrhiResourceId s_depthResources[3];

// Makes the depth-writing pipeline and waits, ten seconds at most, for it.
static bool MakeFill(Gpu* gpu)
{
    mrhiShaderDef shader = mrhiDefaultShaderDef();
    shader.bytes = s_depthFillContainer;
    shader.byteCount = sizeof s_depthFillContainer;
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.vertexEntry = "cover";
    def.vertexEntryLength = 5;
    def.fragmentEntry = "fill";
    def.fragmentEntryLength = 4;
    def.depthStencilFormat = mrhi_formatDepth32Float;
    def.depthWrite = true;
    def.depthCompare = mrhi_compareAlways;
    mrhiRequestId request = {0};
    if (mrhiCreateShader(gpu->device, &shader, &s_fillShader) != mrhi_success)
    {
        return false;
    }
    def.shader = s_fillShader;
    if (mrhiCreateGraphicsPipeline(gpu->device, &def, &s_fill, &request) != mrhi_success)
    {
        return false;
    }
    double deadline = Seconds() + 10.0;
    while (Seconds() < deadline)
    {
        mrhiDeviceNotification record;
        while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
        {
            if (record.kind == mrhi_devicePipelineReady &&
                record.request.index1 == request.index1 &&
                record.request.generation == request.generation)
            {
                return record.outcome == mrhi_success;
            }
        }
    }
    return false;
}

// Makes a run's depth image's buffer and textures, the pixels and normals
// laid out for them.
static void OpenDepth(Gpu* gpu, const mvfxDepthImage* image)
{
    s_depth = image;
    int32_t w = image->width;
    int32_t h = image->height;
    CHECK(w <= DEPTH_SIDE && h <= DEPTH_SIDE && image->format == mvfx_depthFloat32,
          "a depth image the test renders");
    uint32_t width = (uint32_t)w;
    memcpy(&s_depthWords[0], &width, sizeof width);
    for (int32_t r = 0; r < h; ++r)
    {
        memcpy(&s_depthWords[1 + r * w],
               (const unsigned char*)image->pixels + (int64_t)r * image->stride,
               (size_t)w * sizeof(float));
        for (int32_t c = 0; image->normals != NULL && c < w; ++c)
        {
            memcpy(&s_normalWords[4 * (r * w + c)],
                   (const unsigned char*)image->normals + (int64_t)r * image->normalStride +
                       (int64_t)c * 12,
                   12);
            s_normalWords[4 * (r * w + c) + 3] = 0.0f;
        }
    }
    mrhiBufferDef buffer = mrhiDefaultBufferDef();
    buffer.size = (uint64_t)(1 + w * h) * sizeof(float);
    buffer.usage = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    mrhiTextureDef texture = mrhiDefaultTextureDef();
    texture.width = (uint32_t)w;
    texture.height = (uint32_t)h;
    texture.format = mrhi_formatDepth32Float;
    texture.usage = mrhi_textureSampled | mrhi_textureRenderTarget;
    CHECK(mrhiCreateBuffer(gpu->device, &buffer, &s_depthBuffer) == mrhi_success &&
              mrhiCreateTexture(gpu->device, &texture, &s_depthTexture) == mrhi_success,
          "the depth image's buffer and texture");
    s_normalTexture = (mrhiTextureId){0};
    if (image->normals != NULL)
    {
        texture.format = mrhi_formatRgba32Float;
        texture.usage = mrhi_textureSampled | mrhi_textureCopyDestination;
        CHECK(mrhiCreateTexture(gpu->device, &texture, &s_normalTexture) == mrhi_success,
              "the normal texture");
    }
}

static void CloseDepth(Gpu* gpu)
{
    if (s_depth == NULL)
    {
        return;
    }
    (void)mrhiDestroyBuffer(gpu->device, s_depthBuffer);
    (void)mrhiDestroyTexture(gpu->device, s_depthTexture);
    if (s_normalTexture.index1 != 0)
    {
        (void)mrhiDestroyTexture(gpu->device, s_normalTexture);
    }
    s_depth = NULL;
}

// Adds the frame's passes making the depth image: the pixels and normals
// written, the pixels rendered into the depth texture; gives the textures
// to the simulation.
static bool AddDepthPasses(Gpu* gpu, mvfxGpuSim* sim)
{
    bool normals = s_normalTexture.index1 != 0;
    mrhiResourceId* r = s_depthResources;
    r[2] = (mrhiResourceId){0};
    if (mrhiImportBuffer(gpu->device, s_depthBuffer, &r[0]) != mrhi_success ||
        mrhiImportTexture(gpu->device, s_depthTexture, &r[1]) != mrhi_success ||
        (normals && mrhiImportTexture(gpu->device, s_normalTexture, &r[2]) != mrhi_success))
    {
        return false;
    }
    const mrhiAccess written[2] = {{.resource = r[0], .kind = mrhi_accessCopyDestination},
                                   {.resource = r[2],
                                    .kind = mrhi_accessCopyDestination,
                                    .range = {.mipCount = 1, .layerCount = 1}}};
    mrhiPassDef write = mrhiDefaultPassDef();
    write.passClass = mrhi_passTransfer;
    write.accesses = written;
    write.accessCount = normals ? 2 : 1;
    const mrhiAccess read = {.resource = r[0], .kind = mrhi_accessStorageRead};
    mrhiPassDef fill = mrhiDefaultPassDef();
    fill.accesses = &read;
    fill.accessCount = 1;
    fill.depthTarget = (mrhiDepthTarget){
        .resource = r[1], .depthLoad = mrhi_loadClear, .depthStore = mrhi_storeKeep};
    return mrhiAddPass(gpu->device, &write, &s_depthPasses[0]) == mrhi_success &&
           mrhiAddPass(gpu->device, &fill, &s_depthPasses[1]) == mrhi_success &&
           mvfxGpuSim_SetDepthTextures(sim, r[1], r[2]) == mvfx_success;
}

// Records the passes AddDepthPasses added.
static bool RecordDepthPasses(Gpu* gpu)
{
    int32_t w = s_depth->width;
    int32_t h = s_depth->height;
    mrhiResourceId* r = s_depthResources;
    mrhiPassId write = s_depthPasses[0];
    mrhiPassId fill = s_depthPasses[1];
    const mrhiTextureCopy normals = {.resource = r[2]};
    const mrhiTexelLayout layout = {.bytesPerRow = (uint32_t)w * 16u};
    const mrhiExtent3d size = {(uint32_t)w, (uint32_t)h, 1};
    bool recorded =
        mrhiBeginPass(gpu->device, write) == mrhi_success &&
        mrhiWriteBuffer(gpu->device, write, r[0], 0, s_depthWords,
                        (uint64_t)(1 + w * h) * sizeof(float)) == mrhi_success &&
        (r[2].index1 == 0 ||
         mrhiWriteTexture(gpu->device, write, &normals, s_normalWords,
                          (size_t)(4 * w * h) * sizeof(float), &layout, &size) == mrhi_success);
    recorded = mrhiEndPass(gpu->device, write) == mrhi_success && recorded;
    const mrhiBinding binding = {.slot = 0, .resource = r[0], .size = MRHI_WHOLE_SIZE};
    recorded = recorded && mrhiBeginPass(gpu->device, fill) == mrhi_success &&
               mrhiSetGraphicsPipeline(gpu->device, fill, s_fill) == mrhi_success &&
               mrhiSetBindings(gpu->device, fill, 0, &binding, 1) == mrhi_success &&
               mrhiDraw(gpu->device, fill, 3, 1, 0, 0) == mrhi_success;
    return mrhiEndPass(gpu->device, fill) == mrhi_success && recorded;
}

// One frame of the simulation's step, after the depth image's passes when
// the run has one.

// The emitter's particles and counts as the GPU holds them, read back, for
// capacities up to 2^17 and particles of up to 48 words; where its values
// are among them, and each of the run's emitters'.
#define MOST_WORDS 48
static uint32_t s_words[MOST_WORDS * (1 << 17)];
static uint32_t s_record[MVFX_GPU_STATISTICS_WORDS];
static mvfxAttributeArrays s_arrays;
static mvfxAttributeArrays s_runArrays[2];

static int32_t s_capacity;

bool ReadBack(Gpu* gpu, const mvfxGpuSim* sim, int32_t emitter)
{
    mvfxGpuParticles particles;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId buffers[2] = {{0}, {0}};
    mrhiPassId pass = {0};
    mrhiRequestId reads[2] = {{0}, {0}};
    if (mvfxGpuSim_GetParticles(sim, emitter, &particles) != mvfx_success ||
        mrhiBeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    s_capacity = particles.capacity;
    s_arrays = s_runArrays[emitter];
    if (particles.words != s_arrays.count + 3 || particles.words > MOST_WORDS)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    uint64_t bytes = (uint64_t)s_capacity * (uint64_t)particles.words * sizeof(uint32_t);
    bool recorded =
        mrhiImportBuffer(gpu->device, particles.particles, &buffers[0]) == mrhi_success &&
        mrhiImportBuffer(gpu->device, particles.statistics, &buffers[1]) == mrhi_success;
    const mrhiAccess reading[2] = {{.resource = buffers[0], .kind = mrhi_accessCopySource},
                                   {.resource = buffers[1], .kind = mrhi_accessCopySource}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = reading;
    def.accessCount = 2;
    def.neverCull = true;
    recorded = recorded && mrhiAddPass(gpu->device, &def, &pass) == mrhi_success &&
               mrhiCompileFrame(gpu->device) == mrhi_success &&
               mrhiBeginPass(gpu->device, pass) == mrhi_success &&
               mrhiReadBuffer(gpu->device, pass, buffers[0], 0, bytes, &reads[0]) == mrhi_success &&
               mrhiReadBuffer(gpu->device, pass, buffers[1], 0, sizeof s_record, &reads[1]) ==
                   mrhi_success &&
               mrhiEndPass(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    size_t taken = 0;
    size_t counted = 0;
    return Finish(gpu) &&
           mrhiTakeReadback(gpu->device, reads[0], s_words, sizeof s_words, &taken) ==
               mrhi_success &&
           taken == bytes &&
           mrhiTakeReadback(gpu->device, reads[1], s_record, sizeof s_record, &counted) ==
               mrhi_success &&
           counted == sizeof s_record;
}

// A word of particle i in the read-back arrays.
static uint32_t Word(int32_t array, int32_t i)
{
    return s_words[array * s_capacity + i];
}

static float FloatOf(int32_t array, int32_t i)
{
    float f;
    uint32_t w = Word(array, i);
    memcpy(&f, &w, sizeof f);
    return f;
}

// The contacts the CPU path's particles hold, summed over a run's steps.
static uint64_t s_contacts;

// The largest differences seen, place and velocity, over the run.
static float s_placeError;
static float s_velocityError;

// Whether the GPU's particles are the CPU path's: counts, serials and ticks
// equal, in order, places and velocities within the tolerance.
// The largest relative difference a place or a velocity may have.
static float s_tolerance = 1e-4f;

// Whether each particle's death tick is compared, and what the comparison
// found: the particles past 2^24 ticks of age at death (16 seconds), those
// whose ticks differ, and whether every difference was within its bound.
static bool s_deathTicks;
static int64_t s_pastAge;
static int64_t s_deathsApart;
static bool s_deathsWithin = true;

// A particle's death tick on the GPU against the CPU path's: the same
// before 2^24 ticks of age, where binary32 holds every age; past it the
// GPU's integer to binary32 conversion may round the other way, by half a
// binary32 unit of the age in ticks (one tick to 32 seconds).
static void CompareDeath(int32_t i, int64_t birth, float inverseLife)
{
    int64_t cpu = mvfxDeathTick(birth, inverseLife);
    int32_t death = s_arrays.count;
    int64_t gpu = (int64_t)((uint64_t)Word(death, i) | (uint64_t)Word(death + 1, i) << 32u);
    int64_t age = cpu - birth;
    int64_t bound = 0;
    for (int64_t top = (int64_t)1 << 25; top <= age * 2; top *= 2)
    {
        bound = bound == 0 ? 1 : 2 * bound;
    }
    int64_t apart = gpu > cpu ? gpu - cpu : cpu - gpu;
    s_pastAge += age >= (int64_t)1 << 24 ? 1 : 0;
    s_deathsApart += apart != 0 ? 1 : 0;
    s_deathsWithin = s_deathsWithin && apart <= bound;
}

// A drawn value of the GPU's particle i against the CPU path's: its
// components from its first word (none where not stored), as binary32
// within the tolerance or as words equal.
static bool SameValue(const mvfxInstance* cpu, int32_t e, int32_t i, int16_t first,
                      int32_t attribute, int32_t components, bool words)
{
    bool same = true;
    for (int32_t k = 0; first >= 0 && k < components; ++k)
    {
        const uint32_t* cpuWords = mvfxAttributeArray(cpu, e, (mvfxAttribute)(attribute + k));
        float cpuValue = 0.0f;
        memcpy(&cpuValue, &cpuWords[i], sizeof cpuValue);
        float gpuValue = FloatOf(first + k, i);
        same =
            same && (words ? Word(first + k, i) == cpuWords[i]
                           : fabsf(gpuValue - cpuValue) <= s_tolerance * (1.0f + fabsf(cpuValue)));
    }
    return same;
}

// Every value a birth draws, as moved since, of the GPU's particle i
// against the CPU path's.
static bool SameValues(const mvfxInstance* cpu, int32_t e, int32_t i)
{
    const mvfxAttributeArrays* a = &s_arrays;
    const mvfxCompiledEmitter* compiled = &cpu->effect->emitters[e];
    bool same = SameValue(cpu, e, i, a->size, mvfx_attributeSizeX, 3, false) &&
                SameValue(cpu, e, i, a->color, mvfx_attributeColorR, 4, false) &&
                SameValue(cpu, e, i, a->frame, mvfx_attributeFrame, 1, true) &&
                SameValue(cpu, e, i, a->mesh, mvfx_attributeMesh, 1, true) &&
                SameValue(cpu, e, i, a->angle, mvfx_attributeAngle, 1, false) &&
                SameValue(cpu, e, i, a->spin, mvfx_attributeSpin, 1, false) &&
                SameValue(cpu, e, i, a->orientation, mvfx_attributeOrientationX, 4, false) &&
                SameValue(cpu, e, i, a->angular, mvfx_attributeAngularX, 3, false) &&
                SameValue(cpu, e, i, a->drag, mvfx_attributeDrag, 1, false);
    for (int32_t c = 0; same && c < compiled->customCount; ++c)
    {
        same = SameValue(cpu, e, i, a->custom[c], mvfx_attributeCustom + 4 * c,
                         compiled->custom[c].components, false);
    }
    return same;
}

static bool SameAsCpu(const mvfxInstance* cpu, int32_t e)
{
    const mvfxEmitterState* state = &cpu->emitters[e];
    if ((int32_t)s_record[0] != state->count || s_record[2] != 0)
    {
        printf("counts: GPU %u (refused %u), CPU %d\n", s_record[0], s_record[2], state->count);
        return false;
    }
    const uint32_t* low = mvfxAttributeArray(cpu, e, mvfx_attributeBirthLow);
    const uint32_t* high = mvfxAttributeArray(cpu, e, mvfx_attributeBirthHigh);
    const uint32_t* serial = mvfxAttributeArray(cpu, e, mvfx_attributeSerial);
    const float* inverseLife = mvfxAttributeArray(cpu, e, mvfx_attributeInverseLife);
    const mvfxAttributePlan* plan = &cpu->effect->emitters[e].plan;
    const uint32_t* collisions = mvfxHasAttribute(plan, mvfx_attributeCollisions)
                                     ? mvfxAttributeArray(cpu, e, mvfx_attributeCollisions)
                                     : NULL;
    static const mvfxAttribute places[3] = {mvfx_attributePositionX, mvfx_attributePositionY,
                                            mvfx_attributePositionZ};
    static const mvfxAttribute velocities[3] = {mvfx_attributeVelocityX, mvfx_attributeVelocityY,
                                                mvfx_attributeVelocityZ};
    // Births from collisions are born at their contacts' ticks, which
    // round a share of the move the GPU finds in binary32: within a
    // thousandth of the step, as the events' (SameEvent).
    const mvfxEventSpawn* spawn = &cpu->effect->emitters[e].eventSpawn;
    int64_t slack =
        spawn->enabled && spawn->kind == mvfx_eventCollision ? 1 + s_stepTicks / 1000 : 0;
    bool same = true;
    for (int32_t i = 0; same && i < state->count; ++i)
    {
        uint32_t lifeBits;
        memcpy(&lifeBits, &inverseLife[i], sizeof lifeBits);
        const mvfxAttributeArrays* a = &s_arrays;
        int64_t gpuBirth = (int64_t)((uint64_t)Word(a->birth + 1, i) << 32u | Word(a->birth, i));
        int64_t cpuBirth = (int64_t)((uint64_t)high[i] << 32u | low[i]);
        same = Word(a->serial, i) == serial[i] && llabs(gpuBirth - cpuBirth) <= slack &&
               Word(a->inverseLife, i) == lifeBits &&
               (collisions == NULL || Word(a->collisions, i) == collisions[i]);
        s_contacts += collisions != NULL ? collisions[i] : 0u;
        if (s_deathTicks)
        {
            CompareDeath(i, (int64_t)((uint64_t)low[i] | (uint64_t)high[i] << 32u), inverseLife[i]);
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            const float* p = mvfxAttributeArray(cpu, e, places[k]);
            const float* v = mvfxAttributeArray(cpu, e, velocities[k]);
            // Relative to the size, as places reach the rebase distance.
            float dp = fabsf(FloatOf(a->position + k, i) - p[i]) / (1.0f + fabsf(p[i]));
            float dv = fabsf(FloatOf(a->velocity + k, i) - v[i]) / (1.0f + fabsf(v[i]));
            s_placeError = dp > s_placeError ? dp : s_placeError;
            s_velocityError = dv > s_velocityError ? dv : s_velocityError;
            same = same && dp <= s_tolerance && dv <= s_tolerance;
        }
        same = same && SameValues(cpu, e, i);
        if (!same)
        {
            printf("particle %d of serial %u differs\n", i, serial[i]);
        }
    }
    return same;
}

// The context of a run: a CPU instance and a planned one side by side.
static mvfxContext* s_context;
static mvfxInstanceId s_cpu;
static mvfxInstanceId s_planned;

// A run's records (none where its layout is NULL): the layout, the
// buffers the GPU writes them and their count into, the view, and the
// most records the buffer holds, past which the CPU path's count still
// counts; the GPU's read back, and the CPU path's, every byte filled
// first, the gaps no field covers kept.
#define RECORD_BYTES (64 * 1024 * 16)
static mvfxLayoutId s_layout;
static mvfxLayoutView s_layoutView;
static mrhiBufferId s_recordBuffer;
static mrhiBufferId s_countBuffer;
static mrhiResourceId s_recordResource;
static mvfxView s_view;
static int64_t s_recordCapacity;
static uint8_t s_gpuRecords[RECORD_BYTES];
static uint8_t s_cpuRecords[RECORD_BYTES];
static uint32_t s_gpuCount[4];

// The fill's bytes: written before the GPU's records each step.
static uint32_t s_fillBytes[RECORD_BYTES / 4];

static bool OpenRecords(Gpu* gpu, const mvfxLayoutDef* layout, int64_t capacity,
                        const mvfxView* view)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = RECORD_BYTES;
    def.usage = mrhi_bufferStorage | mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferDef count = def;
    count.size = sizeof s_gpuCount;
    memset(s_fillBytes, RECORD_FILL, sizeof s_fillBytes);
    s_recordCapacity = capacity;
    s_view = view != NULL ? *view
                          : (mvfxView){.origin = {3.5, -1.25, 2.0},
                                       .eye = {0, 0, 5},
                                       .forward = {0, 0, -1},
                                       .up = {0, 1, 0}};
    return mvfxCreateLayout(s_context, layout, &s_layout) == mvfx_success &&
           mvfxLayout_GetView(s_context, s_layout, &s_layoutView) == mvfx_success &&
           mrhiCreateBuffer(gpu->device, &def, &s_recordBuffer) == mrhi_success &&
           mrhiCreateBuffer(gpu->device, &count, &s_countBuffer) == mrhi_success;
}

static void CloseRecords(Gpu* gpu)
{
    (void)mrhiDestroyBuffer(gpu->device, s_recordBuffer);
    (void)mrhiDestroyBuffer(gpu->device, s_countBuffer);
    s_layout = (mvfxLayoutId){0};
}

// Adds the fill of the records buffer and the first emitter's record pass,
// its count at the count buffer's second word; the fill's pass.
static bool AddRecordPasses(mvfxGpuSim* sim, mrhiDevice* device, mrhiPassId* fill)
{
    mrhiResourceId count = {0};
    if (mrhiImportBuffer(device, s_recordBuffer, &s_recordResource) != mrhi_success ||
        mrhiImportBuffer(device, s_countBuffer, &count) != mrhi_success)
    {
        return false;
    }
    const mrhiAccess written[1] = {
        {.resource = s_recordResource, .kind = mrhi_accessCopyDestination}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = written;
    def.accessCount = 1;
    def.neverCull = true;
    mvfxGpuRecordsDef records = mvfxDefaultGpuRecordsDef();
    records.layout = s_layout;
    records.view = s_view;
    records.records = s_recordResource;
    records.offset = 64;
    records.capacity = s_recordCapacity;
    records.count = count;
    records.countOffset = 4;
    return mrhiAddPass(device, &def, fill) == mrhi_success &&
           mvfxGpuSim_AddRecords(sim, &records) == mvfx_success;
}

static bool RecordFill(mrhiDevice* device, mrhiPassId fill)
{
    bool recorded = mrhiBeginPass(device, fill) == mrhi_success &&
                    mrhiWriteBuffer(device, fill, s_recordResource, 0, s_fillBytes,
                                    sizeof s_fillBytes) == mrhi_success;
    return mrhiEndPass(device, fill) == mrhi_success && recorded;
}

// Reads the GPU's records and count back.
static bool ReadRecords(Gpu* gpu)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId buffers[2] = {{0}, {0}};
    mrhiPassId pass = {0};
    mrhiRequestId reads[2] = {{0}, {0}};
    if (mrhiBeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    bool recorded = mrhiImportBuffer(gpu->device, s_recordBuffer, &buffers[0]) == mrhi_success &&
                    mrhiImportBuffer(gpu->device, s_countBuffer, &buffers[1]) == mrhi_success;
    const mrhiAccess reading[2] = {{.resource = buffers[0], .kind = mrhi_accessCopySource},
                                   {.resource = buffers[1], .kind = mrhi_accessCopySource}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = reading;
    def.accessCount = 2;
    def.neverCull = true;
    recorded =
        recorded && mrhiAddPass(gpu->device, &def, &pass) == mrhi_success &&
        mrhiCompileFrame(gpu->device) == mrhi_success &&
        mrhiBeginPass(gpu->device, pass) == mrhi_success &&
        mrhiReadBuffer(gpu->device, pass, buffers[0], 0, RECORD_BYTES, &reads[0]) == mrhi_success &&
        mrhiReadBuffer(gpu->device, pass, buffers[1], 0, sizeof s_gpuCount, &reads[1]) ==
            mrhi_success &&
        mrhiEndPass(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    size_t taken = 0;
    size_t counted = 0;
    return Finish(gpu) &&
           mrhiTakeReadback(gpu->device, reads[0], s_gpuRecords, sizeof s_gpuRecords, &taken) ==
               mrhi_success &&
           mrhiTakeReadback(gpu->device, reads[1], s_gpuCount, sizeof s_gpuCount, &counted) ==
               mrhi_success &&
           taken == RECORD_BYTES && counted == sizeof s_gpuCount;
}

// A run's emitters' events read back from the GPU (each emitter's, as
// its particles are, after its step), and the CPU path's count of those
// dropped by the steps before.
#define MOST_EVENTS 4096
static uint32_t s_eventWords[MOST_EVENTS * MVFX_GPU_EVENT_WORDS];
static uint64_t s_dropped[MVFX_MAX_EMITTERS];
// The CPU path's events over the run.
static uint64_t s_events;

// Reads an emitter's events back, when it writes any.
static bool ReadEvents(Gpu* gpu, const mvfxGpuSim* sim, int32_t emitter)
{
    mvfxGpuParticles particles;
    if (mvfxGpuSim_GetParticles(sim, emitter, &particles) != mvfx_success ||
        particles.eventCapacity > MOST_EVENTS)
    {
        return false;
    }
    if (particles.events.index1 == 0)
    {
        return true;
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId buffer = {0};
    mrhiPassId pass = {0};
    mrhiRequestId read = {0};
    uint64_t bytes = (uint64_t)particles.eventCapacity * MVFX_GPU_EVENT_WORDS * sizeof(uint32_t);
    if (mrhiBeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    bool recorded = mrhiImportBuffer(gpu->device, particles.events, &buffer) == mrhi_success;
    const mrhiAccess reading[1] = {{.resource = buffer, .kind = mrhi_accessCopySource}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = reading;
    def.accessCount = 1;
    def.neverCull = true;
    recorded = recorded && mrhiAddPass(gpu->device, &def, &pass) == mrhi_success &&
               mrhiCompileFrame(gpu->device) == mrhi_success &&
               mrhiBeginPass(gpu->device, pass) == mrhi_success &&
               mrhiReadBuffer(gpu->device, pass, buffer, 4u * (uint64_t)particles.eventFirst, bytes,
                              &read) == mrhi_success &&
               mrhiEndPass(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    size_t taken = 0;
    return Finish(gpu) &&
           mrhiTakeReadback(gpu->device, read, s_eventWords, sizeof s_eventWords, &taken) ==
               mrhi_success &&
           taken == bytes;
}

// Whether two floats agree within the run's tolerance, relative to the
// CPU path's.
static bool Near(float gpu, float cpu)
{
    return fabsf(gpu - cpu) <= s_tolerance * (1.0f + fabsf(cpu));
}

// Whether an event the GPU wrote (its words) is the CPU path's: words
// equal, a collision's tick within a thousandth of the step's ticks (at
// least one: its share of the move is a float of the GPU's, and a short
// move's share is the least sure), the rest within the tolerance, the
// place from the plan's anchor after.
static bool SameEvent(const uint32_t* w, const mvfxEvent* cpu, const double anchor[3])
{
    uint64_t tick = ((uint64_t)w[3] << 32u) | w[2];
    int64_t apart = (int64_t)tick - cpu->tick;
    bool same = (w[0] & 0xFFu) == cpu->kind && ((w[0] >> 8u) != 0) == cpu->killed &&
                w[1] == cpu->serial &&
                (apart == 0 ||
                 (cpu->kind == mvfx_eventCollision && llabs(apart) <= 1 + s_stepTicks / 1000));
    float values[20];
    memcpy(values, w + 4, sizeof values);
    for (int32_t k = 0; k < 3; ++k)
    {
        double place = anchor[k] + (double)values[k];
        same = same && fabs(place - cpu->place[k]) <= (double)s_tolerance * (1.0 + fabs(place)) &&
               Near(values[3 + k], cpu->velocity[k]) && Near(values[6 + k], cpu->normal[k]) &&
               Near(values[13 + k], cpu->size[k]);
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        same = same && Near(values[9 + k], cpu->color[k]);
    }
    return same;
}

// An emitter's events of the step on the GPU against the CPU path's: as
// many, as many dropped, each alike in the same order.
static bool SameEvents(int32_t emitter)
{
    const mvfxEvent* events = NULL;
    int32_t count = 0;
    mvfxEmitterStats stats;
    mvfxEmitterPlan plan;
    if (mvfxInstance_GetEvents(s_context, s_cpu, emitter, &events, &count) != mvfx_success ||
        mvfxInstance_GetEmitterStats(s_context, s_cpu, emitter, &stats) != mvfx_success ||
        mvfxInstance_GetEmitterPlan(s_context, s_planned, emitter, &plan) != mvfx_success)
    {
        return false;
    }
    uint64_t dropped = stats.eventsDropped - s_dropped[emitter];
    s_dropped[emitter] = stats.eventsDropped;
    s_events += count;
    bool same = (int32_t)s_record[MVFX_GPU_EVENTS_WRITTEN] == count &&
                s_record[MVFX_GPU_EVENTS_WRITTEN + 1] == dropped;
    for (int32_t e = 0; same && e < count; ++e)
    {
        same = SameEvent(s_eventWords + e * MVFX_GPU_EVENT_WORDS, &events[e], plan.anchorAfter);
        if (!same)
        {
            const uint32_t* w = s_eventWords + e * MVFX_GPU_EVENT_WORDS;
            float place[3];
            memcpy(place, w + 4, sizeof place);
            printf("event %d of %d differs: GPU kind %u killed %u serial %u tick %llu place "
                   "(%g %g %g); CPU kind %u killed %d serial %u tick %lld place (%g %g %g)\n",
                   e, count, w[0] & 0xFFu, w[0] >> 8u, w[1],
                   (unsigned long long)(((uint64_t)w[3] << 32u) | w[2]),
                   plan.anchorAfter[0] + (double)place[0], plan.anchorAfter[1] + (double)place[1],
                   plan.anchorAfter[2] + (double)place[2], events[e].kind, events[e].killed,
                   events[e].serial, (long long)events[e].tick, events[e].place[0],
                   events[e].place[1], events[e].place[2]);
        }
    }
    if (!same)
    {
        printf("events: GPU %u (dropped %u), CPU %d (dropped %llu)\n",
               s_record[MVFX_GPU_EVENTS_WRITTEN], s_record[MVFX_GPU_EVENTS_WRITTEN + 1], count,
               (unsigned long long)dropped);
    }
    return same;
}

// The records' bytes a field's component takes, and its value decoded; a
// word for uint32.
static uint32_t FieldBytes(mvfxFormat format)
{
    return format == mvfx_formatFloat16 || format == mvfx_formatUnorm16 ||
                   format == mvfx_formatSnorm16
               ? 2u
           : format == mvfx_formatUnorm8 || format == mvfx_formatSnorm8 ? 1u
                                                                        : 4u;
}

static double Decoded(mvfxFormat format, const uint8_t* at)
{
    uint32_t word = 0;
    memcpy(&word, at, FieldBytes(format));
    float value = 0.0f;
    switch (format)
    {
    case mvfx_formatFloat32:
        memcpy(&value, &word, sizeof value);
        return (double)value;
    case mvfx_formatFloat16:
        return (double)mvfxFloatFromHalf((uint16_t)word);
    case mvfx_formatSnorm8:
        return (int8_t)word;
    case mvfx_formatSnorm16:
        return (int16_t)word;
    default:
        return word;
    }
}

// Records whose integer formats are a step apart, and those whose floats
// differ, over a run.
static int64_t s_stepsApart;

// Whether a GPU record's field is the CPU path's: words alike, floats
// within the tolerance (a half's within its last place), normalized
// integers within a step (their binary32 inputs within the tolerance may
// round apart).
// Whether the run's records must match exactly.
static bool s_exactRecords;

static bool SameField(const mvfxLayoutField* field, const uint8_t* cpu, const uint8_t* gpu)
{
    uint32_t bytes = FieldBytes(field->format);
    bool same = true;
    for (uint32_t k = 0; same && k < field->components; ++k)
    {
        double a = Decoded(field->format, cpu + field->offset + k * bytes);
        double b = Decoded(field->format, gpu + field->offset + k * bytes);
        double apart = fabs(a - b);
        switch (s_exactRecords ? mvfx_formatUint32 : field->format)
        {
        case mvfx_formatUint32:
            same = a == b;
            break;
        case mvfx_formatFloat32:
            same = apart <= (double)s_tolerance * (1.0 + fabs(a));
            break;
        case mvfx_formatFloat16:
            same = apart <= fmax(fabs(a) * 0x1p-10, (double)s_tolerance * (1.0 + fabs(a)));
            break;
        default:
            same = apart <= 1.0;
            s_stepsApart += apart != 0.0 ? 1 : 0;
            break;
        }
    }
    return same;
}

// Whether the run's first emitter sorts its records by place, and by
// depth; the CPU path's scratch for its sort.
static bool s_sorted;
static bool s_byDepth;
static uint64_t s_sortScratch[4096 * 16 / 8];

// A record's measure from the view's eye, in binary64 from its place (a
// binary32 vector at the layout's first field).
static double MeasureOf(const uint8_t* record)
{
    float place[3];
    memcpy(place, record, sizeof place);
    double measure = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        double d = (double)place[k] - (s_view.eye[k] - s_view.origin[k]);
        measure += s_byDepth ? d * (double)s_view.forward[k] : d * d;
    }
    return measure;
}

// The serial of a record, a word at its serial field's offset.
static uint32_t SerialOf(const uint8_t* record)
{
    uint32_t serial = 0;
    for (int32_t f = 0; f < s_layoutView.fieldCount; ++f)
    {
        if (s_layoutView.fields[f].field == mvfx_recordSerial)
        {
            memcpy(&serial, record + s_layoutView.fields[f].offset, sizeof serial);
        }
    }
    return serial;
}

// The CPU path's records by serial: each one's serial and place.
typedef struct Serial
{
    uint32_t serial;
    int64_t record;
} Serial;

static Serial s_serials[RECORD_BYTES / 64];

static int BySerial(const void* a, const void* b)
{
    uint32_t x = ((const Serial*)a)->serial;
    uint32_t y = ((const Serial*)b)->serial;
    return (x > y) - (x < y);
}

// The CPU path's record of a serial (NULL for none).
static const uint8_t* CpuRecordOf(uint32_t serial, int64_t written)
{
    Serial key = {.serial = serial};
    const Serial* found = bsearch(&key, s_serials, (size_t)written, sizeof key, BySerial);
    return found != NULL ? s_cpuRecords + 64 + found->record * s_layoutView.stride : NULL;
}

// Each of the GPU's records against the CPU path's of its serial: the CPU
// path's measure of the GPU's record at each place the same as of its own
// within 1e-5 (so the orders differ only where measures do by less, as
// sim.h states; the places, held to the tolerance, are the fields'); a
// serial the CPU path did not write only at the cut, its measure within
// that of the last record's.
static bool SameSorted(int64_t written)
{
    uint32_t stride = s_layoutView.stride;
    for (int64_t c = 0; c < written; ++c)
    {
        s_serials[c] = (Serial){.serial = SerialOf(s_cpuRecords + 64 + c * stride), .record = c};
    }
    qsort(s_serials, (size_t)written, sizeof s_serials[0], BySerial);
    bool same = true;
    for (int64_t r = 0; same && r < written; ++r)
    {
        const uint8_t* gpu = s_gpuRecords + 64 + r * stride;
        const uint8_t* cpu = CpuRecordOf(SerialOf(gpu), written);
        double cpuMeasure = MeasureOf(s_cpuRecords + 64 + r * stride);
        double measure = MeasureOf(cpu != NULL ? cpu : gpu);
        same = fabs(measure - cpuMeasure) <= 1e-5 * (1.0 + fabs(cpuMeasure));
        double last = MeasureOf(s_cpuRecords + 64 + (written - 1) * stride);
        same = same && (cpu != NULL || (written == s_recordCapacity &&
                                        fabs(MeasureOf(gpu) - last) <= 1e-5 * (1.0 + fabs(last))));
        for (int32_t f = 0; same && cpu != NULL && f < s_layoutView.fieldCount; ++f)
        {
            same = SameField(&s_layoutView.fields[f], cpu, gpu);
        }
        if (!same)
        {
            printf("sorted record %lld of %lld differs\n", (long long)r, (long long)written);
        }
    }
    return same;
}

// The GPU's records and count against the CPU path's: each record's
// fields (in the same order, or by serial where sorted), every other byte
// the fill's.
static bool SameRecords(void)
{
    int64_t count = 0;
    int64_t scratch = 0;
    memset(s_cpuRecords, RECORD_FILL, sizeof s_cpuRecords);
    if (mvfxInstance_GetSortScratchBytes(s_context, s_cpu, 0, &scratch) != mvfx_success ||
        scratch > (int64_t)sizeof s_sortScratch ||
        mvfxInstance_WriteParticles(s_context, s_cpu, 0, s_layout, &s_view, s_sortScratch, scratch,
                                    s_cpuRecords + 64, s_recordCapacity, &count) != mvfx_success)
    {
        return false;
    }
    int64_t written = count < s_recordCapacity ? count : s_recordCapacity;
    bool same = (int64_t)s_gpuCount[1] == written;
    uint32_t stride = s_layoutView.stride;
    // Every byte no field covers: the fill's on both.
    for (int64_t b = 0; same && b < RECORD_BYTES; ++b)
    {
        same = s_cpuRecords[b] == s_gpuRecords[b] || (b >= 64 && b < 64 + written * stride);
    }
    for (int64_t r = 0; same && r < written; ++r)
    {
        const uint8_t* cpu = s_cpuRecords + 64 + r * stride;
        const uint8_t* gpu = s_gpuRecords + 64 + r * stride;
        bool covered[MVFX_MAX_LAYOUT_STRIDE] = {false};
        for (int32_t f = 0; same && f < s_layoutView.fieldCount; ++f)
        {
            const mvfxLayoutField* field = &s_layoutView.fields[f];
            same = s_sorted || SameField(field, cpu, gpu);
            for (uint32_t b = 0; b < FieldBytes(field->format) * field->components; ++b)
            {
                covered[field->offset + b] = true;
            }
        }
        for (uint32_t b = 0; same && b < stride; ++b)
        {
            same = covered[b] || (cpu[b] == RECORD_FILL && gpu[b] == RECORD_FILL);
        }
        if (!same)
        {
            printf("record %lld of %lld differs\n", (long long)r, (long long)written);
        }
    }
    return same && (!s_sorted || SameSorted(written));
}

bool Simulate(Gpu* gpu, mvfxGpuSim* sim)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    if (mrhiBeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    bool depth = s_depth != NULL;
    bool records = s_layout.index1 != 0;
    mrhiPassId fill = {0};
    if ((depth && !AddDepthPasses(gpu, sim)) || mvfxGpuSim_AddPasses(sim) != mvfx_success ||
        (records && !AddRecordPasses(sim, gpu->device, &fill)) ||
        mrhiCompileFrame(gpu->device) != mrhi_success || (depth && !RecordDepthPasses(gpu)) ||
        (records && !RecordFill(gpu->device, fill)) || mvfxGpuSim_Record(sim) != mvfx_success)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    return Finish(gpu);
}

// The largest difference of the bounds seen over the run, relative to
// the box's size along its axis (as places, offsets from an anchor up to
// that far, are compared relative to theirs).
static double s_boundsError;

// Whether the GPU's bounds, from its record, are the CPU path's.
static bool SameBounds(const mvfxGpuSim* sim, int32_t e)
{
    mvfxBounds gpuBox;
    mvfxBounds cpuBox;
    if (mvfxGpuSim_BoundsOf(sim, e, s_record, &gpuBox) != mvfx_success ||
        mvfxInstance_GetBounds(s_context, s_cpu, e, &cpuBox) != mvfx_success ||
        gpuBox.empty != cpuBox.empty)
    {
        printf("bounds: GPU %s, CPU %s\n", gpuBox.empty ? "empty" : "a box",
               cpuBox.empty ? "empty" : "a box");
        return false;
    }
    bool same = true;
    for (int32_t k = 0; !cpuBox.empty && k < 3; ++k)
    {
        double size = 1.0 + (cpuBox.max[k] - cpuBox.min[k]);
        double low = fabs(gpuBox.min[k] - cpuBox.min[k]) / size;
        double high = fabs(gpuBox.max[k] - cpuBox.max[k]) / size;
        double most = low > high ? low : high;
        s_boundsError = most > s_boundsError ? most : s_boundsError;
        same = same && most <= 1e-4;
    }
    if (!same)
    {
        printf("bounds differ: GPU (%g %g %g)-(%g %g %g), CPU (%g %g %g)-(%g %g %g)\n",
               gpuBox.min[0], gpuBox.min[1], gpuBox.min[2], gpuBox.max[0], gpuBox.max[1],
               gpuBox.max[2], cpuBox.min[0], cpuBox.min[1], cpuBox.min[2], cpuBox.max[0],
               cpuBox.max[1], cpuBox.max[2]);
    }
    return same;
}

static const mvfxBurst s_bursts[1] = {
    {.time = 0.1f, .minCount = 20, .maxCount = 20, .cycles = 1, .probability = 1.0f}};

// A sprite emitter: a sphere's shell, cone velocities, gravity and drag,
// a burst; in the world.
mvfxEmitterDef Sprite(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = CAPACITY;
    emitter.rate = 600.0f;
    emitter.bursts = s_bursts;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(0.2f, 0.4f);
    emitter.shape = (mvfxShape){.kind = mvfx_shapeSphere, .rotation = {0, 0, 0, 1}, .radius = 0.5f};
    emitter.velocity = (mvfxVelocity){
        .mode = mvfx_directionCone, .direction = {0, 1, 0}, .coneAngle = 0.4f, .speed = {2, 4}};
    emitter.motion.gravity[1] = -9.8f;
    emitter.motion.drag = 0.5f;
    return emitter;
}

// Queues a run's host particles for both instances at step s: a little
// further each step, about the instance's origin for a world-space
// emitter.
static void SpawnFor(const Run* run, int32_t s)
{
    double positions[3 * 64];
    mvfxSpawnRequest request = *run->spawn;
    const mvfxInstance* cpu = mvfxContextInstance(s_context, s_cpu);
    bool world = run->emitters[0].space == mvfx_spaceWorld;
    for (int32_t i = 0; i < request.count; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            positions[3 * i + k] = run->spawn->positions[3 * i + k] +
                                   (world ? cpu->to.origin[k] : 0.0) + 0.01 * (double)s;
        }
    }
    request.positions = positions;
    mvfxInstanceId both[2] = {s_cpu, s_planned};
    for (int32_t i = 0; i < 2; ++i)
    {
        CHECK(mvfxInstance_Spawn(s_context, both[i], 0, &request) == mvfx_success, "spawned");
    }
}

// Moves both instances to step s's place: along a line past the rebase
// distance, turning about a tilted axis.
// Places both instances far from the origin, still.
static void PlaceFar(void)
{
    static const double place[3] = {100000.0, 2500.0, -60000.0};
    static const float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(mvfxInstance_SetTransform(s_context, s_cpu, place, q, 1.0f, true) == mvfx_success &&
              mvfxInstance_SetTransform(s_context, s_planned, place, q, 1.0f, true) == mvfx_success,
          "placed far");
}

static void Move(int32_t s, double pace)
{
    const double place[3] = {90.0 * pace * s, 3.0 * pace * s, -40.0 * pace * s};
    float angle = 0.03f * (float)s;
    const float q[4] = {0.6f * sinf(angle), 0.8f * sinf(angle), 0.0f, cosf(angle)};
    float scale = 1.0f + 0.01f * (float)s;
    CHECK(mvfxInstance_SetTransform(s_context, s_cpu, place, q, scale, false) == mvfx_success &&
              mvfxInstance_SetTransform(s_context, s_planned, place, q, scale, false) ==
                  mvfx_success,
          "moved");
}

// Clears and restarts both instances.
static void Clear(void)
{
    mvfxInstanceId both[2] = {s_cpu, s_planned};
    for (int32_t i = 0; i < 2; ++i)
    {
        CHECK(mvfxInstance_Stop(s_context, both[i], mvfx_stopAndClear) == mvfx_success &&
                  mvfxInstance_Restart(s_context, both[i]) == mvfx_success,
              "cleared");
    }
}

// The run's grids, and its emitters with their vector fields and grid
// colliders reading them.
static mvfxGridId s_grid;
static mvfxGridId s_colliderGrids[4];
static mvfxEmitterDef s_emitters[2];
static mvfxForce s_forces[2][MVFX_MAX_FORCES];
static mvfxCollider s_colliders[2][MVFX_MAX_COLLIDERS];

// Colliders of a run with their grids: {.index1 = n} the run's nth.
static const mvfxCollider* WithColliderGrids(const mvfxCollider* colliders, int32_t count,
                                             mvfxCollider* out)
{
    for (int32_t c = 0; c < count; ++c)
    {
        out[c] = colliders[c];
        uint32_t n = colliders[c].grid.index1;
        out[c].grid = n > 0 ? s_colliderGrids[n - 1] : colliders[c].grid;
    }
    return colliders != NULL ? out : NULL;
}

// Makes the run's grids and points its emitters' vector fields and grid
// colliders at them.
static const mvfxEmitterDef* WithGrid(const Run* run)
{
    if (run->grid != NULL)
    {
        CHECK(mvfxCreateGrid(s_context, run->grid, &s_grid) == mvfx_success, "the run's grid");
    }
    for (int32_t g = 0; g < run->colliderGridCount; ++g)
    {
        CHECK(mvfxCreateGrid(s_context, &run->colliderGrids[g], &s_colliderGrids[g]) ==
                  mvfx_success,
              "a collider's grid");
    }
    for (int32_t e = 0; e < run->emitterCount; ++e)
    {
        s_emitters[e] = run->emitters[e];
        for (int32_t f = 0; f < s_emitters[e].forceCount; ++f)
        {
            s_forces[e][f] = s_emitters[e].forces[f];
            if (run->grid != NULL && s_forces[e][f].kind == mvfx_forceVectorField)
            {
                s_forces[e][f].grid = s_grid;
            }
        }
        s_emitters[e].forces = s_forces[e];
        s_emitters[e].colliders =
            WithColliderGrids(s_emitters[e].colliders, s_emitters[e].colliderCount, s_colliders[e]);
    }
    return s_emitters;
}

// Gives both instances colliders of a run.
static void SetHostColliders(const mvfxCollider* colliders, int32_t count)
{
    static mvfxCollider mapped[MVFX_MAX_COLLIDERS];
    WithColliderGrids(colliders, count, mapped);
    CHECK(mvfxInstance_SetColliders(s_context, s_cpu, mapped, count) == mvfx_success &&
              mvfxInstance_SetColliders(s_context, s_planned, mapped, count) == mvfx_success,
          "the instance's colliders");
}

// Gives the run's grid new values: each doubled.
static void Animate(const mvfxGridDef* def)
{
    static float values[3 * 64];
    int64_t count = 3 * (int64_t)def->size[0] * def->size[1] * def->size[2];
    CHECK(count <= 3 * 64, "a small grid");
    for (int64_t i = 0; i < count; ++i)
    {
        values[i] = 2.0f * def->values[i];
    }
    CHECK(mvfxGrid_SetValues(s_context, s_grid, values, count) == mvfx_success, "animated");
}

mvfxGpuSim* OpenRun(mvfxGpu* kernels, const Run* run, mvfxEffectId* effectOut)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = WithGrid(run);
    effectDef.emitterCount = run->emitterCount;
    effectDef.parameters = run->parameters;
    effectDef.parameterCount = run->parameterCount;
    CHECK(mvfxCreateEffect(s_context, &effectDef, effectOut) == mvfx_success, "effect");
    for (int32_t e = 0; e < run->emitterCount; ++e)
    {
        mvfxEmitterView view;
        CHECK(mvfxEffect_GetEmitterView(s_context, *effectOut, e, &view) == mvfx_success, "a view");
        s_runArrays[e] = view.arrays;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = *effectOut;
    instanceDef.seed = 11;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_cpu) == mvfx_success, "a CPU instance");
    instanceDef.planned = true;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_planned) == mvfx_success, "planned");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = s_context;
    simDef.effect = *effectOut;
    simDef.instance = s_planned;
    simDef.hostGridBytes = run->hostGridBytes;
    mvfxGpuSim* sim = NULL;
    mvfxGpuRefusal refusal;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_success && refusal.emitter == -1,
          "the simulation");
    return sim;
}

void Compare(Gpu* gpu, mvfxGpu* kernels, const Run* run, const char* what)
{
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(kernels, run, &effect);
    s_stepsApart = 0;
    if (run->layout != NULL)
    {
        CHECK(OpenRecords(gpu, run->layout, run->recordCapacity, run->view),
              "the records' buffers");
    }
    s_sorted = run->emitters[0].sortMode >= mvfx_sortBackToFront && !run->tied;
    s_exactRecords = run->exact;
    s_byDepth = run->emitters[0].sortMeasure == mvfx_sortByDepth;
    s_placeError = 0.0f;
    s_velocityError = 0.0f;
    s_contacts = 0;
    s_events = 0;
    memset(s_dropped, 0, sizeof s_dropped);
    if (run->hostColliders != NULL)
    {
        SetHostColliders(run->hostColliders, run->hostColliderCount);
    }
    if (run->depth != NULL)
    {
        mvfxDepthImage bare = *run->depth;
        bare.pixels = NULL;
        bare.byteCount = 0;
        bare.normals = NULL;
        bare.normalByteCount = 0;
        OpenDepth(gpu, run->depth);
        CHECK(mvfxInstance_SetDepthImage(s_context, s_cpu, run->depth) == mvfx_success &&
                  mvfxInstance_SetDepthImage(s_context, s_planned, &bare) == mvfx_success,
              "the depth images");
    }
    s_boundsError = 0.0;
    s_tolerance = run->tolerance > 0.0f ? run->tolerance : 1e-4f;
    s_deathTicks = run->deathTicks;
    s_pastAge = 0;
    s_deathsApart = 0;
    s_deathsWithin = true;
    bool same = sim != NULL;
    int32_t most[2] = {0, 0};
    int32_t steps = run->steps > 0 ? run->steps : STEPS;
    if (run->far)
    {
        PlaceFar();
    }
    int32_t s = 0;
    for (; same && s < steps; ++s)
    {
        if (run->moving)
        {
            Move(s, run->pace > 0.0 ? run->pace : 1.0);
        }
        if (run->clearing && s == STEPS / 2)
        {
            Clear();
        }
        if (run->animated && s == steps / 2)
        {
            Animate(run->grid);
        }
        if (run->laterHostColliders != NULL && s == steps / 2)
        {
            SetHostColliders(run->laterHostColliders, run->laterHostColliderCount);
        }
        if (run->spawn != NULL)
        {
            SpawnFor(run, s);
        }
        CHECK(mvfxContext_StepTicks(s_context, s_stepTicks) == mvfx_success, "stepped");
        same = Simulate(gpu, sim);
        if (!same)
        {
            printf("step %d: the simulation was not recorded and finished\n", s);
        }
        for (int32_t e = 0; same && e < run->emitterCount; ++e)
        {
            if (!ReadBack(gpu, sim, e))
            {
                printf("step %d: emitter %d not read back\n", s, e);
                same = false;
                break;
            }
            same = SameAsCpu(mvfxContextInstance(s_context, s_cpu), e) && SameBounds(sim, e) &&
                   (run->emitters[e].events == 0 || (ReadEvents(gpu, sim, e) && SameEvents(e)));
            most[e] = (int32_t)s_record[0] > most[e] ? (int32_t)s_record[0] : most[e];
        }
        same = same && (run->layout == NULL || (ReadRecords(gpu) && SameRecords()));
    }
    if (!same)
    {
        printf("  differs at step %d\n", s - 1);
    }
    printf("%s: largest relative differences, place %g, velocity %g, bounds %g\n", what,
           (double)s_placeError, (double)s_velocityError, s_boundsError);
    bool eventful = run->emitters[0].events != 0;
    if (eventful)
    {
        printf("  events: %llu\n", (unsigned long long)s_events);
    }
    CHECK(same && (!eventful || s_events > 0) && most[0] >= (run->least > 0 ? run->least : 100) &&
              (run->emitterCount < 2 || most[1] >= (run->secondLeast != 0 ? run->secondLeast : 65)),
          what);
    if (run->colliding)
    {
        printf("  contacts held: %llu\n", (unsigned long long)s_contacts);
        CHECK(s_contacts > 0, "contacts");
    }
    if (run->deathTicks)
    {
        printf("  death ticks past 2^24 ticks of age: %lld, apart: %lld\n", (long long)s_pastAge,
               (long long)s_deathsApart);
        CHECK(s_pastAge > 0 && s_deathsWithin, "death ticks within their bound");
    }
    if (run->killing)
    {
        const mvfxEmitterState* state = &mvfxContextInstance(s_context, s_cpu)->emitters[0];
        printf("  killed: %llu\n", (unsigned long long)state->killed);
        CHECK(state->killed > 0, "kills");
    }
    if (run->layout != NULL)
    {
        printf("  records a step apart: %lld\n", (long long)s_stepsApart);
        CloseRecords(gpu);
    }
    mvfxDestroyGpuSim(sim);
    CloseDepth(gpu);
    mvfxDestroyContext(s_context);
}

mvfxContext* RunContext(void)
{
    return s_context;
}

mvfxInstanceId PlannedInstance(void)
{
    return s_planned;
}

int64_t StepTicks(void)
{
    return s_stepTicks;
}

void SetStepTicks(int64_t ticks)
{
    s_stepTicks = ticks;
}

const uint32_t* StatisticsRecord(void)
{
    return s_record;
}

// A scene whose frames dispatch each kernel of the step's, the events',
// the births' from events, the records' and the sort's, run with the
// warm-up's wait (every emitter's step dispatches the step's kernels): a
// device that compiles a kernel at its first dispatch does it here.
static void WarmUp(Gpu* gpu, mvfxGpu* kernels)
{
    static const mvfxCollider ceiling = {
        .kind = mvfx_colliderPlane, .center = {0, 0.55f, 0}, .normal = {0, -1, 0}};
    static const mvfxLayoutField fields[2] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 12}};
    mvfxLayoutDef layout = mvfxDefaultLayoutDef();
    layout.fields = fields;
    layout.fieldCount = 2;
    layout.stride = 16;
    mvfxEmitterDef emitters[2] = {Sprite(), Sprite()};
    emitters[0].events = mvfx_eventMaskBirth | mvfx_eventMaskDeath | mvfx_eventMaskCollision;
    emitters[0].eventCapacity = 4096;
    emitters[0].colliders = &ceiling;
    emitters[0].colliderCount = 1;
    emitters[0].collision = (mvfxCollision){.enabled = true, .restitution = 0.5f};
    emitters[0].sortMode = mvfx_sortBackToFront;
    emitters[1].rate = 0.0f;
    emitters[1].burstCount = 0;
    emitters[1].eventSpawn = (mvfxEventSpawn){
        .enabled = true, .kind = mvfx_eventDeath, .minCount = 1, .maxCount = 2, .capacity = 512};
    Compare(gpu, kernels,
            &(Run){.emitters = emitters,
                   .emitterCount = 2,
                   .moving = true,
                   .layout = &layout,
                   .recordCapacity = CAPACITY},
            "the warm-up");
}

int RunSuite(SceneSuite suite)
{
    Gpu gpu;
    if (!Open(&gpu))
    {
        Close(&gpu);
        if (getenv("MVFX_RHI_REQUIRED") != NULL)
        {
            printf("FAIL: no adapter, and MVFX_RHI_REQUIRED is set\n");
            return 1;
        }
        printf("SKIP: no adapter\n");
        return 77;
    }
    mvfxGpuDef def = mvfxDefaultGpuDef();
    def.device = gpu.device;
    mvfxGpu* kernels = NULL;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success && AwaitReady(&gpu, kernels), "kernels");
    CHECK(MakeFill(&gpu), "the depth-writing pipeline");
    WarmUp(&gpu, kernels);
    s_waitNs = FRAME_WAIT_NS;
    suite(&gpu, kernels);
    printf("longest frame after the warm-up: %.3f s\n", s_longestFrame);
    (void)mrhiDestroyGraphicsPipeline(gpu.device, s_fill);
    (void)mrhiDestroyShader(gpu.device, s_fillShader);
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
