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
#include "force.h"
#include "half.h"
#include "instance.h"
#include "instance_api.h"
#include "output.h"
#include "plexus.h"
#include "rhi_apart.h"
#include "rhi_waters.h"
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

// The longest a frame of the warm-up, or a run's first, is waited for: a
// device's first frame may compile the kernels it dispatches (lavapipe
// does, without its shader cache, a minute on a loaded machine; the unit
// pass alone took 25 s), and a run's first dispatches every kernel the
// run does, those the warm-up does not among them (units', rigid
// particles', self-collision's); and every frame after it, so a kernel
// that hangs fails in seconds.
#define WARM_WAIT_NS  300000000000ull
#define FRAME_WAIT_NS 20000000000ull
static uint64_t s_waitNs = WARM_WAIT_NS;
// Whether the warm-up is over.
static bool s_warm;

// The longest a frame after the warm-up and after a run's first took, in
// seconds.
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
    // Room for a frame that splits into the most steps
    // (MVFX_MAX_PLAN_STEPS), each a liquid gas's and its emitter's passes.
    deviceDef.deviceLimits.framePasses = 2048u;
    deviceDef.deviceLimits.frameAccesses = 32768u;
    deviceDef.deviceLimits.frameBarriers = 32768u;
    deviceDef.deviceLimits.frameCommandBytes = 8u << 20u;
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

void LongFrames(bool slow)
{
    s_waitNs = slow ? WARM_WAIT_NS : FRAME_WAIT_NS;
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
static mvfxAttributeArrays s_runArrays[RUN_EMITTERS];
// How far a rigid body's surface reaches from its center at most, each
// emitter's (0 for particles): half its largest size's diagonal; whether
// its bodies meet each other.
static float s_bodyRadius[RUN_EMITTERS];
static bool s_bodiesMeet[RUN_EMITTERS];

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
static void CompareDeath(int32_t g, int64_t birth, float inverseLife)
{
    int64_t cpu = mvfxDeathTick(birth, inverseLife);
    int32_t death = s_arrays.count;
    int64_t gpu = (int64_t)((uint64_t)Word(death, g) | (uint64_t)Word(death + 1, g) << 32u);
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

// A drawn value of the GPU's particle g against the CPU path's particle c:
// its components from its first word (none where not stored), as binary32
// within the tolerance or as words equal.
static bool SameValue(const mvfxInstance* cpu, int32_t e, int32_t g, int32_t c, int16_t first,
                      int32_t attribute, int32_t components, bool words)
{
    bool same = true;
    for (int32_t k = 0; first >= 0 && k < components; ++k)
    {
        const uint32_t* cpuWords = mvfxAttributeArray(cpu, e, (mvfxAttribute)(attribute + k));
        float cpuValue = 0.0f;
        memcpy(&cpuValue, &cpuWords[c], sizeof cpuValue);
        float gpuValue = FloatOf(first + k, g);
        same =
            same && (words ? Word(first + k, g) == cpuWords[c]
                           : fabsf(gpuValue - cpuValue) <= s_tolerance * (1.0f + fabsf(cpuValue)));
    }
    return same;
}

// Whether the neighbor attributes are compared as words, and the
// particles whose were compared over the run.
static bool s_exactNeighbors;
static int64_t s_neighborsCompared;

// The neighbor attributes of the GPU's particle g against the CPU path's
// particle c, where the emitter has them: the count exactly, the offsets
// as words or, as each is a mean less the particle's own place or
// velocity, within the tolerance of that place, or of both velocities.
static bool SameNeighbors(const mvfxInstance* cpu, int32_t e, int32_t g, int32_t c)
{
    const mvfxAttributeArrays* a = &s_arrays;
    if (a->neighborCount < 0)
    {
        return true;
    }
    ++s_neighborsCompared;
    if (s_exactNeighbors)
    {
        return SameValue(cpu, e, g, c, a->neighborCount, mvfx_attributeNeighborCount, 1, true) &&
               SameValue(cpu, e, g, c, a->neighborCenter, mvfx_attributeNeighborCenterX, 3, true) &&
               SameValue(cpu, e, g, c, a->neighborVelocity, mvfx_attributeNeighborVelocityX, 3,
                         true);
    }
    bool same = SameValue(cpu, e, g, c, a->neighborCount, mvfx_attributeNeighborCount, 1, true);
    for (int32_t k = 0; k < 3; ++k)
    {
        const float* p = mvfxAttributeArray(cpu, e, (mvfxAttribute)(mvfx_attributePositionX + k));
        const float* v = mvfxAttributeArray(cpu, e, (mvfxAttribute)(mvfx_attributeVelocityX + k));
        const float* center =
            mvfxAttributeArray(cpu, e, (mvfxAttribute)(mvfx_attributeNeighborCenterX + k));
        const float* mean =
            mvfxAttributeArray(cpu, e, (mvfxAttribute)(mvfx_attributeNeighborVelocityX + k));
        // The mean velocity is the neighbors', each within the tolerance of
        // its own size, as the particle's is of its.
        float dc = fabsf(FloatOf(a->neighborCenter + k, g) - center[c]) / (1.0f + fabsf(p[c]));
        float dv = fabsf(FloatOf(a->neighborVelocity + k, g) - mean[c]) /
                   (2.0f + fabsf(v[c]) + fabsf(mean[c] + v[c]));
        same = same && dc <= s_tolerance && dv <= s_tolerance;
    }
    return same;
}

// Every value a birth draws, as moved since, of the GPU's particle g
// against the CPU path's particle c, its affine matrix (a grid liquid's)
// and its neighbor attributes.
static bool SameValues(const mvfxInstance* cpu, int32_t e, int32_t g, int32_t c)
{
    const mvfxAttributeArrays* a = &s_arrays;
    const mvfxCompiledEmitter* compiled = &cpu->effect->emitters[e];
    bool same = SameValue(cpu, e, g, c, a->size, mvfx_attributeSizeX, 3, false) &&
                SameValue(cpu, e, g, c, a->color, mvfx_attributeColorR, 4, false) &&
                SameValue(cpu, e, g, c, a->frame, mvfx_attributeFrame, 1, true) &&
                SameValue(cpu, e, g, c, a->mesh, mvfx_attributeMesh, 1, true) &&
                SameValue(cpu, e, g, c, a->angle, mvfx_attributeAngle, 1, false) &&
                SameValue(cpu, e, g, c, a->spin, mvfx_attributeSpin, 1, false) &&
                SameValue(cpu, e, g, c, a->orientation, mvfx_attributeOrientationX, 4, false) &&
                SameValue(cpu, e, g, c, a->angular, mvfx_attributeAngularX, 3, false) &&
                SameValue(cpu, e, g, c, a->drag, mvfx_attributeDrag, 1, false) &&
                SameValue(cpu, e, g, c, a->sleep, mvfx_attributeSleep, 4, false) &&
                SameValue(cpu, e, g, c, a->affine, mvfx_attributeAffine, 9, false);
    for (int32_t n = 0; same && n < compiled->customCount; ++n)
    {
        same = SameValue(cpu, e, g, c, a->custom[n], mvfx_attributeCustom + 4 * n,
                         compiled->custom[n].components, false);
    }
    return same && SameNeighbors(cpu, e, g, c);
}

// Ribbons compared up to their ids (sim.h): a break by distance whose gap
// lies within the tolerance of the break distance may happen on one path
// only, and the counter's ids after it then differ by one. Each emitter's
// shifts (from a serial on, the CPU path's counter ids less the GPU's),
// the ids of the chains through a birth so decided (their distances not
// compared after), the latest particle of the counter's ribbon seen in
// serial order this step, and the run's births so decided.
#define MOST_SHIFTS 16
typedef struct RibbonState
{
    int32_t shifts;
    uint32_t from[MOST_SHIFTS];
    uint32_t shift[MOST_SHIFTS];
    uint32_t apartGpu[MOST_SHIFTS];
    uint32_t apartCpu[MOST_SHIFTS];
    bool seen;
    uint32_t gpu;
    uint32_t cpu;
    float gpuAlong;
    float cpuAlong;
} RibbonState;

static RibbonState s_ribbons[RUN_EMITTERS];
static int32_t s_breaksApart;

// Whether an emitter's ribbon id is the counter's: every id by the
// counter, those with the top bit by source, none by batch.
static bool IsCounterId(const mvfxCompiledEmitter* compiled, uint32_t id)
{
    return compiled->ribbonIds == mvfx_ribbonOne ||
           (compiled->ribbonIds == mvfx_ribbonPerSource && (id & 0x80000000u) != 0);
}

// The shift of the counter's ids for a particle of a serial.
static uint32_t ShiftAt(const RibbonState* r, uint32_t serial)
{
    uint32_t shift = 0;
    for (int32_t k = 0; k < r->shifts && serial >= r->from[k]; ++k)
    {
        shift = r->shift[k];
    }
    return shift;
}

static bool IsApartChain(const RibbonState* r, uint32_t gpu, uint32_t cpu)
{
    for (int32_t k = 0; k < r->shifts; ++k)
    {
        if (gpu == r->apartGpu[k] || cpu == r->apartCpu[k])
        {
            return true;
        }
    }
    return false;
}

// The ribbon and distance of the GPU's particle g against the CPU path's
// particle c, pairs taken in serial order: a fixed id equal; a counter id
// equal up to its shift, which changes at a birth only where one path
// broke its ribbon and the other did not, the gap (the distance the
// joining path added) within the tolerance of the break distance; a
// distance within the tolerance but on a chain through such a birth.
static bool SameRibbon(const mvfxInstance* cpu, int32_t e, int32_t g, int32_t c)
{
    const mvfxAttributeArrays* a = &s_arrays;
    if (a->ribbon < 0)
    {
        return true;
    }
    const mvfxCompiledEmitter* compiled = &cpu->effect->emitters[e];
    const uint32_t* ids = mvfxAttributeArray(cpu, e, mvfx_attributeRibbon);
    const float* alongs =
        a->ribbonDistance >= 0 ? mvfxAttributeArray(cpu, e, mvfx_attributeRibbonDistance) : NULL;
    RibbonState* r = &s_ribbons[e];
    uint32_t gpu = Word(a->ribbon, g);
    uint32_t cpuId = ids[c];
    float gpuAlong = alongs != NULL ? FloatOf(a->ribbonDistance, g) : 0.0f;
    float cpuAlong = alongs != NULL ? alongs[c] : 0.0f;
    if (!IsCounterId(compiled, cpuId))
    {
        return gpu == cpuId && (alongs == NULL || fabsf(gpuAlong - cpuAlong) <=
                                                      s_tolerance * (1.0f + fabsf(cpuAlong)));
    }
    uint32_t serial = Word(a->serial, g);
    bool same = gpu + ShiftAt(r, serial) == cpuId;
    if (!same && r->seen && alongs != NULL && r->shifts < MOST_SHIFTS)
    {
        bool gpuBroke = gpu != r->gpu;
        bool cpuBroke = cpuId != r->cpu;
        float gap = gpuBroke ? cpuAlong - r->cpuAlong : gpuAlong - r->gpuAlong;
        float d = compiled->ribbonBreakDistance;
        float near = 4.0f * s_tolerance * (1.0f + d + fabsf(gpuBroke ? cpuAlong : gpuAlong));
        if (gpuBroke != cpuBroke && fabsf(gap - d) <= near)
        {
            r->from[r->shifts] = serial;
            r->shift[r->shifts] = cpuId - gpu;
            r->apartGpu[r->shifts] = gpu;
            r->apartCpu[r->shifts] = cpuId;
            r->shifts += 1;
            s_breaksApart += 1;
            same = true;
        }
    }
    if (same && alongs != NULL && !IsApartChain(r, gpu, cpuId))
    {
        same = fabsf(gpuAlong - cpuAlong) <= s_tolerance * (1.0f + fabsf(cpuAlong));
    }
    r->seen = true;
    r->gpu = gpu;
    r->cpu = cpuId;
    r->gpuAlong = gpuAlong;
    r->cpuAlong = cpuAlong;
    return same;
}

// The context of a run: a CPU instance and a planned one side by side.
static mvfxContext* s_context;
static mvfxInstanceId s_cpu;
static mvfxInstanceId s_planned;

// The GPU's particle g against the CPU path's particle c of the same
// serial: ticks, life and contacts equal, places, velocities and values
// within the tolerance.
static bool SameParticle(const mvfxInstance* cpu, int32_t e, int32_t g, int32_t c)
{
    const uint32_t* low = mvfxAttributeArray(cpu, e, mvfx_attributeBirthLow);
    const uint32_t* high = mvfxAttributeArray(cpu, e, mvfx_attributeBirthHigh);
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
    uint32_t lifeBits;
    memcpy(&lifeBits, &inverseLife[c], sizeof lifeBits);
    const mvfxAttributeArrays* a = &s_arrays;
    int64_t gpuBirth = (int64_t)((uint64_t)Word(a->birth + 1, g) << 32u | Word(a->birth, g));
    int64_t cpuBirth = (int64_t)((uint64_t)high[c] << 32u | low[c]);
    bool same = llabs(gpuBirth - cpuBirth) <= slack && Word(a->inverseLife, g) == lifeBits &&
                (collisions == NULL || Word(a->collisions, g) == collisions[c]);
    if (s_deathTicks)
    {
        CompareDeath(g, cpuBirth, inverseLife[c]);
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        const float* p = mvfxAttributeArray(cpu, e, places[k]);
        const float* v = mvfxAttributeArray(cpu, e, velocities[k]);
        // Relative to the size, as places reach the rebase distance.
        float dp = fabsf(FloatOf(a->position + k, g) - p[c]) / (1.0f + fabsf(p[c]));
        float dv = fabsf(FloatOf(a->velocity + k, g) - v[c]) / (1.0f + fabsf(v[c]));
        s_placeError = dp > s_placeError ? dp : s_placeError;
        s_velocityError = dv > s_velocityError ? dv : s_velocityError;
        same = same && dp <= s_tolerance && dv <= s_tolerance;
    }
    return same && SameValues(cpu, e, g, c);
}

// The birth tick of the GPU's particle g, and of the CPU path's c.
static int64_t GpuBirth(int32_t g)
{
    return (int64_t)((uint64_t)Word(s_arrays.birth + 1, g) << 32u | Word(s_arrays.birth, g));
}

static int64_t CpuBirth(const mvfxInstance* cpu, int32_t e, int32_t c)
{
    const uint32_t* low = mvfxAttributeArray(cpu, e, mvfx_attributeBirthLow);
    const uint32_t* high = mvfxAttributeArray(cpu, e, mvfx_attributeBirthHigh);
    return (int64_t)((uint64_t)high[c] << 32u | low[c]);
}

// The most particles of an emitter spawning from events left out at a
// step, born from uncertain events, over the run; and whether each
// emitter's step left any particle out, whose bounds then go uncompared.
static int32_t s_childrenLeft;
static bool s_leftOut[RUN_EMITTERS];

// Whether a spawning emitter's particles on the GPU are the CPU path's:
// those born at an uncertain event's tick (rhi_apart.h) left out on both
// paths, the rest the same in order (SameParticle), each pair's serials
// apart by an offset that changes only across an uncertain tick (the
// births one path gave alone took serials).
static bool SameChildren(const mvfxInstance* cpu, int32_t e)
{
    s_ribbons[e].seen = false;
    const mvfxEmitterState* state = &cpu->emitters[e];
    const uint32_t* serial = mvfxAttributeArray(cpu, e, mvfx_attributeSerial);
    int64_t slack = 1 + s_stepTicks / 1000;
    int32_t held = (int32_t)s_record[0];
    bool same = s_record[2] == 0;
    bool paired = false;
    int64_t offset = 0;
    int64_t lastBirth = 0;
    int32_t left = 0;
    int32_t g = 0;
    int32_t c = 0;
    while (same && (g < held || c < state->count))
    {
        if (g < held && IsUncertain(GpuBirth(g), slack))
        {
            ++g;
            ++left;
            continue;
        }
        if (c < state->count && IsUncertain(CpuBirth(cpu, e, c), slack))
        {
            ++c;
            ++left;
            continue;
        }
        same = g < held && c < state->count;
        int64_t birth = same ? CpuBirth(cpu, e, c) : 0;
        int64_t apart = same ? (int64_t)Word(s_arrays.serial, g) - (int64_t)serial[c] : 0;
        // The first pair's offset counts the births given alone before it.
        same =
            same && (paired ? apart == offset || UncertainBetween(lastBirth, birth, slack)
                            : apart == 0 || UncertainBetween(INT64_MIN + slack + 1, birth, slack));
        same = same && SameParticle(cpu, e, g, c) && SameRibbon(cpu, e, g, c);
        if (!same)
        {
            printf("child %d of %d (GPU), %d of %d (CPU) differs\n", g, held, c, state->count);
        }
        paired = true;
        offset = apart;
        lastBirth = birth;
        ++g;
        ++c;
    }
    s_childrenLeft = left > s_childrenLeft ? left : s_childrenLeft;
    s_leftOut[e] = left > 0;
    return same;
}

// The planned instance's plan of an emitter for the last step of the
// context's (the whole step's when it is one).
static mvfxResult LastPlan(int32_t e, mvfxEmitterPlan* plan)
{
    int32_t steps = 0;
    mvfxResult result = mvfxInstance_GetPlanSteps(s_context, s_planned, &steps);
    return result == mvfx_success
               ? mvfxInstance_GetEmitterStepPlan(s_context, s_planned, e, steps - 1, plan)
               : result;
}

// Whether the GPU's particles are the CPU path's, merged by serial (both
// pools keep serial order): each pair the same (SameParticle), and a
// particle that differs, or that one path holds alone, only one gone
// apart (rhi_apart.h), compared no more.
static bool SameAsCpu(const mvfxInstance* cpu, int32_t e)
{
    s_leftOut[e] = false;
    s_ribbons[e].seen = false;
    if (cpu->effect->emitters[e].eventSpawn.enabled)
    {
        return SameChildren(cpu, e);
    }
    const mvfxEmitterState* state = &cpu->emitters[e];
    const uint32_t* serial = mvfxAttributeArray(cpu, e, mvfx_attributeSerial);
    const mvfxAttributePlan* attributes = &cpu->effect->emitters[e].plan;
    const uint32_t* collisions = mvfxHasAttribute(attributes, mvfx_attributeCollisions)
                                     ? mvfxAttributeArray(cpu, e, mvfx_attributeCollisions)
                                     : NULL;
    const float* places[3] = {mvfxAttributeArray(cpu, e, mvfx_attributePositionX),
                              mvfxAttributeArray(cpu, e, mvfx_attributePositionY),
                              mvfxAttributeArray(cpu, e, mvfx_attributePositionZ)};
    mvfxEmitterPlan plan;
    if (LastPlan(e, &plan) != mvfx_success || s_record[2] != 0)
    {
        printf("no plan, or births refused: %u\n", s_record[2]);
        return false;
    }
    ApartTakeStep(e, serial, places, state->count, &plan, s_tolerance, s_bodyRadius[e],
                  s_bodiesMeet[e]);
    int32_t held = (int32_t)s_record[0];
    bool same = true;
    int32_t g = 0;
    int32_t c = 0;
    while (same && (g < held || c < state->count))
    {
        uint32_t gs = g < held ? Word(s_arrays.serial, g) : UINT32_MAX;
        uint32_t cs = c < state->count ? serial[c] : UINT32_MAX;
        uint32_t at = gs < cs ? gs : cs;
        bool apart = IsApart(e, at);
        if (gs == cs && !apart)
        {
            s_contacts += collisions != NULL ? collisions[c] : 0u;
            same = SameParticle(cpu, e, g, c) && SameRibbon(cpu, e, g, c);
        }
        // One path holds it alone: only one that may go apart.
        same = same && (gs == cs || apart);
        bool gone = !same && GoApart(e, at);
        s_leftOut[e] = s_leftOut[e] || apart || gone || gs != cs;
        same = same || apart || gone;
        if (!same)
        {
            printf("particle of serial %u differs (GPU %d of %d, CPU %d of %d)\n", at, g, held, c,
                   state->count);
        }
        g += gs <= cs ? 1 : 0;
        c += cs <= gs ? 1 : 0;
    }
    return same;
}

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

// A run's ribbon points instead (s_ribbonPoints), or strips (s_strips):
// the list's buffer (the list or the indices from its word LIST_FIRST),
// the most ribbons or indices it holds, the GPU's read back and the CPU
// path's, filled first as the records are.
#define LIST_RIBBONS 4096
#define LIST_FIRST   4
static bool s_ribbonPoints;
static bool s_strips;
static bool s_plexus;
static mvfxTubeDef s_tube;
static bool s_sprites;
static bool s_sheets;
static int32_t s_recordEmitter;
static mrhiBufferId s_listBuffer;
static mrhiResourceId s_listResource;
static int64_t s_ribbonCapacity;
static uint32_t s_gpuList[4 * LIST_RIBBONS + LIST_FIRST];
static mvfxRibbon s_cpuList[LIST_RIBBONS];
static uint32_t s_cpuIndices[4 * LIST_RIBBONS];

// Whether a run's records are lights, its layout then the harness's own
// (positions, read by none); the most lights at a step of the run.
static bool s_lights;
static int64_t s_mostLights;
static const mvfxLayoutField s_lightField = {
    .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0};

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
    mrhiBufferDef list = def;
    list.size = sizeof s_gpuList;
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
           mrhiCreateBuffer(gpu->device, &count, &s_countBuffer) == mrhi_success &&
           mrhiCreateBuffer(gpu->device, &list, &s_listBuffer) == mrhi_success;
}

static void CloseRecords(Gpu* gpu)
{
    (void)mrhiDestroyBuffer(gpu->device, s_recordBuffer);
    (void)mrhiDestroyBuffer(gpu->device, s_countBuffer);
    (void)mrhiDestroyBuffer(gpu->device, s_listBuffer);
    s_layout = (mvfxLayoutId){0};
}

// Adds the first emitter's ribbon pass: its points where the records go,
// its list in the list's buffer, its counts in the count buffer.
static bool AddRibbonPass(mvfxGpuSim* sim, mrhiResourceId count)
{
    mvfxGpuRibbonsDef ribbons = mvfxDefaultGpuRibbonsDef();
    ribbons.emitter = s_recordEmitter;
    ribbons.layout = s_layout;
    ribbons.view = s_view;
    ribbons.points = s_recordResource;
    ribbons.pointOffset = 64;
    ribbons.pointCapacity = s_recordCapacity;
    ribbons.ribbons = s_listResource;
    ribbons.ribbonOffset = 4u * LIST_FIRST;
    ribbons.ribbonCapacity = s_ribbonCapacity;
    ribbons.counts = count;
    if (!s_strips)
    {
        return (s_plexus ? mvfxGpuSim_AddPlexus(sim, &ribbons)
                         : mvfxGpuSim_AddRibbons(sim, &ribbons)) == mvfx_success;
    }
    mvfxGpuExpandDef strips = mvfxDefaultGpuExpandDef();
    strips.emitter = s_recordEmitter;
    strips.layout = s_layout;
    strips.view = s_view;
    strips.vertices = s_recordResource;
    strips.vertexOffset = 64;
    strips.vertexCapacity = s_recordCapacity;
    strips.indices = s_listResource;
    strips.indexOffset = 4u * LIST_FIRST;
    strips.indexCapacity = s_ribbonCapacity;
    strips.counts = count;
    strips.tube = s_tube;
    if (s_sprites)
    {
        return mvfxGpuSim_ExpandSprites(sim, &strips) == mvfx_success;
    }
    if (s_sheets)
    {
        return mvfxGpuSim_ExpandSheets(sim, &strips) == mvfx_success;
    }
    if (s_plexus)
    {
        return mvfxGpuSim_ExpandPlexus(sim, &strips) == mvfx_success;
    }
    return (s_tube.sides > 0 ? mvfxGpuSim_ExpandTubes(sim, &strips)
                             : mvfxGpuSim_ExpandRibbons(sim, &strips)) == mvfx_success;
}

// Adds the fill of the records and list buffers and the first emitter's
// record pass, its count at the count buffer's second word (or its ribbon
// pass); the fill's pass.
static bool AddRecordPasses(mvfxGpuSim* sim, mrhiDevice* device, mrhiPassId* fill)
{
    mrhiResourceId count = {0};
    if (mrhiImportBuffer(device, s_recordBuffer, &s_recordResource) != mrhi_success ||
        mrhiImportBuffer(device, s_countBuffer, &count) != mrhi_success ||
        mrhiImportBuffer(device, s_listBuffer, &s_listResource) != mrhi_success)
    {
        return false;
    }
    const mrhiAccess written[2] = {
        {.resource = s_recordResource, .kind = mrhi_accessCopyDestination},
        {.resource = s_listResource, .kind = mrhi_accessCopyDestination}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = written;
    def.accessCount = 2;
    def.neverCull = true;
    if (s_ribbonPoints)
    {
        return mrhiAddPass(device, &def, fill) == mrhi_success && AddRibbonPass(sim, count);
    }
    if (s_lights)
    {
        mvfxGpuLightsDef lights = mvfxDefaultGpuLightsDef();
        lights.emitter = s_recordEmitter;
        lights.view = s_view;
        lights.lights = s_recordResource;
        lights.offset = 64;
        lights.capacity = s_recordCapacity;
        lights.count = count;
        lights.countOffset = 4;
        return mrhiAddPass(device, &def, fill) == mrhi_success &&
               mvfxGpuSim_AddLights(sim, &lights) == mvfx_success;
    }
    mvfxGpuRecordsDef records = mvfxDefaultGpuRecordsDef();
    records.emitter = s_recordEmitter;
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
                                    sizeof s_fillBytes) == mrhi_success &&
                    mrhiWriteBuffer(device, fill, s_listResource, 0, s_fillBytes,
                                    sizeof s_gpuList) == mrhi_success;
    return mrhiEndPass(device, fill) == mrhi_success && recorded;
}

// Reads the GPU's records, count and list back.
static bool ReadRecords(Gpu* gpu)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId buffers[3] = {{0}, {0}, {0}};
    mrhiPassId pass = {0};
    mrhiRequestId reads[3] = {{0}, {0}, {0}};
    if (mrhiBeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    bool recorded = mrhiImportBuffer(gpu->device, s_recordBuffer, &buffers[0]) == mrhi_success &&
                    mrhiImportBuffer(gpu->device, s_countBuffer, &buffers[1]) == mrhi_success &&
                    mrhiImportBuffer(gpu->device, s_listBuffer, &buffers[2]) == mrhi_success;
    const mrhiAccess reading[3] = {{.resource = buffers[0], .kind = mrhi_accessCopySource},
                                   {.resource = buffers[1], .kind = mrhi_accessCopySource},
                                   {.resource = buffers[2], .kind = mrhi_accessCopySource}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = reading;
    def.accessCount = 3;
    def.neverCull = true;
    recorded =
        recorded && mrhiAddPass(gpu->device, &def, &pass) == mrhi_success &&
        mrhiCompileFrame(gpu->device) == mrhi_success &&
        mrhiBeginPass(gpu->device, pass) == mrhi_success &&
        mrhiReadBuffer(gpu->device, pass, buffers[0], 0, RECORD_BYTES, &reads[0]) == mrhi_success &&
        mrhiReadBuffer(gpu->device, pass, buffers[1], 0, sizeof s_gpuCount, &reads[1]) ==
            mrhi_success &&
        mrhiReadBuffer(gpu->device, pass, buffers[2], 0, sizeof s_gpuList, &reads[2]) ==
            mrhi_success &&
        mrhiEndPass(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    size_t taken = 0;
    size_t counted = 0;
    size_t listed = 0;
    return Finish(gpu) &&
           mrhiTakeReadback(gpu->device, reads[0], s_gpuRecords, sizeof s_gpuRecords, &taken) ==
               mrhi_success &&
           mrhiTakeReadback(gpu->device, reads[1], s_gpuCount, sizeof s_gpuCount, &counted) ==
               mrhi_success &&
           mrhiTakeReadback(gpu->device, reads[2], s_gpuList, sizeof s_gpuList, &listed) ==
               mrhi_success &&
           taken == RECORD_BYTES && counted == sizeof s_gpuCount && listed == sizeof s_gpuList;
}

// A run's emitters' events read back from the GPU (each emitter's, as
// its particles are, after its step), and the CPU path's count of those
// dropped by the steps before.
#define MOST_EVENTS 4096
static uint32_t s_eventWords[MOST_EVENTS * MVFX_GPU_EVENT_WORDS];
static uint64_t s_dropped[MVFX_MAX_EMITTERS];
// The CPU path's events over the run.
static uint64_t s_events;

// Reads bytes of a buffer from a byte into out, of room bytes.
bool ReadRange(Gpu* gpu, mrhiBufferId source, uint64_t first, uint64_t bytes, void* out,
               size_t room)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId buffer = {0};
    mrhiPassId pass = {0};
    mrhiRequestId read = {0};
    if (bytes > room || mrhiBeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    bool recorded = mrhiImportBuffer(gpu->device, source, &buffer) == mrhi_success;
    const mrhiAccess reading[1] = {{.resource = buffer, .kind = mrhi_accessCopySource}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = reading;
    def.accessCount = 1;
    def.neverCull = true;
    recorded = recorded && mrhiAddPass(gpu->device, &def, &pass) == mrhi_success &&
               mrhiCompileFrame(gpu->device) == mrhi_success &&
               mrhiBeginPass(gpu->device, pass) == mrhi_success &&
               mrhiReadBuffer(gpu->device, pass, buffer, first, bytes, &read) == mrhi_success &&
               mrhiEndPass(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    size_t taken = 0;
    return Finish(gpu) && mrhiTakeReadback(gpu->device, read, out, room, &taken) == mrhi_success &&
           taken == bytes;
}

// Reads an emitter's events back, when it writes any.
static bool ReadEvents(Gpu* gpu, const mvfxGpuSim* sim, int32_t emitter)
{
    mvfxGpuParticles particles;
    if (mvfxGpuSim_GetParticles(sim, emitter, &particles) != mvfx_success ||
        particles.eventCapacity > MOST_EVENTS)
    {
        return false;
    }
    uint64_t bytes = (uint64_t)particles.eventCapacity * MVFX_GPU_EVENT_WORDS * sizeof(uint32_t);
    return particles.events.index1 == 0 ||
           ReadRange(gpu, particles.events, 4u * (uint64_t)particles.eventFirst, bytes,
                     s_eventWords, sizeof s_eventWords);
}

// Whether two floats agree within the run's tolerance, relative to the
// CPU path's.
static bool Near(float gpu, float cpu)
{
    return fabsf(gpu - cpu) <= s_tolerance * (1.0f + fabsf(cpu));
}

// How far apart a collision's or a crossing of a water surface's ticks
// may be on the two paths: a thousandth
// of the step's ticks (at least one: its share of the move is a float of
// the GPU's, and a short move's share is the least sure); for an emitter
// of units, a substep's ticks, as a unit point's place parts from the CPU
// path's by the projections' rounding and the share of its move moves by
// that over its speed, unbounded for a slow point (its place is compared
// within the tolerance).
static int64_t s_contactSlack;

// Whether an event the GPU wrote (its words) is the CPU path's: words
// equal, a collision's tick within the contact slack, the rest within the
// tolerance, the place from the plan's anchor after.
static bool SameEvent(const uint32_t* w, const mvfxEvent* cpu, const double anchor[3])
{
    uint64_t tick = ((uint64_t)w[3] << 32u) | w[2];
    int64_t apart = (int64_t)tick - cpu->tick;
    bool same =
        (w[0] & 0xFFu) == cpu->kind && ((w[0] >> 8u) != 0) == cpu->killed && w[1] == cpu->serial &&
        (apart == 0 || ((cpu->kind == mvfx_eventCollision || cpu->kind == mvfx_eventWater) &&
                        llabs(apart) <= s_contactSlack));
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
// Whether an emitter of the effect spawns from the source's events.
static bool SpawnedFrom(const mvfxInstance* cpu, int32_t source)
{
    bool spawned = false;
    for (int32_t e = 0; e < cpu->effect->emitterCount; ++e)
    {
        const mvfxEventSpawn* spawn = &cpu->effect->emitters[e].eventSpawn;
        spawned = spawned || (spawn->enabled && spawn->source == source);
    }
    return spawned;
}

static bool SameEvents(int32_t emitter)
{
    const mvfxEvent* events = NULL;
    int32_t count = 0;
    mvfxEmitterStats stats;
    mvfxEmitterPlan plan;
    if (mvfxInstance_GetEvents(s_context, s_cpu, emitter, &events, &count) != mvfx_success ||
        mvfxInstance_GetEmitterStats(s_context, s_cpu, emitter, &stats) != mvfx_success ||
        LastPlan(emitter, &plan) != mvfx_success)
    {
        return false;
    }
    uint64_t dropped = stats.eventsDropped - s_dropped[emitter];
    s_dropped[emitter] = stats.eventsDropped;
    s_events += count;
    // The events of particles gone apart left out on both paths, each
    // other the same in order. Those, and events whose ticks differ where
    // an emitter spawns from them (their births' count and draws follow
    // from the tick), are uncertain.
    bool spawned = SpawnedFrom(mvfxContextInstance(s_context, s_cpu), emitter);
    int32_t written = (int32_t)s_record[MVFX_GPU_EVENTS_WRITTEN];
    bool same = s_record[MVFX_GPU_EVENTS_WRITTEN + 1] == dropped;
    int32_t g = 0;
    for (int32_t e = 0; same && e <= count; ++e)
    {
        while (g < written && IsApart(emitter, s_eventWords[g * MVFX_GPU_EVENT_WORDS + 1]))
        {
            const uint32_t* skipped = s_eventWords + g++ * MVFX_GPU_EVENT_WORDS;
            TakeUncertain((int64_t)(((uint64_t)skipped[3] << 32u) | skipped[2]));
        }
        if (e < count && IsApart(emitter, events[e].serial))
        {
            TakeUncertain(events[e].tick);
            continue;
        }
        if (e == count || g == written)
        {
            same = e == count && g == written;
            break;
        }
        const uint32_t* w = s_eventWords + g++ * MVFX_GPU_EVENT_WORDS;
        // A near particle's event decided apart takes it apart.
        same = SameEvent(w, &events[e], plan.anchorAfter) ||
               (w[1] == events[e].serial && GoApart(emitter, events[e].serial));
        int64_t tick = (int64_t)(((uint64_t)w[3] << 32u) | w[2]);
        if (tick != events[e].tick && spawned)
        {
            TakeUncertain(tick);
            TakeUncertain(events[e].tick);
        }
        if (!same)
        {
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

static bool SameField(const mvfxLayoutField* field, const uint8_t* cpu, const uint8_t* gpu,
                      double slack)
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
            same = apart <= (double)s_tolerance * (1.0 + fabs(a)) + slack;
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

// The records a sorted unit takes: a quad's four vertices for sprites,
// else one; a unit's measure, of its place (a quad's: the midpoint of its
// first and last corners, its center).
static int64_t UnitRecords(void)
{
    return s_sprites ? 4 : 1;
}

static double UnitMeasure(const uint8_t* unit)
{
    if (!s_sprites)
    {
        return MeasureOf(unit);
    }
    float first[3];
    float last[3];
    memcpy(first, unit, sizeof first);
    memcpy(last, unit + 3 * s_layoutView.stride, sizeof last);
    uint8_t center[3 * sizeof(float)];
    for (int32_t k = 0; k < 3; ++k)
    {
        float mid = (float)(0.5 * ((double)first[k] + (double)last[k]));
        memcpy(center + k * sizeof(float), &mid, sizeof mid);
    }
    return MeasureOf(center);
}

// Each of the GPU's units (records, or quads of four) against the CPU
// path's of its serial: the CPU path's measure of the GPU's unit at each
// place the same as of its own within 1e-5 (so the orders differ only
// where measures do by less, as sim.h states; the places, held to the
// tolerance, are the fields'); a serial the CPU path did not write only
// at the cut, its measure within that of the last unit's.
static bool SameSorted(int64_t written)
{
    int64_t size = UnitRecords() * s_layoutView.stride;
    int64_t units = written / UnitRecords();
    int64_t room = s_sprites ? (s_recordCapacity / 4 < s_ribbonCapacity / 6 ? s_recordCapacity / 4
                                                                            : s_ribbonCapacity / 6)
                             : s_recordCapacity;
    for (int64_t c = 0; c < units; ++c)
    {
        s_serials[c] =
            (Serial){.serial = SerialOf(s_cpuRecords + 64 + c * size), .record = c * UnitRecords()};
    }
    qsort(s_serials, (size_t)units, sizeof s_serials[0], BySerial);
    bool same = true;
    for (int64_t r = 0; same && r < units; ++r)
    {
        const uint8_t* gpu = s_gpuRecords + 64 + r * size;
        const uint8_t* cpu = CpuRecordOf(SerialOf(gpu), units);
        double cpuMeasure = UnitMeasure(s_cpuRecords + 64 + r * size);
        double measure = UnitMeasure(cpu != NULL ? cpu : gpu);
        same = fabs(measure - cpuMeasure) <= 1e-5 * (1.0 + fabs(cpuMeasure));
        double last = UnitMeasure(s_cpuRecords + 64 + (units - 1) * size);
        same = same && (cpu != NULL || (units == room && fabs(UnitMeasure(gpu) - last) <=
                                                             1e-5 * (1.0 + fabs(last))));
        for (int64_t v = 0; same && cpu != NULL && v < UnitRecords(); ++v)
        {
            for (int32_t f = 0; same && f < s_layoutView.fieldCount; ++f)
            {
                same = SameField(&s_layoutView.fields[f], cpu + v * s_layoutView.stride,
                                 gpu + v * s_layoutView.stride, 0.0);
            }
        }
        if (!same)
        {
            printf("sorted record %lld of %lld differs\n", (long long)r, (long long)units);
        }
    }
    return same;
}

// The GPU's records and count against the CPU path's: each record's
// fields (in the same order, or by serial where sorted), every other byte
// the fill's.
// The CPU path's places and distances of the emitter whose ribbons are
// written, kept while the GPU's stand in for them.
#define MOST_POINTS 16384
static float s_keptPlaces[4][MOST_POINTS];

// Swaps the GPU's places and ribbon distances (read back last) into the
// CPU path's emitter, or back: false when its pool is not the GPU's,
// serial by serial. A point's frame turns with the difference of its
// neighbours' places, so the paths' particles, alike within the
// tolerance, would move it by as much over their distance; from the same
// places the points compare the kernels' sums and turns alone.
static bool SwapPlaces(bool in)
{
    mvfxInstance* cpu = mvfxContextInstance(s_context, s_cpu);
    int32_t e = s_recordEmitter;
    int32_t count = cpu->emitters[e].count;
    const uint32_t* serials = mvfxAttributeArray(cpu, e, mvfx_attributeSerial);
    bool alike = (int32_t)s_record[0] == count && count <= MOST_POINTS;
    for (int32_t i = 0; alike && i < count; ++i)
    {
        alike = Word(s_arrays.serial, i) == serials[i];
    }
    if (!alike)
    {
        printf("the pools differ: %d particles, GPU %u\n", count, s_record[0]);
        return false;
    }
    static const mvfxAttribute swapped[4] = {mvfx_attributePositionX, mvfx_attributePositionY,
                                             mvfx_attributePositionZ, mvfx_attributeRibbonDistance};
    const int32_t gpu[4] = {s_arrays.position, s_arrays.position + 1, s_arrays.position + 2,
                            s_arrays.ribbonDistance};
    for (int32_t k = 0; k < 4; ++k)
    {
        float* values = gpu[k] >= 0 ? mvfxAttributeArray(cpu, e, swapped[k]) : NULL;
        for (int32_t i = 0; values != NULL && i < count; ++i)
        {
            if (in)
            {
                s_keptPlaces[k][i] = values[i];
            }
            values[i] = in ? FloatOf(gpu[k], i) : s_keptPlaces[k][i];
        }
    }
    return true;
}

// SwapPlaces for an output, and for plexus links their links found again
// from the places swapped in (the drops kept), so both paths write the
// same links.
static bool SwapOutputPlaces(bool in)
{
    if (!SwapPlaces(in))
    {
        return false;
    }
    if (s_plexus)
    {
        mvfxInstance* cpu = mvfxContextInstance(s_context, s_cpu);
        uint64_t kept = cpu->emitters[s_recordEmitter].linksDropped;
        mvfxFindLinks(cpu, s_recordEmitter);
        cpu->emitters[s_recordEmitter].linksDropped = kept;
    }
    return true;
}

// The most rigid bodies of the first emitter the GPU held asleep at a
// step over a run, those it woke (asleep at a step, awake at the next),
// and which serials (modulo SLEEP_SERIALS) were asleep at the last step.
#define SLEEP_SERIALS 4096
static int32_t s_mostAsleep;
static int32_t s_woken;
static uint8_t s_wasAsleep[SLEEP_SERIALS];

// Counts the GPU's sleeping bodies of the first emitter at this step.
static void CountSleep(int32_t count)
{
    static uint8_t asleep[SLEEP_SERIALS];
    memset(asleep, 0, sizeof asleep);
    int32_t now = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        uint32_t serial = Word(s_arrays.serial, i) % SLEEP_SERIALS;
        asleep[serial] = FloatOf(s_arrays.sleep, i) < 0.0f;
        now += asleep[serial];
        s_woken += s_wasAsleep[serial] && !asleep[serial];
    }
    memcpy(s_wasAsleep, asleep, sizeof asleep);
    s_mostAsleep = now > s_mostAsleep ? now : s_mostAsleep;
}

// Gives the CPU path's emitter the GPU's places and velocities (and a
// rigid body's turn and sleep), read back last: false when its pool is
// not the GPU's, serial by serial.
static bool AdoptGpu(int32_t e)
{
    mvfxInstance* cpu = mvfxContextInstance(s_context, s_cpu);
    int32_t count = cpu->emitters[e].count;
    const uint32_t* serials = mvfxAttributeArray(cpu, e, mvfx_attributeSerial);
    bool alike = (int32_t)s_record[0] == count;
    for (int32_t i = 0; alike && i < count; ++i)
    {
        alike = Word(s_arrays.serial, i) == serials[i];
    }
    if (!alike)
    {
        printf("the pools differ: %d particles, GPU %u\n", count, s_record[0]);
        return false;
    }
    if (e == 0 && s_arrays.sleep >= 0)
    {
        CountSleep(count);
    }
    // Places and velocities; a rigid body's orientation, angular velocity
    // and sleep, and a grid liquid's affine velocity, where it stores them.
    const struct
    {
        int16_t first;
        mvfxAttribute attribute;
        int32_t components;
    } adopted[6] = {{s_arrays.position, mvfx_attributePositionX, 3},
                    {s_arrays.velocity, mvfx_attributeVelocityX, 3},
                    {s_arrays.orientation, mvfx_attributeOrientationX, 4},
                    {s_arrays.angular, mvfx_attributeAngularX, 3},
                    {s_arrays.sleep, mvfx_attributeSleep, 4},
                    {s_arrays.affine, mvfx_attributeAffine, 9}};
    for (int32_t a = 0; a < 6; ++a)
    {
        for (int32_t k = 0; adopted[a].first >= 0 && k < adopted[a].components; ++k)
        {
            float* values = mvfxAttributeArray(cpu, e, (mvfxAttribute)(adopted[a].attribute + k));
            for (int32_t i = 0; i < count; ++i)
            {
                values[i] = FloatOf(adopted[a].first + k, i);
            }
        }
    }
    return true;
}

// Writes the CPU path's ribbon points and list from the GPU's places:
// whether they and the counts are the GPU's (the list exactly), the points
// written.
// The CPU path's strips and indices from the GPU's places: whether they
// and the counts are the GPU's (the indices exactly), the vertices
// written (those the indices name).
static bool SameStrips(int64_t* writtenOut)
{
    uint32_t* indices = s_cpuIndices;
    memset(s_cpuIndices, RECORD_FILL, sizeof s_cpuIndices);
    const mvfxExpandBuffers buffers = {.vertices = s_cpuRecords + 64,
                                       .vertexCapacity = s_recordCapacity,
                                       .indices = indices,
                                       .indexCapacity = s_ribbonCapacity,
                                       .scratch = s_sortScratch,
                                       .scratchBytes = sizeof s_sortScratch};
    int64_t vertices = 0;
    int64_t count = 0;
    if (!SwapOutputPlaces(true))
    {
        return false;
    }
    mvfxResult expanded = mvfx_success;
    if (s_plexus)
    {
        expanded = mvfxInstance_ExpandPlexus(s_context, s_cpu, s_recordEmitter, s_layout, &s_view,
                                             &buffers, &vertices, &count);
    }
    else if (s_sheets)
    {
        expanded = mvfxInstance_ExpandSheets(s_context, s_cpu, s_recordEmitter, s_layout, &s_view,
                                             &buffers, &vertices, &count);
    }
    else if (s_sprites)
    {
        int64_t quads = 0;
        expanded = mvfxInstance_ExpandSprites(s_context, s_cpu, s_recordEmitter, s_layout, &s_view,
                                              &buffers, &quads);
        vertices = 4 * quads;
        count = 6 * quads;
    }
    else
    {
        expanded = s_tube.sides > 0
                       ? mvfxInstance_ExpandTubes(s_context, s_cpu, s_recordEmitter, s_layout,
                                                  &s_view, &s_tube, &buffers, &vertices, &count)
                       : mvfxInstance_ExpandRibbons(s_context, s_cpu, s_recordEmitter, s_layout,
                                                    &s_view, &buffers, &vertices, &count);
    }
    if (!SwapOutputPlaces(false) || expanded != mvfx_success)
    {
        return false;
    }
    int64_t written = 0;
    int64_t named = 0;
    while (written < s_ribbonCapacity && indices[written] != RECORD_FILL * 0x01010101u)
    {
        named = indices[written] + 1 > named ? indices[written] + 1 : named;
        ++written;
    }
    // A sheet's last vertices may be named by no quad (its last quads
    // torn): its vertices written are the records the CPU path wrote.
    while (s_sheets && named < s_recordCapacity &&
           memcmp(s_cpuRecords + 64 + named * s_layoutView.stride, s_fillBytes,
                  (size_t)s_layoutView.stride) != 0)
    {
        ++named;
    }
    *writtenOut = named;
    bool same = s_gpuCount[0] == (uint32_t)vertices && s_gpuCount[1] == (uint32_t)count &&
                s_gpuCount[2] == (uint32_t)named && s_gpuCount[3] == (uint32_t)written &&
                memcmp(s_cpuIndices, s_gpuList + LIST_FIRST, sizeof s_cpuIndices) == 0;
    for (int32_t w = 0; same && w < LIST_FIRST; ++w)
    {
        same = s_gpuList[w] == RECORD_FILL * 0x01010101u;
    }
    if (!same)
    {
        printf("strips differ: vertices %lld (GPU %u), indices %lld (GPU %u), written %lld and "
               "%lld (GPU %u and %u)\n",
               (long long)vertices, s_gpuCount[0], (long long)count, s_gpuCount[1],
               (long long)named, (long long)written, s_gpuCount[2], s_gpuCount[3]);
    }
    return same;
}

static bool SameRibbonList(int64_t* writtenOut)
{
    if (s_strips)
    {
        return SameStrips(writtenOut);
    }
    memset(s_cpuList, RECORD_FILL, sizeof s_cpuList);
    const mvfxRibbonBuffers buffers = {.points = s_cpuRecords + 64,
                                       .pointCapacity = s_recordCapacity,
                                       .ribbons = s_cpuList,
                                       .ribbonCapacity = s_ribbonCapacity,
                                       .scratch = s_sortScratch,
                                       .scratchBytes = sizeof s_sortScratch};
    int64_t points = 0;
    int64_t ribbons = 0;
    if (!SwapOutputPlaces(true))
    {
        return false;
    }
    mvfxResult written =
        s_plexus ? mvfxInstance_WritePlexus(s_context, s_cpu, s_recordEmitter, s_layout, &s_view,
                                            &buffers, &points, &ribbons)
                 : mvfxInstance_WriteRibbons(s_context, s_cpu, s_recordEmitter, s_layout, &s_view,
                                             &buffers, &points, &ribbons);
    if (!SwapOutputPlaces(false) || written != mvfx_success)
    {
        return false;
    }
    int64_t listed = 0;
    while (listed < s_ribbonCapacity && s_cpuList[listed].count != (int32_t)0xEEEEEEEEu)
    {
        ++listed;
    }
    int64_t taken = listed > 0 ? s_cpuList[listed - 1].first + s_cpuList[listed - 1].count : 0;
    *writtenOut = taken;
    bool same = s_gpuCount[0] == (uint32_t)points && s_gpuCount[1] == (uint32_t)ribbons &&
                s_gpuCount[2] == (uint32_t)taken && s_gpuCount[3] == (uint32_t)listed &&
                memcmp(s_cpuList, s_gpuList + LIST_FIRST, sizeof s_cpuList) == 0;
    // The words before the list: the fill's.
    for (int32_t w = 0; same && w < LIST_FIRST; ++w)
    {
        same = s_gpuList[w] == RECORD_FILL * 0x01010101u;
    }
    if (!same)
    {
        printf("ribbons differ: points %lld (GPU %u), ribbons %lld (GPU %u), written %lld and "
               "%lld (GPU %u and %u)\n",
               (long long)points, s_gpuCount[0], (long long)ribbons, s_gpuCount[1],
               (long long)taken, (long long)listed, s_gpuCount[2], s_gpuCount[3]);
    }
    return same;
}

// A float32 vector field of a record, its first three components (0 for
// a field the layout lacks).
static void VectorField(const uint8_t* record, mvfxField kind, double out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = 0.0;
    }
    for (int32_t f = 0; f < s_layoutView.fieldCount; ++f)
    {
        const mvfxLayoutField* field = &s_layoutView.fields[f];
        for (uint32_t k = 0; field->field == kind && k < 3 && k < field->components; ++k)
        {
            out[k] = Decoded(field->format, record + field->offset + 4u * k);
        }
    }
}

// What a ribbon point's side and face normal may differ by past the
// tolerance: units of the tangent crossed with the direction it faces
// across (the eye's from its place, or the emitter's normal in the
// world), they are the tangent's tolerance over the sine between the two
// apart, which grows as the tangent nears that direction. From the CPU
// path's record.
static double FacingSlack(const mvfxLayoutField* field, const uint8_t* cpu)
{
    // A tube's side and face normal turn about its frame, crossed with
    // nothing faced.
    if (!s_ribbonPoints || s_tube.sides > 0 ||
        (field->field != mvfx_recordSide && field->field != mvfx_recordFaceNormal))
    {
        return 0.0;
    }
    const mvfxInstance* instance = mvfxContextInstance(s_context, s_cpu);
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[s_recordEmitter];
    double place[3];
    double tangent[3];
    VectorField(cpu, mvfx_recordPosition, place);
    VectorField(cpu, mvfx_recordTangent, tangent);
    double across[3];
    if (compiled->ribbonFacing == mvfx_ribbonFaceNormal)
    {
        mvfxRecordSource source = mvfxOpenRecordSource(instance, s_recordEmitter, s_view.origin);
        float normal[3];
        mvfxUnmapVector(&source.map, compiled->ribbonNormal, normal);
        for (int32_t k = 0; k < 3; ++k)
        {
            across[k] = (double)normal[k];
        }
    }
    else
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            across[k] = s_view.eye[k] - s_view.origin[k] - place[k];
        }
    }
    double cross[3] = {tangent[1] * across[2] - tangent[2] * across[1],
                       tangent[2] * across[0] - tangent[0] * across[2],
                       tangent[0] * across[1] - tangent[1] * across[0]};
    double sine = sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]) /
                  sqrt(across[0] * across[0] + across[1] * across[1] + across[2] * across[2]);
    return (double)s_tolerance / fmax(sine, 1e-6);
}

// Whether the GPU's lights are the CPU path's (mvfxInstance_WriteLights
// on the CPU instance): the count, each light's serial exactly and its
// values within the tolerance, the fill everywhere else.
static bool SameLights(void)
{
    static mvfxLight cpu[RECORD_BYTES / sizeof(mvfxLight)];
    int64_t capacity = s_recordCapacity;
    int64_t count = 0;
    if (capacity > (int64_t)(RECORD_BYTES / sizeof(mvfxLight)) - 4 ||
        mvfxInstance_WriteLights(s_context, s_cpu, s_recordEmitter, &s_view, cpu, capacity,
                                 &count) != mvfx_success)
    {
        return false;
    }
    bool same = (int64_t)s_gpuCount[1] == count;
    if (!same)
    {
        printf("lights: %lld, GPU %u\n", (long long)count, s_gpuCount[1]);
    }
    int64_t written = count < capacity ? count : capacity;
    for (int64_t j = 0; same && j < written; ++j)
    {
        mvfxLight gpu;
        memcpy(&gpu, s_gpuRecords + 64 + j * (int64_t)sizeof gpu, sizeof gpu);
        const mvfxLight* c = &cpu[j];
        same = gpu.serial == c->serial && Near(gpu.radius, c->radius);
        for (int32_t k = 0; same && k < 3; ++k)
        {
            same = Near(gpu.position[k], c->position[k]) && Near(gpu.color[k], c->color[k]);
        }
        if (!same)
        {
            printf("light %lld of %lld differs: serial %u (GPU %u), radius %g (GPU %g), "
                   "position %g (GPU %g), red %g (GPU %g)\n",
                   (long long)j, (long long)written, c->serial, gpu.serial, (double)c->radius,
                   (double)gpu.radius, (double)c->position[0], (double)gpu.position[0],
                   (double)c->color[0], (double)gpu.color[0]);
        }
    }
    int64_t end = 64 + written * (int64_t)sizeof(mvfxLight);
    for (int64_t b = 0; same && b < RECORD_BYTES; ++b)
    {
        same = (b >= 64 && b < end) || s_gpuRecords[b] == RECORD_FILL;
    }
    s_mostLights = count > s_mostLights ? count : s_mostLights;
    return same;
}

static bool SameRecords(void)
{
    if (s_lights)
    {
        return SameLights();
    }
    int64_t count = 0;
    int64_t scratch = 0;
    int64_t written = 0;
    memset(s_cpuRecords, RECORD_FILL, sizeof s_cpuRecords);
    if (s_ribbonPoints)
    {
        if (!SameRibbonList(&written))
        {
            return false;
        }
    }
    else if (mvfxInstance_GetSortScratchBytes(s_context, s_cpu, s_recordEmitter, &scratch) !=
                 mvfx_success ||
             scratch > (int64_t)sizeof s_sortScratch ||
             mvfxInstance_WriteParticles(s_context, s_cpu, s_recordEmitter, s_layout, &s_view,
                                         s_sortScratch, scratch, s_cpuRecords + 64,
                                         s_recordCapacity, &count) != mvfx_success)
    {
        return false;
    }
    else
    {
        written = count < s_recordCapacity ? count : s_recordCapacity;
    }
    bool same = s_ribbonPoints || (int64_t)s_gpuCount[1] == written;
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
            same = s_sorted || SameField(field, cpu, gpu, FacingSlack(field, cpu));
            if (!same)
            {
                printf("field %d (kind %u) differs: %g, GPU %g\n", f, field->field,
                       Decoded(field->format, cpu + field->offset),
                       Decoded(field->format, gpu + field->offset));
            }
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

// The terraces' pixels and normals, 64 a side.
static float s_pixels[DEPTH_SIDE * DEPTH_SIDE];
static float s_viewNormals[3 * DEPTH_SIDE * DEPTH_SIDE];

// A projection's device depth at a view depth z (negative ahead).
float DeviceDepth(const float projection[16], float z)
{
    return (projection[10] * z + projection[14]) / (projection[11] * z + projection[15]);
}

// An image of 64 by 64 pixels of a surface at view depth far, with a
// square of columns 24 to 39 and rows 16 to 31 at view depth near (off the
// middle row, so rows read upside down are seen); normals (when given)
// far's and near's, in the view's space.
mvfxDepthImage Terraces(const float projection[16], float far, float near, const float* farNormal,
                        const float* nearNormal)
{
    float d[2] = {DeviceDepth(projection, far), DeviceDepth(projection, near)};
    for (int32_t p = 0; p < DEPTH_SIDE * DEPTH_SIDE; ++p)
    {
        int32_t c = p % DEPTH_SIDE;
        int32_t r = p / DEPTH_SIDE;
        int32_t square = c >= 24 && c < 40 && r >= 16 && r < 32 ? 1 : 0;
        s_pixels[p] = d[square];
        for (int32_t k = 0; farNormal != NULL && k < 3; ++k)
        {
            s_viewNormals[3 * p + k] = square != 0 ? nearNormal[k] : farNormal[k];
        }
    }
    mvfxDepthImage image = {.pixels = s_pixels,
                            .byteCount = sizeof s_pixels,
                            .width = DEPTH_SIDE,
                            .height = DEPTH_SIDE,
                            .stride = DEPTH_SIDE * 4,
                            .format = mvfx_depthFloat32,
                            .rotation = {0, 0, 0, 1}};
    memcpy(image.projection, projection, sizeof image.projection);
    if (farNormal != NULL)
    {
        image.normals = s_viewNormals;
        image.normalByteCount = sizeof s_viewNormals;
        image.normalStride = DEPTH_SIDE * 12;
    }
    return image;
}

void SetDepthPixel(int32_t column, int32_t row, float depth)
{
    s_pixels[row * DEPTH_SIDE + column] = depth;
}

bool ReportGases(Gpu* gpu, mvfxGpuSim* sim)
{
    mvfxGpuGas gas = {0};
    bool reported = true;
    for (int32_t g = 0; reported && mvfxGpuSim_GetGas(sim, g, &gas) == mvfx_success; ++g)
    {
        uint32_t record[MVFX_GPU_GAS_STATISTICS_WORDS];
        reported = ReadRange(gpu, gas.statistics, 0, sizeof record, record, sizeof record) &&
                   mvfxGpuSim_ReportGas(sim, s_context, g, record) == mvfx_success;
    }
    return reported;
}

// Whether Simulate drops a frame of the passes first (Run.dropping).
static bool s_dropping;

void DropFrames(bool dropping)
{
    s_dropping = dropping;
}

bool Simulate(Gpu* gpu, mvfxGpuSim* sim)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    if (s_dropping &&
        (mrhiBeginFrame(gpu->device, &frame) != mrhi_success ||
         mvfxGpuSim_AddPasses(sim) != mvfx_success || mrhiDropFrame(gpu->device) != mrhi_success))
    {
        return false;
    }
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

// Sets the first emitter's host frames of both instances at step s: a
// frame more each 4 steps up to the run's (units born on one not yet set
// stay held after), frame f a little along x from the last, rising,
// turning about y, its rotation's sign flipped every other step (the same
// turn, which a unit's frame eases to the shorter way).
static void FramesFor(const Run* run, int32_t s)
{
    mvfxAttachFrame frames[8];
    int32_t count = run->attachFrames < 8 ? run->attachFrames : 8;
    count = count < 1 + s / 4 ? count : 1 + s / 4;
    for (int32_t f = 0; f < count; ++f)
    {
        float angle = 0.05f * (float)(s * (f + 1));
        float sign = (s + f) % 2 == 0 ? 1.0f : -1.0f;
        frames[f] = (mvfxAttachFrame){
            .position = {0.4 * (double)f - 0.6, 0.5 + 0.02 * (double)s, 0.1 * (double)f},
            .rotation = {0.0f, sign * sinf(angle), 0.0f, sign * cosf(angle)}};
    }
    mvfxInstanceId both[2] = {s_cpu, s_planned};
    for (int32_t i = 0; i < 2; ++i)
    {
        CHECK(mvfxInstance_SetAttachFrames(s_context, both[i], 0, frames, count) == mvfx_success,
              "frames set");
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

// Sets both instances' parameters anew at step s: each one's default
// with its x, y and z turning on a circle of radius 1.5.
static void Aim(const Run* run, int32_t s)
{
    for (int32_t p = 0; p < run->parameterCount; ++p)
    {
        const float* given = run->parameters + 4 * p;
        float angle = 0.21f * (float)s + (float)p;
        const float value[4] = {given[0] + 1.5f * cosf(angle), given[1] + 1.5f * sinf(angle),
                                given[2] + 1.5f * sinf(0.5f * angle), given[3]};
        CHECK(mvfxInstance_SetParameter(s_context, s_cpu, p, value) == mvfx_success &&
                  mvfxInstance_SetParameter(s_context, s_planned, p, value) == mvfx_success,
              "aimed");
    }
}

// Breaks both instances' ribbons by the counter, as the host does.
static void BreakRibbons(void)
{
    CHECK(mvfxInstance_BreakRibbons(s_context, s_cpu, -1) == mvfx_success &&
              mvfxInstance_BreakRibbons(s_context, s_planned, -1) == mvfx_success,
          "ribbons broken");
}

// The most ribbon breaks the GPU made at a step of the run.
static uint32_t s_ribbonBreaks;

// The queries their cap stopped, counted by the GPU, at the run's last
// step.
static uint32_t s_capped;

// Whether an emitter's queries stopped by their cap, counted by the GPU,
// are the CPU path's.
static bool SameCapped(int32_t e)
{
    mvfxEmitterStats stats;
    uint32_t capped = s_record[MVFX_GPU_NEIGHBORS_CAPPED];
    bool same = mvfxInstance_GetEmitterStats(s_context, s_cpu, e, &stats) == mvfx_success &&
                stats.neighborsCapped == capped;
    if (!same)
    {
        printf("neighbor queries capped: GPU %u, the CPU path's %llu\n", capped,
               (unsigned long long)stats.neighborsCapped);
    }
    s_capped = capped;
    return same;
}

// The particles whose contacts passed the most, counted by the GPU, at
// the run's last step.
static uint32_t s_contactsCapped;

// Whether an emitter's particles whose contacts passed the most, counted
// by the GPU, are the CPU path's.
static bool SameContactsCapped(int32_t e)
{
    mvfxEmitterStats stats;
    uint32_t capped = s_record[MVFX_GPU_CONTACTS_CAPPED];
    bool same = mvfxInstance_GetEmitterStats(s_context, s_cpu, e, &stats) == mvfx_success &&
                stats.contactsCapped == capped;
    if (!same)
    {
        printf("contacts capped: GPU %u, the CPU path's %llu\n", capped,
               (unsigned long long)stats.contactsCapped);
    }
    s_contactsCapped = capped > s_contactsCapped ? capped : s_contactsCapped;
    return same;
}

// The points whose self-collision candidates passed the most, counted by
// the GPU, at the run's last step.
static uint32_t s_selfCapped;

// Whether an emitter's points whose candidates passed the most, counted
// by the GPU, are the CPU path's.
static bool SameSelfCapped(int32_t e)
{
    mvfxEmitterStats stats;
    uint32_t capped = s_record[MVFX_GPU_SELF_CAPPED];
    bool same = mvfxInstance_GetEmitterStats(s_context, s_cpu, e, &stats) == mvfx_success &&
                stats.selfContactsCapped == capped;
    if (!same)
    {
        printf("self-contacts capped: GPU %u, the CPU path's %llu\n", capped,
               (unsigned long long)stats.selfContactsCapped);
    }
    s_selfCapped = capped > s_selfCapped ? capped : s_selfCapped;
    return same;
}

// The rigid bodies' contacts dropped past the most, counted by the GPU, at
// the run's last step.
static uint32_t s_rigidCapped;

// Whether an emitter's rigid contacts dropped past the most and its
// searches stopped at their cap, counted by the GPU, are the CPU path's
// within a hundredth of them (at least one): a contact is one within the
// margin of a surface, decided on places, so a corner at the margin may
// count on one path only, and the counts sum over the run.
static bool SameRigidCapped(int32_t e)
{
    mvfxEmitterStats stats;
    uint32_t capped = s_record[MVFX_GPU_RIGID_CAPPED];
    bool same = mvfxInstance_GetEmitterStats(s_context, s_cpu, e, &stats) == mvfx_success;
    uint64_t apart = capped > stats.rigidContactsCapped ? capped - stats.rigidContactsCapped
                                                        : stats.rigidContactsCapped - capped;
    uint64_t slack = stats.rigidContactsCapped / 100u;
    same = same && apart <= (slack > 1u ? slack : 1u);
    // The searches stopped at their cap, alike (binary32 here: one near
    // its cap may stop on one path only, within the same slack).
    uint32_t searches = s_record[MVFX_GPU_RIGID_SEARCHES];
    uint64_t searchesApart = searches > stats.rigidSearchesCapped
                                 ? searches - stats.rigidSearchesCapped
                                 : stats.rigidSearchesCapped - searches;
    uint64_t searchesSlack = stats.rigidSearchesCapped / 100u;
    same = same && searchesApart <= (searchesSlack > 1u ? searchesSlack : 1u);
    if (!same)
    {
        printf("rigid contacts capped: GPU %u, the CPU path's %llu; searches GPU %u, CPU %llu\n",
               capped, (unsigned long long)stats.rigidContactsCapped, searches,
               (unsigned long long)stats.rigidSearchesCapped);
    }
    s_rigidCapped = capped > s_rigidCapped ? capped : s_rigidCapped;
    return same;
}

// The tears the GPU counted over a run, the last emitter's that tears.
static uint32_t s_torn;

// Whether an emitter's units' tears, counted by the GPU, are the CPU
// path's.
static bool SameTorn(int32_t e)
{
    mvfxEmitterStats stats;
    uint32_t torn = s_record[MVFX_GPU_TORN];
    bool same = mvfxInstance_GetEmitterStats(s_context, s_cpu, e, &stats) == mvfx_success &&
                stats.torn == torn;
    if (!same)
    {
        printf("tears: GPU %u, the CPU path's %llu\n", torn, (unsigned long long)stats.torn);
    }
    s_torn = torn;
    return same;
}

// The links the GPU wrote at a step, read back; the most at a step and
// the drops over the run; the GPU's drops at the last step (0 after a
// clear), each emitter's.
#define MOST_LINKS 16384
static uint32_t s_linkWords[2 * MOST_LINKS];
static uint32_t s_mostLinks;
static uint64_t s_linksDropped;
static uint32_t s_lastDrops[MVFX_MAX_EMITTERS];

// Whether an emitter's links on the GPU are the CPU path's from the same
// places: the CPU path's emitter takes the GPU's places at the step's end
// (read back last; its pool the GPU's, serial by serial), finds its links
// again with the core's function, and they and the step's drops must be
// the GPU's word for word; then its places, links and drops are put
// back. Links are decided on places, which the paths hold within the
// tolerance; from the same places the distance test, a sum of squared
// differences, rounds alike.
static bool SameLinks(Gpu* gpu, const mvfxGpuSim* sim, int32_t e)
{
    mvfxInstance* cpu = mvfxContextInstance(s_context, s_cpu);
    mvfxEmitterState* state = &cpu->emitters[e];
    int32_t count = state->count;
    uint32_t written = s_record[MVFX_GPU_LINKS];
    uint32_t drops = s_record[MVFX_GPU_LINKS_DROPPED] - s_lastDrops[e];
    s_lastDrops[e] = s_record[MVFX_GPU_LINKS_DROPPED];
    mvfxGpuParticles held;
    const uint32_t* serials = mvfxAttributeArray(cpu, e, mvfx_attributeSerial);
    bool alike = (int32_t)s_record[0] == count && count <= MOST_POINTS && written <= MOST_LINKS &&
                 mvfxGpuSim_GetParticles(sim, e, &held) == mvfx_success &&
                 (written == 0 || ReadRange(gpu, held.links, 0, 8u * (uint64_t)written, s_linkWords,
                                            sizeof s_linkWords));
    for (int32_t i = 0; alike && i < count; ++i)
    {
        alike = Word(s_arrays.serial, i) == serials[i];
    }
    if (!alike)
    {
        printf("links: the pools differ or were not read (%d particles, GPU %u, %u links)\n", count,
               s_record[0], written);
        return false;
    }
    float* places[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        places[k] = mvfxAttributeArray(cpu, e, (mvfxAttribute)(mvfx_attributePositionX + k));
        for (int32_t i = 0; i < count; ++i)
        {
            s_keptPlaces[k][i] = places[k][i];
            places[k][i] = FloatOf(s_arrays.position + k, i);
        }
    }
    uint64_t kept = state->linksDropped;
    state->linksDropped = 0;
    mvfxFindLinks(cpu, e);
    bool same = state->plexus.linkCount == (int32_t)written && state->linksDropped == drops;
    for (int32_t l = 0; same && l < 2 * (int32_t)written; ++l)
    {
        same = (uint32_t)state->plexus.links[l] == s_linkWords[l];
    }
    if (!same)
    {
        printf("links: GPU %u, dropped %u; the CPU path's from the GPU's places %d, dropped "
               "%llu\n",
               written, drops, state->plexus.linkCount, (unsigned long long)state->linksDropped);
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        for (int32_t i = 0; i < count; ++i)
        {
            places[k][i] = s_keptPlaces[k][i];
        }
    }
    mvfxFindLinks(cpu, e);
    state->linksDropped = kept;
    s_mostLinks = written > s_mostLinks ? written : s_mostLinks;
    s_linksDropped += drops;
    return same;
}

// Whether an emitter's ribbon counter on the GPU (its plan's, the host's
// breaks, plus the breaks the GPU made) is the CPU path's.
static bool SameRibbonCounter(const mvfxInstance* cpu, int32_t e)
{
    mvfxEmitterPlan plan;
    uint32_t breaks = s_record[MVFX_GPU_RIBBON_BREAKS];
    const RibbonState* r = &s_ribbons[e];
    uint32_t shift = r->shifts > 0 ? r->shift[r->shifts - 1] : 0;
    bool same = LastPlan(e, &plan) == mvfx_success &&
                plan.ribbonCounter + breaks + shift == cpu->emitters[e].ribbonCounter;
    if (!same)
    {
        printf("ribbon counter %u + %u, the CPU path's %u\n", plan.ribbonCounter, breaks,
               cpu->emitters[e].ribbonCounter);
    }
    s_ribbonBreaks = breaks > s_ribbonBreaks ? breaks : s_ribbonBreaks;
    return same;
}

static void Clear(void)
{
    // Both counters start again from 0, and the drops.
    memset(s_ribbons, 0, sizeof s_ribbons);
    memset(s_lastDrops, 0, sizeof s_lastDrops);
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
static mvfxMeshId s_mesh;
static mvfxEmitterDef s_emitters[RUN_EMITTERS];
static mvfxForce s_forces[RUN_EMITTERS][MVFX_MAX_FORCES];
static mvfxCollider s_colliders[RUN_EMITTERS][MVFX_MAX_COLLIDERS];

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
    if (run->mesh != NULL)
    {
        CHECK(mvfxCreateMesh(s_context, run->mesh, &s_mesh) == mvfx_success, "the run's mesh");
    }
    for (int32_t e = 0; e < run->emitterCount; ++e)
    {
        s_emitters[e] = run->emitters[e];
        if (run->mesh != NULL && s_emitters[e].shape.kind == mvfx_shapeMesh)
        {
            s_emitters[e].shape.mesh = s_mesh;
        }
        if (run->mesh != NULL && s_emitters[e].rigid.enabled &&
            s_emitters[e].rigid.shape == mvfx_rigidHull)
        {
            s_emitters[e].rigid.hull = s_mesh;
        }
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

// Changes the run's mesh at half time: its new positions, and a second
// mesh bound to both instances' first emitter, the planned one's through
// its simulation.
static void ChangeMesh(const Run* run, mvfxGpuSim* sim)
{
    if (run->laterPositions != NULL)
    {
        CHECK(mvfxMesh_SetPositions(s_context, s_mesh, run->laterPositions, NULL,
                                    run->mesh->vertexCount) == mvfx_success,
              "the mesh moved");
    }
    if (run->laterMesh != NULL)
    {
        mvfxMeshId later = {0};
        CHECK(mvfxCreateMesh(s_context, run->laterMesh, &later) == mvfx_success &&
                  mvfxInstance_SetEmitterMesh(s_context, s_cpu, 0, later) == mvfx_success &&
                  mvfxGpuSim_SetEmitterMesh(sim, s_context, 0, later) == mvfx_success,
              "another mesh bound");
    }
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
    if (run->emitterCount < 1 || run->emitterCount > RUN_EMITTERS)
    {
        // Past the harness's tables.
        printf("FAIL: a run of %d emitters (1 to %d)\n", run->emitterCount, RUN_EMITTERS);
        exit(1);
    }
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = WithGrid(run);
    effectDef.emitterCount = run->emitterCount;
    effectDef.gases = run->gases;
    effectDef.gasCount = run->gasCount;
    // A surface's terrain takes the run's grids as a collider does.
    static mvfxWaterDef waters[MVFX_MAX_WATERS];
    for (int32_t w = 0; w < run->waterCount; ++w)
    {
        waters[w] = run->waters[w];
        WithColliderGrids(&run->waters[w].terrain, 1, &waters[w].terrain);
    }
    effectDef.waters = run->waterCount > 0 ? waters : NULL;
    effectDef.waterCount = run->waterCount;
    effectDef.parameters = run->parameters;
    effectDef.parameterCount = run->parameterCount;
    effectDef.qualityLevels = run->quality;
    effectDef.qualityLevelCount = run->quality != NULL ? 1 : 0;
    CHECK(mvfxCreateEffect(s_context, &effectDef, effectOut) == mvfx_success, "effect");
    for (int32_t e = 0; e < run->emitterCount; ++e)
    {
        mvfxEmitterView view;
        CHECK(mvfxEffect_GetEmitterView(s_context, *effectOut, e, &view) == mvfx_success, "a view");
        s_runArrays[e] = view.arrays;
        const float* most = view.sizeBounds;
        s_bodyRadius[e] =
            view.rigid.enabled
                ? 0.5f * sqrtf(most[0] * most[0] + most[1] * most[1] + most[2] * most[2])
                : 0.0f;
        s_bodiesMeet[e] = view.rigid.enabled && view.rigid.bodyContacts > 0 &&
                          (view.features & MVFX_FEATURE_COLLISION) != 0;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = *effectOut;
    instanceDef.seed = 11;
    instanceDef.quality = run->quality != NULL ? 1 : 0;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_cpu) == mvfx_success, "a CPU instance");
    instanceDef.planned = true;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_planned) == mvfx_success, "planned");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = s_context;
    simDef.effect = *effectOut;
    simDef.instance = s_planned;
    simDef.hostGridBytes = run->hostGridBytes;
    simDef.hostMeshBytes = run->hostMeshBytes;
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
    s_dropping = run->dropping;
    s_ribbonPoints = run->ribbons;
    s_strips = run->strips;
    s_plexus = run->plexus;
    s_tube = run->tube;
    s_sprites = run->sprites;
    s_sheets = run->sheets;
    s_recordEmitter = run->recordEmitter;
    s_ribbonCapacity = run->ribbonCapacity;
    s_lights = run->lights;
    s_mostLights = 0;
    s_exactNeighbors = run->exactNeighbors;
    s_neighborsCompared = 0;
    s_capped = 0;
    s_contactsCapped = 0;
    s_selfCapped = 0;
    s_rigidCapped = 0;
    s_mostAsleep = 0;
    s_woken = 0;
    memset(s_wasAsleep, 0, sizeof s_wasAsleep);
    s_torn = 0;
    s_mostLinks = 0;
    s_linksDropped = 0;
    memset(s_lastDrops, 0, sizeof s_lastDrops);
    const mvfxLayoutDef lightLayout = {.cookie = mvfxDefaultLayoutDef().cookie,
                                       .fields = &s_lightField,
                                       .fieldCount = 1,
                                       .stride = 16};
    bool records = run->layout != NULL || run->lights;
    if (records)
    {
        CHECK(OpenRecords(gpu, run->layout != NULL ? run->layout : &lightLayout,
                          run->recordCapacity, run->view),
              "the records' buffers");
    }
    s_sorted = run->emitters[0].sortMode >= mvfx_sortBackToFront && !run->tied;
    s_exactRecords = run->exact;
    s_byDepth = run->emitters[0].sortMeasure == mvfx_sortByDepth;
    s_placeError = 0.0f;
    ResetWaters();
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
    const mvfxUnitDef* unit = &run->emitters[0].unit;
    s_contactSlack = unit->kind != mvfx_unitParticle && unit->substeps > 0
                         ? s_stepTicks / unit->substeps
                         : 1 + s_stepTicks / 1000;
    s_pastAge = 0;
    s_deathsApart = 0;
    s_deathsWithin = true;
    ApartReset();
    s_childrenLeft = 0;
    s_ribbonBreaks = 0;
    memset(s_ribbons, 0, sizeof s_ribbons);
    s_breaksApart = 0;
    bool same = sim != NULL;
    int32_t most[RUN_EMITTERS] = {0};
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
        if (run->mesh != NULL && s == steps / 2)
        {
            ChangeMesh(run, sim);
        }
        if (run->spawn != NULL)
        {
            SpawnFor(run, s);
        }
        if (run->attachFrames > 0)
        {
            FramesFor(run, s);
        }
        if (run->aiming)
        {
            Aim(run, s);
        }
        if (run->breaking && s % 7 == 3)
        {
            BreakRibbons();
        }
        PushWaters(run, s);
        CHECK(mvfxContext_StepTicks(s_context, s_stepTicks) == mvfx_success, "stepped");
        s_waitNs = s_warm && s > 0 ? FRAME_WAIT_NS : WARM_WAIT_NS;
        same = Simulate(gpu, sim) && ReportGases(gpu, sim) && ReportWaters(gpu, sim);
        if (!same)
        {
            printf("step %d: the simulation was not recorded and finished\n", s);
        }
        for (int32_t e = 0; same && e < run->emitterCount; ++e)
        {
            // An emitter of no room has no buffers to read.
            mvfxGpuParticles held;
            if (mvfxGpuSim_GetParticles(sim, e, &held) == mvfx_success && held.capacity == 0)
            {
                same = mvfxContextInstance(s_context, s_cpu)->emitters[e].count == 0;
                continue;
            }
            if (!ReadBack(gpu, sim, e))
            {
                printf("step %d: emitter %d not read back\n", s, e);
                same = false;
                break;
            }
            const mvfxInstance* cpu = mvfxContextInstance(s_context, s_cpu);
            same = SameAsCpu(cpu, e) && (s_leftOut[e] || SameBounds(sim, e)) &&
                   (run->emitters[e].ribbon.ids == mvfx_ribbonNone || SameRibbonCounter(cpu, e)) &&
                   (!run->emitters[e].neighbors.enabled || SameCapped(e)) &&
                   (!run->emitters[e].contacts.enabled || SameContactsCapped(e)) &&
                   (!(run->emitters[e].unit.tearStrain > 0.0f) || SameTorn(e)) &&
                   (!(run->emitters[e].unit.selfThickness > 0.0f) || SameSelfCapped(e)) &&
                   (!run->emitters[e].rigid.enabled || !run->emitters[e].collision.enabled ||
                    SameRigidCapped(e)) &&
                   (!run->emitters[e].plexus.enabled || SameLinks(gpu, sim, e)) &&
                   (run->emitters[e].events == 0 || (ReadEvents(gpu, sim, e) && SameEvents(e))) &&
                   (!run->adopting || AdoptGpu(e));
            most[e] = (int32_t)s_record[0] > most[e] ? (int32_t)s_record[0] : most[e];
        }
        same = same && SameWaters(gpu, sim, run, s_tolerance);
        // The emitter whose records are written read back last, as the
        // ribbons take the GPU's places.
        same =
            same && (!records || ((!run->ribbons || run->recordEmitter == run->emitterCount - 1 ||
                                   ReadBack(gpu, sim, run->recordEmitter)) &&
                                  ReadRecords(gpu) && SameRecords()));
    }
    if (!same)
    {
        printf("  differs at step %d\n", s - 1);
    }
    s_dropping = false;
    printf("%s: largest relative differences, place %g, velocity %g, bounds %g\n", what,
           (double)s_placeError, (double)s_velocityError, s_boundsError);
    if (run->waterCount > 0)
    {
        // The surfaces moved: the pushes reached them.
        printf("  water: %g, the fastest a cell moved %g\n", (double)WaterError(),
               (double)WaterMoved());
        CHECK(WaterMoved() > 1e-3f, "the surfaces moved");
    }
    bool eventful = false;
    for (int32_t e = 0; e < run->emitterCount; ++e)
    {
        eventful = eventful || run->emitters[e].events != 0;
    }
    if (eventful)
    {
        printf("  events: %llu\n", (unsigned long long)s_events);
    }
    // Children of uncertain events left out: at most each such event's
    // most births, on both paths.
    for (int32_t e = 0; e < run->emitterCount; ++e)
    {
        const mvfxEventSpawn* spawn = &run->emitters[e].eventSpawn;
        if (spawn->enabled)
        {
            int32_t bound = 2 * UncertainCount() * spawn->maxCount;
            printf("  uncertain events: %d; children left out at a step: %d (at most %d)\n",
                   UncertainCount(), s_childrenLeft, bound);
            CHECK(s_childrenLeft <= bound, "children left out within the bound");
        }
    }
    CHECK(same && (!eventful || s_events > 0) && most[0] >= (run->least != 0 ? run->least : 100) &&
              (run->emitterCount < 2 || most[1] >= (run->secondLeast != 0 ? run->secondLeast : 65)),
          what);
    if (run->colliding)
    {
        printf("  contacts held: %llu; particles apart: %d (at most %d near a collider at a "
               "step; the farthest at %.3f of the reach)\n",
               (unsigned long long)s_contacts, ApartCount(), MostNear(), (double)LargestShare());
        CHECK(s_contacts > 0, "contacts");
    }
    if (run->mostApart > 0)
    {
        printf("  particles apart: %d (at most %d)\n", ApartCount(), run->mostApart);
        CHECK(ApartCount() <= run->mostApart, "particles apart");
    }
    if (run->leastBreaks > 0)
    {
        printf("  ribbon breaks the GPU made: %u (at least %d); decided apart within the "
               "tolerance: %d\n",
               s_ribbonBreaks, run->leastBreaks, s_breaksApart);
        CHECK(s_ribbonBreaks >= (uint32_t)run->leastBreaks, "ribbon breaks");
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
    if (s_neighborsCompared > 0)
    {
        printf("  neighbor attributes compared: %lld; queries capped: %u (at least %d)\n",
               (long long)s_neighborsCompared, s_capped, run->leastCapped);
        CHECK(s_capped >= (uint32_t)run->leastCapped, "queries capped");
    }
    if (run->leastTorn > 0)
    {
        printf("  torn: %u (at least %d)\n", s_torn, run->leastTorn);
        CHECK(s_torn >= (uint32_t)run->leastTorn, "torn");
    }
    if (run->emitters[0].contacts.enabled)
    {
        printf("  contacts capped: %u (at least %d)\n", s_contactsCapped, run->leastContactsCapped);
        CHECK(s_contactsCapped >= (uint32_t)run->leastContactsCapped, "contacts capped");
    }
    if (run->emitters[0].unit.selfThickness > 0.0f)
    {
        printf("  self-contacts capped: %u (at least %d)\n", s_selfCapped, run->leastSelfCapped);
        CHECK(s_selfCapped >= (uint32_t)run->leastSelfCapped, "self-contacts capped");
    }
    if (run->emitters[0].rigid.enabled && run->emitters[0].collision.enabled)
    {
        printf("  rigid contacts capped: %u (at least %d)\n", s_rigidCapped, run->leastRigidCapped);
        CHECK(s_rigidCapped >= (uint32_t)run->leastRigidCapped, "rigid contacts capped");
    }
    if (run->emitters[0].rigid.enabled && run->emitters[0].rigid.sleepSpeed > 0.0f)
    {
        printf("  bodies asleep: at most %d at a step (at least %d), %d woken (at least %d)\n",
               s_mostAsleep, run->leastAsleep, s_woken, run->leastWoken);
        CHECK(s_mostAsleep >= run->leastAsleep && s_woken >= run->leastWoken, "bodies asleep");
    }
    if (run->emitters[0].plexus.enabled)
    {
        printf("  links: at most %u at a step (at least %d), %llu dropped (at least %d)\n",
               s_mostLinks, run->leastLinks, (unsigned long long)s_linksDropped, run->leastDropped);
        CHECK(s_mostLinks >= (uint32_t)run->leastLinks &&
                  s_linksDropped >= (uint64_t)run->leastDropped,
              "links");
    }
    if (run->lights)
    {
        printf("  most lights at a step: %lld (at least %d)\n", (long long)s_mostLights,
               run->leastLights);
        CHECK(s_mostLights >= run->leastLights, "lights carried");
    }
    if (records)
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

mvfxInstanceId CpuInstance(void)
{
    return s_cpu;
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

// Scenes whose frames dispatch each kernel of the step's, the events',
// the births' from events, the records', the sort's and the ribbons', run with the
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
    // And the ribbon passes', their segments cut into pieces.
    mvfxEmitterDef ribboned = Sprite();
    ribboned.ribbon.ids = mvfx_ribbonOne;
    ribboned.ribbon.pieces = 2;
    Compare(gpu, kernels,
            &(Run){.emitters = &ribboned,
                   .emitterCount = 1,
                   .steps = 2,
                   .least = 1,
                   .layout = &layout,
                   .recordCapacity = CAPACITY,
                   .ribbons = true,
                   .ribbonCapacity = 64},
            "the ribbons' warm-up");
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
    s_warm = true;
    s_waitNs = FRAME_WAIT_NS;
    suite(&gpu, kernels);
    printf("longest frame after the warm-up: %.3f s\n", s_longestFrame);
    (void)mrhiDestroyGraphicsPipeline(gpu.device, s_fill);
    (void)mrhiDestroyShader(gpu.device, s_fillShader);
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
