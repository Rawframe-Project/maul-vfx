// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part's kernels on a real device (lavapipe on CI): the object
// made and its pipelines built, then pcg4d of 4,096 indices dispatched in
// one frame and read back, every word the CPU's (src/random.h) bit for
// bit, and every byte the object took returned; the kernels' exact
// arithmetic (rhi/kernels/shared/exact.wgsl, through a test kernel) against
// the core's: 1 over every significand, drawn quotients, 64-bit counts near
// rounding ties as binary32, drawn lives' ticks as mvfxDeathTick's, and
// 64-bit remainders stored over their inputs and eight words apart (the
// first expected wrong on WARP, the second right everywhere); and the drag
// factors (rhi/kernels/shared/drag.wgsl) against the core's binary64 ones
// over seven decades of drag times seconds. Skips (77)
// without an adapter, unless MVFX_RHI_REQUIRED is set, whose driver the
// adapter must be; the refusals of mvfxCreateGpu need no device. On the
// windows-rhi-d3d12 preset it runs under D3D12's debug layer and fails on
// any error it reports.

// getenv is standard C; MSVC's runtime deprecates it for its own.
#define _CRT_SECURE_NO_WARNINGS

#include "elementary.h"
#include "emitter_step.h"
#include "exact_check_container.h"
#include "integrate.h"
#include "kernels.h"
#include "random.h"
#include "rhi_driver.h"
#include "test_harness.h"
#ifdef MVFX_RHI_D3D12_DEBUG
#include "d3d12_debug.h"
#endif

#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"
#include "maul-vfx-rhi/gpu.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// The indices hashed, a multiple of the kernel's 64 invocations.
#define WORDS   4096
#define WAIT_NS 300000000000ull

// The exact checks' elements a frame, four words each, and the bytes a
// frame uploads and reads back (the device's readback room twice that: an
// earlier frame's readback holds its room a while after it is taken).
#define CHECKS      (1 << 20)
#define CHECK_BYTES ((uint32_t)CHECKS * 16u)

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
    // Whether the adapter is WARP, whose JIT gets the in-place remainders
    // wrong.
    bool warp;
} Gpu;

// The checks' device, with room for a frame of them: OpenDevice's result.
static int Open(Gpu* gpu)
{
    *gpu = (Gpu){0};
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.frameUploadBytes = CHECK_BYTES;
    def.deviceLimits.readbackBytes = 2u * CHECK_BYTES;
    mrhiAdapterId adapter;
    int opened = OpenDevice(def, &gpu->instance, &adapter, &gpu->device);
    gpu->warp = opened == 0 && IsWarp(gpu->instance, adapter);
    return opened;
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
    // hygiene: bounds waits on the device, never a result
    (void)timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// Polls the device for the object's pipelines, as long as a frame may take
// (a cold shader cache compiles every kernel here).
static bool AwaitReady(Gpu* gpu, mvfxGpu* kernels)
{
    double deadline = Seconds() + (double)WAIT_NS * 1e-9;
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

// Records the frame: a pass dispatching pcg4d into the buffer, a pass
// reading it back.
static bool Record(Gpu* gpu, mvfxGpu* kernels, mrhiResourceId words, mrhiRequestId* readback)
{
    const mrhiAccess write = {.resource = words, .kind = mrhi_accessStorageWrite};
    mrhiPassDef hashDef = mrhiDefaultPassDef();
    hashDef.accesses = &write;
    hashDef.accessCount = 1;
    hashDef.neverCull = true;
    const mrhiAccess read = {.resource = words, .kind = mrhi_accessCopySource};
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId hashing = {0};
    mrhiPassId reading = {0};
    const mrhiBinding binding = {.slot = 0, .resource = words, .size = MRHI_WHOLE_SIZE};
    return mrhiAddPass(gpu->device, &hashDef, &hashing) == mrhi_success &&
           mrhiAddPass(gpu->device, &readDef, &reading) == mrhi_success &&
           mrhiCompileFrame(gpu->device) == mrhi_success &&
           mrhiBeginPass(gpu->device, hashing) == mrhi_success &&
           mrhiSetComputePipeline(gpu->device, hashing,
                                  mvfxGpuPipeline(kernels, mvfx_kernelPcg4d)) == mrhi_success &&
           mrhiSetBindings(gpu->device, hashing, 0, &binding, 1) == mrhi_success &&
           mrhiDispatch(gpu->device, hashing, WORDS / 64, 1, 1) == mrhi_success &&
           mrhiEndPass(gpu->device, hashing) == mrhi_success &&
           mrhiBeginPass(gpu->device, reading) == mrhi_success &&
           mrhiReadBuffer(gpu->device, reading, words, 0, WORDS * 16, readback) == mrhi_success &&
           mrhiEndPass(gpu->device, reading) == mrhi_success;
}

// Runs pcg4d on the device and reads its words.
static bool Hash(Gpu* gpu, mvfxGpu* kernels, uint32_t* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = WORDS * 16;
    def.usage = mrhi_bufferStorage | mrhi_bufferCopySource;
    mrhiBufferId buffer = {0};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId words = {0};
    mrhiRequestId readback = {0};
    mrhiRequestId token = {0};
    if (mrhiCreateBuffer(gpu->device, &def, &buffer) != mrhi_success)
    {
        return false;
    }
    bool recorded = mrhiBeginFrame(gpu->device, &frame) == mrhi_success &&
                    mrhiImportBuffer(gpu->device, buffer, &words) == mrhi_success &&
                    Record(gpu, kernels, words, &readback);
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        (void)mrhiDestroyBuffer(gpu->device, buffer);
        return false;
    }
    bool done = mrhiSubmitFrame(gpu->device, &token) == mrhi_success &&
                mrhiWaitFrame(gpu->device, token, WAIT_NS) == mrhi_success;
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
    {
    }
    size_t taken = 0;
    done = done &&
           mrhiTakeReadback(gpu->device, readback, out, WORDS * 16, &taken) == mrhi_success &&
           taken == WORDS * 16;
    (void)mrhiDestroyBuffer(gpu->device, buffer);
    return done;
}

static void TestRefusals(void)
{
    mvfxGpu* gpu = (mvfxGpu*)&gpu;
    mvfxGpuDef def = mvfxDefaultGpuDef();
    bool refused = mvfxCreateGpu(NULL, &gpu) == mvfx_errorInvalid && gpu == NULL &&
                   mvfxCreateGpu(&def, NULL) == mvfx_errorInvalid &&
                   mvfxCreateGpu(&def, &gpu) == mvfx_errorInvalid;
    def.cookie = 0;
    refused = refused && mvfxCreateGpu(&def, &gpu) == mvfx_errorInvalid;
    CHECK(refused && !mvfxGpu_IsReady(NULL) && !mvfxGpu_Notify(NULL, NULL),
          "refusals without a device");
    mvfxDestroyGpu(NULL);
}

// The bytes a counting allocator holds: the part's own leaks show here,
// as the driver's would drown them under LeakSanitizer.
static int64_t s_held;

static void* Allocate(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_held += (int64_t)size;
    return malloc(size);
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_held -= (int64_t)size;
    free(memory);
}

static void TestPcg4d(Gpu* gpu)
{
    mvfxGpuDef def = mvfxDefaultGpuDef();
    def.device = gpu->device;
    def.allocator = (mvfxAllocator){.alloc = Allocate, .free = Free};
    mvfxGpu* kernels = NULL;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success, "the object");
    CHECK(AwaitReady(gpu, kernels), "its pipelines built");
    uint32_t* words = calloc(WORDS * 4, sizeof *words);
    CHECK(words != NULL && Hash(gpu, kernels, words), "pcg4d dispatched and read back");
    int32_t same = 0;
    for (uint32_t i = 0; words != NULL && i < WORDS; ++i)
    {
        mvfxWords4 h = mvfxPcg4d((mvfxWords4){i, i * 3u, i * 7u, 0x9E3779B9u});
        same += words[4 * i] == h.x && words[4 * i + 1] == h.y && words[4 * i + 2] == h.z &&
                words[4 * i + 3] == h.w;
    }
    CHECK(same == WORDS, "every word the CPU's");
    free(words);
    CHECK(s_held > 0, "the object held through its allocator");
    mvfxDestroyGpu(kernels);
    CHECK(s_held == 0, "every byte returned");
}

// The exact checks' kernel and buffer, and what a frame of them does.
typedef enum CheckMode
{
    CHECK_RECIPROCALS = 0,
    CHECK_QUOTIENTS = 1,
    CHECK_CONVERSIONS = 2,
    CHECK_LIFE_TICKS = 3,
    CHECK_SUMS = 4,
    CHECK_REMAINDERS = 5,
    CHECK_RATIOS = 6,
    CHECK_REMAINDERS_APART = 7,
    CHECK_DRAG_FACTORS = 8,
    CHECK_HELD_SPEEDS = 9,
    CHECK_CLASSES = 10,
    CHECK_SINES = 11
} CheckMode;

// The bytes of an element of a mode: eight words for remainders apart and
// held speeds, four for the others.
static uint32_t ElementBytes(uint32_t mode)
{
    return mode == CHECK_REMAINDERS_APART || mode == CHECK_HELD_SPEEDS ? 32u : 16u;
}

typedef struct Checker
{
    mrhiShaderId shader;
    mrhiComputePipelineId pipeline;
    mrhiBufferId buffer;
    uint32_t* words;
} Checker;

static bool OpenChecker(Gpu* gpu, Checker* checker)
{
    *checker = (Checker){.words = calloc(CHECKS, 16)};
    mrhiShaderDef shader = mrhiDefaultShaderDef();
    shader.bytes = s_exactCheckContainer;
    shader.byteCount = sizeof s_exactCheckContainer;
    mrhiComputePipelineDef pipeline = mrhiDefaultComputePipelineDef();
    pipeline.entry = "check";
    pipeline.entryLength = strlen(pipeline.entry);
    mrhiBufferDef buffer = mrhiDefaultBufferDef();
    buffer.size = CHECK_BYTES;
    buffer.usage = mrhi_bufferStorage | mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiRequestId request = {0};
    if (checker->words == NULL ||
        mrhiCreateShader(gpu->device, &shader, &checker->shader) != mrhi_success ||
        mrhiCreateBuffer(gpu->device, &buffer, &checker->buffer) != mrhi_success)
    {
        return false;
    }
    pipeline.shader = checker->shader;
    if (mrhiCreateComputePipeline(gpu->device, &pipeline, &checker->pipeline, &request) !=
        mrhi_success)
    {
        return false;
    }
    double deadline = Seconds() + (double)WAIT_NS * 1e-9;
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

static void CloseChecker(Gpu* gpu, Checker* checker)
{
    if (checker->pipeline.index1 != 0)
    {
        (void)mrhiDestroyComputePipeline(gpu->device, checker->pipeline);
    }
    if (checker->shader.index1 != 0)
    {
        (void)mrhiDestroyShader(gpu->device, checker->shader);
    }
    if (checker->buffer.index1 != 0)
    {
        (void)mrhiDestroyBuffer(gpu->device, checker->buffer);
    }
    free(checker->words);
}

// Records a frame's passes: the elements uploaded, the kernel, the
// elements read back.
static bool RecordChecks(Gpu* gpu, const Checker* checker, mrhiResourceId words,
                         const uint32_t root[4], mrhiRequestId* readback)
{
    uint64_t bytes = (uint64_t)root[2] * ElementBytes(root[0]);
    const mrhiAccess put = {.resource = words, .kind = mrhi_accessCopyDestination};
    const mrhiAccess use = {.resource = words, .kind = mrhi_accessStorageReadWrite};
    const mrhiAccess take = {.resource = words, .kind = mrhi_accessCopySource};
    mrhiPassDef defs[3] = {mrhiDefaultPassDef(), mrhiDefaultPassDef(), mrhiDefaultPassDef()};
    const mrhiAccess* accesses[3] = {&put, &use, &take};
    mrhiPassId passes[3] = {0};
    bool recorded = true;
    for (int32_t p = 0; p < 3; ++p)
    {
        defs[p].passClass = p == 1 ? mrhi_passGraphics : mrhi_passTransfer;
        defs[p].accesses = accesses[p];
        defs[p].accessCount = 1;
        defs[p].neverCull = true;
        recorded = recorded && mrhiAddPass(gpu->device, &defs[p], &passes[p]) == mrhi_success;
    }
    const mrhiBinding binding = {.slot = 0, .resource = words, .size = MRHI_WHOLE_SIZE};
    return recorded && mrhiCompileFrame(gpu->device) == mrhi_success &&
           mrhiBeginPass(gpu->device, passes[0]) == mrhi_success &&
           mrhiWriteBuffer(gpu->device, passes[0], words, 0, checker->words, bytes) ==
               mrhi_success &&
           mrhiEndPass(gpu->device, passes[0]) == mrhi_success &&
           mrhiBeginPass(gpu->device, passes[1]) == mrhi_success &&
           mrhiSetComputePipeline(gpu->device, passes[1], checker->pipeline) == mrhi_success &&
           mrhiSetBindings(gpu->device, passes[1], 0, &binding, 1) == mrhi_success &&
           mrhiSetRootBlock(gpu->device, passes[1], 0, root, 4 * sizeof(uint32_t)) ==
               mrhi_success &&
           mrhiDispatch(gpu->device, passes[1], root[3], 1, 1) == mrhi_success &&
           mrhiEndPass(gpu->device, passes[1]) == mrhi_success &&
           mrhiBeginPass(gpu->device, passes[2]) == mrhi_success &&
           mrhiReadBuffer(gpu->device, passes[2], words, 0, bytes, readback) == mrhi_success &&
           mrhiEndPass(gpu->device, passes[2]) == mrhi_success;
}

// Runs a frame of checks over the checker's words, count elements, and
// reads them back in place.
static bool RunChecks(Gpu* gpu, Checker* checker, CheckMode mode, uint32_t first, uint32_t count)
{
    uint32_t groups = (count + 63u) / 64u;
    const uint32_t root[4] = {(uint32_t)mode, first, count, groups < 65535u ? groups : 65535u};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId words = {0};
    mrhiRequestId readback = {0};
    mrhiRequestId token = {0};
    bool recorded = mrhiBeginFrame(gpu->device, &frame) == mrhi_success &&
                    mrhiImportBuffer(gpu->device, checker->buffer, &words) == mrhi_success &&
                    RecordChecks(gpu, checker, words, root, &readback);
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    bool done = mrhiSubmitFrame(gpu->device, &token) == mrhi_success &&
                mrhiWaitFrame(gpu->device, token, WAIT_NS) == mrhi_success;
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
    {
    }
    size_t taken = 0;
    return done &&
           mrhiTakeReadback(gpu->device, readback, checker->words,
                            (size_t)count * ElementBytes(mode), &taken) == mrhi_success &&
           taken == (size_t)count * ElementBytes(mode);
}

static uint32_t Bits(float x)
{
    uint32_t bits = 0;
    memcpy(&bits, &x, sizeof bits);
    return bits;
}

static float FloatOf(uint32_t bits)
{
    float x = 0.0f;
    memcpy(&x, &bits, sizeof x);
    return x;
}

// A word of a simple generator, the checks' draws.
static uint32_t Draw(uint64_t* state)
{
    *state = *state * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(*state >> 32u);
}

// 1 over every significand of [1, 2), the core's division: correctly
// rounded for every significand, so for every exponent the quotient keeps
// normal.
static void TestReciprocals(Gpu* gpu, Checker* checker)
{
    int64_t wrong = 0;
    bool ran = true;
    for (uint32_t first = 0; ran && first < (1u << 23u); first += CHECKS)
    {
        ran = RunChecks(gpu, checker, CHECK_RECIPROCALS, first, CHECKS);
        for (uint32_t e = 0; ran && e < CHECKS; ++e)
        {
            wrong += checker->words[4 * e + 2] != Bits(1.0f / FloatOf(0x3F800000u | (first + e)));
        }
    }
    CHECK(ran && wrong == 0, "1 over every significand as the core divides");
}

// Drawn quotients with normal results (exponents within 2^±40), and an
// inverse life over what each of the contact's losses leaves.
static void TestQuotients(Gpu* gpu, Checker* checker)
{
    uint64_t state = 1;
    for (uint32_t e = 0; e < CHECKS; ++e)
    {
        uint32_t* w = &checker->words[4 * e];
        w[0] = ((87u + Draw(&state) % 81u) << 23u) | (Draw(&state) & 0x007FFFFFu);
        w[1] = ((87u + Draw(&state) % 81u) << 23u) | (Draw(&state) & 0x007FFFFFu);
        if (e % 4 == 0)
        {
            w[1] = Bits(1.0f - (float)(Draw(&state) >> 8u) * 0x1p-24f);
        }
    }
    bool ran = RunChecks(gpu, checker, CHECK_QUOTIENTS, 0, CHECKS);
    int64_t wrong = 0;
    for (uint32_t e = 0; ran && e < CHECKS; ++e)
    {
        const uint32_t* w = &checker->words[4 * e];
        wrong += w[2] != Bits(FloatOf(w[0]) / FloatOf(w[1]));
    }
    CHECK(ran && wrong == 0, "drawn quotients as the core divides");
}

// 64-bit counts of every length, drawn and at rounding ties (the dropped
// bits exactly half, and a unit either side), as C converts them.
static void TestConversions(Gpu* gpu, Checker* checker)
{
    uint64_t state = 2;
    for (uint32_t e = 0; e < CHECKS; ++e)
    {
        uint32_t length = 1u + e % 64u;
        uint64_t x = ((uint64_t)Draw(&state) << 32u | Draw(&state)) >> (64u - length);
        x |= 1ull << (length - 1u);
        if (length > 25u && e % 3 != 0)
        {
            uint64_t half = 1ull << (length - 25u);
            x = (x & ~(2 * half - 1)) | half;
            x += (uint64_t)(e % 3) - 1u;
        }
        checker->words[4 * e] = (uint32_t)x;
        checker->words[4 * e + 1] = (uint32_t)(x >> 32u);
    }
    bool ran = RunChecks(gpu, checker, CHECK_CONVERSIONS, 0, CHECKS);
    int64_t wrong = 0;
    for (uint32_t e = 0; ran && e < CHECKS; ++e)
    {
        const uint32_t* w = &checker->words[4 * e];
        uint64_t x = (uint64_t)w[1] << 32u | w[0];
        wrong += w[2] != Bits((float)x);
    }
    CHECK(ran && wrong == 0, "64-bit counts as C converts them");
}

// Lives from the shortest (2^-20 s) to the longest (MVFX_MAX_LIFETIME),
// drawn evenly in their logarithm and at whole ticks, their inverses as
// the core divides; inverses at powers of 2 and a unit in the last place
// either side (ticks at powers of 2, where the gap below a count's binary32
// halves, 2^24 among them); half of them with the search started up to 8
// values a count rounds to either side of its estimate, as a division
// within 2.5 units in the last place might put it. Each life's ticks as
// mvfxDeathTick finds them.
static void TestLifeTicks(Gpu* gpu, Checker* checker)
{
    uint64_t state = 3;
    const double span = log2((double)MVFX_MAX_LIFETIME) + 20.0;
    for (uint32_t e = 0; e < CHECKS; ++e)
    {
        double share = (double)Draw(&state) * 0x1p-32;
        float life = (float)exp2(share * span - 20.0);
        if (e % 2 == 0)
        {
            life = (float)(floor((double)life * (double)MVFX_TICKS_PER_SECOND) /
                           (double)MVFX_TICKS_PER_SECOND);
        }
        life = life >= 0x1p-20f ? life : 0x1p-20f;
        checker->words[4 * e] = Bits(1.0f / life);
        if (e < 3 * 20)
        {
            // 2^0 down to 2^-19, as lives of 1 s to 2^19 s, and either
            // neighbour.
            checker->words[4 * e] = ((127u - e / 3u) << 23u) + (e % 3u) - 1u;
        }
        int32_t away = e % 2 == 0 ? 0 : (int32_t)(e / 2 % 17) - 8;
        memcpy(&checker->words[4 * e + 1], &away, sizeof away);
    }
    bool ran = RunChecks(gpu, checker, CHECK_LIFE_TICKS, 0, CHECKS);
    int64_t wrong = 0;
    for (uint32_t e = 0; ran && e < CHECKS; ++e)
    {
        const uint32_t* w = &checker->words[4 * e];
        int64_t ticks = (int64_t)((uint64_t)w[3] << 32u | w[2]);
        wrong += ticks != mvfxDeathTick(0, FloatOf(w[0]));
    }
    CHECK(ran && wrong == 0, "drawn lives' ticks as mvfxDeathTick finds them");
}

// 64-bit sums and differences, drawn and with every carry and borrow at
// the low word's edge.
static void TestSums(Gpu* gpu, Checker* checker)
{
    uint64_t state = 4;
    for (uint32_t e = 0; e < CHECKS; ++e)
    {
        uint32_t* w = &checker->words[4 * e];
        w[0] = Draw(&state);
        w[1] = Draw(&state);
        w[2] = Draw(&state);
        w[3] = Draw(&state);
        if (e % 2 == 0)
        {
            w[0] = 0xFFFFFFFFu - (Draw(&state) & 7u);
            w[1] = Draw(&state) & 15u;
        }
    }
    bool ran = RunChecks(gpu, checker, CHECK_SUMS, 0, CHECKS);
    // The same draws again, against what came back.
    state = 4;
    int64_t wrong = 0;
    for (uint32_t e = 0; ran && e < CHECKS; ++e)
    {
        uint32_t in[4];
        for (int32_t k = 0; k < 4; ++k)
        {
            in[k] = Draw(&state);
        }
        if (e % 2 == 0)
        {
            in[0] = 0xFFFFFFFFu - (Draw(&state) & 7u);
            in[1] = Draw(&state) & 15u;
        }
        uint64_t a = (uint64_t)in[2] << 32u | in[0];
        uint64_t b = (uint64_t)in[3] << 32u | in[1];
        const uint32_t* w = &checker->words[4 * e];
        uint64_t sum = (uint64_t)w[2] << 32u | w[0];
        uint64_t difference = (uint64_t)w[3] << 32u | w[1];
        wrong += sum != a + b || difference != a - b;
    }
    CHECK(ran && wrong == 0, "64-bit sums and differences with their carries");
}

// Two 64-bit numbers of an element, its first and second.
static void PutPair(uint32_t* w, uint64_t first, uint64_t second)
{
    w[0] = (uint32_t)first;
    w[1] = (uint32_t)(first >> 32u);
    w[2] = (uint32_t)second;
    w[3] = (uint32_t)(second >> 32u);
}

// A drawn number of 1 to bits bits.
static uint64_t DrawBits(uint64_t* state, uint32_t bits)
{
    uint32_t length = 1u + Draw(state) % bits;
    uint64_t x = ((uint64_t)Draw(state) << 32u | Draw(state)) >> (64u - length);
    return x | 1ull << (length - 1u);
}

// 64-bit remainders over divisors of up to 48 bits, and the ratios of
// numbers below their divisors (below 2^53, as C's binary64 divides them
// exactly) as C rounds them to binary32: drawn, and at exact ties (an odd
// numerator of 25 bits over a power of 2).
static void TestRemaindersAndRatios(Gpu* gpu, Checker* checker)
{
    uint64_t state = 5;
    for (uint32_t e = 0; e < CHECKS; ++e)
    {
        PutPair(&checker->words[4 * e], DrawBits(&state, 64), DrawBits(&state, 48));
    }
    bool ran = RunChecks(gpu, checker, CHECK_REMAINDERS, 0, CHECKS);
    state = 5;
    int64_t wrong = 0;
    for (uint32_t e = 0; ran && e < CHECKS; ++e)
    {
        uint64_t x = DrawBits(&state, 64);
        uint64_t d = DrawBits(&state, 48);
        const uint32_t* w = &checker->words[4 * e];
        uint64_t got = (uint64_t)w[1] << 32u | w[0];
        if (got != x % d && wrong++ == 0)
        {
            printf("first wrong remainder: %llu %% %llu gave %llu, not %llu\n",
                   (unsigned long long)x, (unsigned long long)d, (unsigned long long)got,
                   (unsigned long long)(x % d));
        }
    }
    if (wrong != 0)
    {
        printf("%lld of %d remainders wrong\n", (long long)wrong, CHECKS);
    }
    if (gpu->warp)
    {
        // WARP stores the loop's state of this shape, x % d stored over x
        // after a constant 64 steps, over words it never writes; the
        // eight-word case below holds remainder64 itself there. Passing
        // means a WARP without the fault, where this check is to be
        // strict again.
        printf("known WARP JIT fault: misplaced stores in a constant 64-step loop\n");
        CHECK(ran && wrong != 0, "64-bit remainders pass on WARP, whose fault they expect");
    }
    else
    {
        CHECK(ran && wrong == 0, "64-bit remainders");
    }
    uint64_t* pairs = calloc(2 * (size_t)CHECKS, sizeof *pairs);
    for (uint32_t e = 0; pairs != NULL && e < CHECKS; ++e)
    {
        uint64_t d = DrawBits(&state, 53);
        uint64_t n = DrawBits(&state, 53) % d;
        if (e % 4 == 0)
        {
            uint32_t shift = 25u + Draw(&state) % 28u;
            d = 1ull << shift;
            n = ((1ull << 24u) | (Draw(&state) & 0x00FFFFFFu) | 1u)
                << (Draw(&state) % (shift - 24u));
        }
        pairs[2 * e] = n;
        pairs[2 * e + 1] = d;
        PutPair(&checker->words[4 * e], n, d);
    }
    ran = pairs != NULL && RunChecks(gpu, checker, CHECK_RATIOS, 0, CHECKS);
    wrong = 0;
    for (uint32_t e = 0; ran && e < CHECKS; ++e)
    {
        float expected = (float)((double)pairs[2 * e] / (double)pairs[2 * e + 1]);
        wrong += checker->words[4 * e] != Bits(expected);
    }
    free(pairs);
    CHECK(ran && wrong == 0, "ratios as C rounds them, ties among them");
}

// The same remainders eight words an element: x and d in, x % d out in
// words 4 and 5 (not over x), words 6 and 7 a value no step writes. Every
// device runs it, WARP among them, whose JIT gets the in-place shape
// wrong; inputs and the untouched words must come back as they went.
static void TestRemaindersApart(Gpu* gpu, Checker* checker)
{
    const uint32_t count = CHECKS / 2;
    uint64_t state = 5;
    for (uint32_t e = 0; e < count; ++e)
    {
        uint32_t* w = &checker->words[8 * e];
        uint64_t x = DrawBits(&state, 64);
        PutPair(w, x, DrawBits(&state, 48));
        w[4] = w[5] = 0;
        w[6] = w[7] = 0x5A5A5A5Au;
    }
    bool ran = RunChecks(gpu, checker, CHECK_REMAINDERS_APART, 0, count);
    state = 5;
    int64_t wrong = 0;
    for (uint32_t e = 0; ran && e < count; ++e)
    {
        uint64_t x = DrawBits(&state, 64);
        uint64_t d = DrawBits(&state, 48);
        const uint32_t* w = &checker->words[8 * e];
        wrong += ((uint64_t)w[1] << 32u | w[0]) != x || ((uint64_t)w[3] << 32u | w[2]) != d ||
                 ((uint64_t)w[5] << 32u | w[4]) != x % d || w[6] != 0x5A5A5A5Au ||
                 w[7] != 0x5A5A5A5Au;
    }
    CHECK(ran && wrong == 0, "64-bit remainders, eight words an element");
}

// The most the kernels' drag factors (shared/drag.wgsl) are from the
// core's, relative. The decay is WGSL's exp past the series, within
// 3 + 2 |x| units in the last place: at most 23 over the x checked, up to
// 2.7e-6 relative. The carry and the push take it only past x = 1, where
// neither cancels: e^-x falls faster than the bound grows, so the decay's
// share of phi1, e^-x (3 + 2x) / (1 - e^-x) units, is largest at x = 1,
// about 3 units, and phi2 carries 1.7 times that, about 5. With a few
// units of rounding, and the series' few, both stay near 1e-6.
#define DRAG_DECAY_ERROR 4e-6
#define DRAG_ERROR       2e-6

// Drag factors against the core's (mvfxDragFactorsOf, in binary64 and
// rounded once), for x, drag times seconds, from 1e-6 to 10 spaced
// evenly in its logarithm, over a second and over a 60th of one.
static void TestDragFactors(Gpu* gpu, Checker* checker)
{
    enum
    {
        POINTS = 4096
    };
    for (uint32_t e = 0; e < 2 * POINTS; ++e)
    {
        uint32_t* w = &checker->words[4 * e];
        float seconds = e < POINTS ? 1.0f : 1.0f / 60.0f;
        double x = 1e-6 * pow(10.0, 7.0 * (double)(e % POINTS) / (POINTS - 1));
        w[0] = Bits((float)(x / (double)seconds));
        w[1] = Bits(seconds);
        w[2] = 0;
        w[3] = 0;
    }
    bool ran = RunChecks(gpu, checker, CHECK_DRAG_FACTORS, 0, 2 * POINTS);
    double worst[3] = {0.0, 0.0, 0.0};
    double worstAt = 0.0;
    for (uint32_t e = 0; ran && e < 2 * POINTS; ++e)
    {
        const uint32_t* w = &checker->words[4 * e];
        // The drag in w0 was read before the decay replaced it.
        float seconds = FloatOf(w[1]);
        double x = 1e-6 * pow(10.0, 7.0 * (double)(e % POINTS) / (POINTS - 1));
        float drag = (float)(x / (double)seconds);
        mvfxDragFactors core = mvfxDragFactorsOf((double)drag, (double)seconds);
        const float expected[3] = {core.decay, core.carry, core.push};
        const float got[3] = {FloatOf(w[0]), FloatOf(w[2]), FloatOf(w[3])};
        for (int32_t k = 0; k < 3; ++k)
        {
            double error = fabs((double)got[k] - (double)expected[k]) / fabs((double)expected[k]);
            worstAt = k == 2 && error > worst[2] ? (double)drag * (double)seconds : worstAt;
            worst[k] = error > worst[k] ? error : worst[k];
        }
    }
    printf("drag factors' largest relative differences from the core's: decay %g, carry %g, "
           "push %g (at x = %g)\n",
           worst[0], worst[1], worst[2], worstAt);
    CHECK(ran && worst[0] <= DRAG_DECAY_ERROR && worst[1] <= DRAG_ERROR && worst[2] <= DRAG_ERROR,
          "drag factors as the core's");
}

// The most a held velocity (shared/speed.wgsl) is from the core's
// (mvfxHoldSpeed, in binary64), relative to its speed: a quotient, a
// square root and a product in binary32, each a few units in the last
// place.
#define HELD_ERROR 1e-6

// Held speeds against the core's. A velocity with a NaN or an infinite
// component comes back bit for bit, as the core leaves it for the kill
// rules: a NaN in each component and in all, infinities of either sign,
// a NaN beside an infinity, each beside a velocity far past the bound.
// Then a zero velocity, one at the bound, and drawn ones from far within
// to far past it, each component from -2^40 to 2^40, under bounds from
// 1e3 to 1e9.
static void TestHeldSpeeds(Gpu* gpu, Checker* checker)
{
    enum
    {
        SPECIAL = 9,
        DRAWN = 4096
    };
    const float special[SPECIAL][3] = {
        {NAN, 2e9f, 0.0f},       {3e9f, NAN, -1e9f},      {1e12f, 1e12f, NAN},
        {NAN, NAN, NAN},         {INFINITY, 1e12f, 0.0f}, {0.0f, -INFINITY, 5e9f},
        {NAN, INFINITY, -1e12f}, {0.0f, 0.0f, 0.0f},      {0.0f, 0.0f, -1e9f}};
    const uint32_t count = SPECIAL + DRAWN;
    uint64_t state = 11;
    for (uint32_t e = 0; e < count; ++e)
    {
        uint32_t* w = &checker->words[8 * e];
        for (int32_t k = 0; k < 3; ++k)
        {
            float drawn =
                ldexpf((float)Draw(&state) / 4294967296.0f - 0.5f, (int32_t)(Draw(&state) % 42u));
            w[k] = Bits(e < SPECIAL ? special[e][k] : drawn);
        }
        w[3] = Bits(e < SPECIAL ? 1e9f : ldexpf(1e3f, (int32_t)(Draw(&state) % 20u)));
        w[4] = w[5] = w[6] = 0;
        w[7] = 0x5A5A5A5Au;
    }
    bool ran = RunChecks(gpu, checker, CHECK_HELD_SPEEDS, 0, count);
    int32_t kept = 0;
    int32_t held = 0;
    double worst = 0.0;
    for (uint32_t e = 0; ran && e < count; ++e)
    {
        const uint32_t* w = &checker->words[8 * e];
        float v[3] = {FloatOf(w[0]), FloatOf(w[1]), FloatOf(w[2])};
        float* const arrays[3] = {&v[0], &v[1], &v[2]};
        mvfxHoldSpeed(arrays, 0, FloatOf(w[3]));
        bool finite = isfinite(FloatOf(w[0])) && isfinite(FloatOf(w[1])) && isfinite(FloatOf(w[2]));
        if (!finite)
        {
            kept += w[4] == w[0] && w[5] == w[1] && w[6] == w[2] && w[7] == 0x5A5A5A5Au;
            continue;
        }
        double speed = sqrt((double)v[0] * (double)v[0] + (double)v[1] * (double)v[1] +
                            (double)v[2] * (double)v[2]);
        double apart = 0.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            apart = fmax(apart, fabs((double)FloatOf(w[4 + k]) - (double)v[k]));
        }
        double error = speed > 0.0 ? apart / speed : apart;
        worst = error > worst ? error : worst;
        held += Bits(v[0]) != w[0] || Bits(v[1]) != w[1] || Bits(v[2]) != w[2];
    }
    printf("held speeds: %d not finite kept bit for bit (of 7), %d held of %d, largest relative "
           "difference from the core's %g\n",
           kept, held, (int32_t)count - 7, worst);
    CHECK(ran && kept == 7 && held > (int32_t)count / 4 && worst <= HELD_ERROR,
          "held speeds as the core's, NaN and infinities left");
}

// The kernels' float classes (shared/finite.wgsl) against C's, on the
// device: NaNs quiet and signalling, of either sign and of the largest
// payload, infinities, the largest and least floats and zeros, alone and
// in each component of a vector of ones. A test written x != x or
// x - x == 0.0 fails here on a device that folds it.
static void TestClasses(Gpu* gpu, Checker* checker)
{
    enum
    {
        VALUES = 12
    };
    const uint32_t values[VALUES] = {0x7FC00000u, 0xFFC00000u, 0x7F800001u, 0x7FFFFFFFu,
                                     0x7F800000u, 0xFF800000u, 0x7F7FFFFFu, 0xFF7FFFFFu,
                                     0x00000001u, 0x80000000u, 0x00000000u, 0x3FC00000u};
    for (uint32_t e = 0; e < VALUES; ++e)
    {
        uint32_t* w = &checker->words[4 * e];
        w[0] = values[e];
        w[1] = w[2] = w[3] = 0x5A5A5A5Au;
    }
    bool ran = RunChecks(gpu, checker, CHECK_CLASSES, 0, VALUES);
    int32_t right = 0;
    for (uint32_t e = 0; ran && e < VALUES; ++e)
    {
        const uint32_t* w = &checker->words[4 * e];
        float x = FloatOf(values[e]);
        uint32_t lane = 1u << (e % 3u);
        uint32_t vector = (isfinite(x) ? 7u : 7u & ~lane) | (isnan(x) ? lane << 3 : 0u);
        right += w[1] == (isfinite(x) ? 1u : 0u) && w[2] == (isnan(x) ? 1u : 0u) && w[3] == vector;
    }
    printf("float classes: %d of %d as C's\n", right, VALUES);
    CHECK(ran && right == VALUES, "finite and NaN tests on the bits");
}

// The most the kernels' sines and cosines (shared/sincos.wgsl) are from
// the core's mvfxSinCos, in units in the last place: the remainder within
// two of its own, from its 64-bit fraction and its product by pi/2 in
// binary32, and the series' few.
#define SINE_ULPS 10

// The distance between two floats in units in the last place, through
// their bits' order, across zero too.
static int64_t UlpsApart(float a, float b)
{
    int64_t x = (int32_t)Bits(a);
    int64_t y = (int32_t)Bits(b);
    x = x < 0 ? INT32_MIN - x : x;
    y = y < 0 ? INT32_MIN - y : y;
    return x > y ? x - y : y - x;
}

// A subnormal as a device flushing subnormals reads or writes it: a zero
// of its sign. WGSL allows that, and D3D12 does it.
static float Flushed(float x)
{
    return fabsf(x) < FLT_MIN ? copysignf(0.0f, x) : x;
}

// Sines and cosines against the core's, on the device: zeros, NaN and
// infinities (those of 0), the largest floats, the float nearest a
// multiple of pi/2 (0x1.f37c8ap+95, its remainder 2^-29.9 of a quarter
// turn), the ends of the core's two reductions at 2^24 and of this one's
// series at pi/4, and floats drawn over every exponent, of either sign.
static void TestSines(Gpu* gpu, Checker* checker)
{
    enum
    {
        SPECIAL = 16,
        DRAWN = 1 << 18
    };
    const uint32_t special[SPECIAL] = {0x00000000u, 0x80000000u, 0x7FC00000u, 0x7F800000u,
                                       0xFF800000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x6F79BE45u,
                                       0xEF79BE45u, 0x4B800000u, 0x4B7FFFFFu, 0x4B800001u,
                                       0x3F490FDAu, 0x3F490FDBu, 0x3FC90FDBu, 0x00000001u};
    const uint32_t count = SPECIAL + DRAWN;
    uint64_t state = 13;
    for (uint32_t e = 0; e < count; ++e)
    {
        uint32_t* w = &checker->words[4 * e];
        uint32_t drawn = Draw(&state);
        // An exponent from 0 to 254 and any significand and sign.
        uint32_t exponent = (Draw(&state) % 255u) << 23;
        w[0] = e < SPECIAL ? special[e] : (drawn & 0x807FFFFFu) | exponent;
        w[1] = w[2] = w[3] = 0x5A5A5A5Au;
    }
    bool ran = RunChecks(gpu, checker, CHECK_SINES, 0, count);
    int64_t worst = 0;
    float worstAt = 0.0f;
    for (uint32_t e = 0; ran && e < count; ++e)
    {
        const uint32_t* w = &checker->words[4 * e];
        float sine = 0.0f;
        float cosine = 0.0f;
        mvfxSinCos(FloatOf(w[0]), &sine, &cosine);
        float flushedSine = 0.0f;
        float flushedCosine = 0.0f;
        mvfxSinCos(Flushed(FloatOf(w[0])), &flushedSine, &flushedCosine);
        int64_t apart = UlpsApart(FloatOf(w[1]), sine);
        int64_t cosineApart = UlpsApart(FloatOf(w[2]), cosine);
        // A device flushing subnormals takes a subnormal angle as 0, and
        // writes a subnormal sine as 0.
        int64_t flushedApart = UlpsApart(Flushed(FloatOf(w[1])), Flushed(flushedSine));
        int64_t flushedCosineApart = UlpsApart(FloatOf(w[2]), flushedCosine);
        apart = flushedApart < apart ? flushedApart : apart;
        cosineApart = flushedCosineApart < cosineApart ? flushedCosineApart : cosineApart;
        apart = cosineApart > apart ? cosineApart : apart;
        worstAt = apart > worst ? FloatOf(w[0]) : worstAt;
        worst = apart > worst ? apart : worst;
    }
    printf("sines and cosines: largest difference from the core's %lld units in the last place "
           "(at %a), of %u angles\n",
           (long long)worst, (double)worstAt, count);
    CHECK(ran && worst <= SINE_ULPS, "sines and cosines as the core's");
}

static void TestExact(Gpu* gpu)
{
    Checker checker;
    bool opened = OpenChecker(gpu, &checker);
    CHECK(opened, "the exact checks' kernel built");
    if (opened)
    {
        TestReciprocals(gpu, &checker);
        TestQuotients(gpu, &checker);
        TestConversions(gpu, &checker);
        TestLifeTicks(gpu, &checker);
        TestSums(gpu, &checker);
        TestRemaindersAndRatios(gpu, &checker);
        TestRemaindersApart(gpu, &checker);
        TestDragFactors(gpu, &checker);
        TestHeldSpeeds(gpu, &checker);
        TestClasses(gpu, &checker);
        TestSines(gpu, &checker);
    }
    CloseChecker(gpu, &checker);
}

int main(void)
{
    // Unbuffered, so that a crash keeps every line printed before it:
    // ctest reads a pipe, which C's runtime otherwise fills in blocks.
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    TestRefusals();
#ifdef MVFX_RHI_D3D12_DEBUG
    mrhiTestWatchD3d12();
#endif
    Gpu gpu;
    int opened = Open(&gpu);
    if (opened != 0)
    {
        // The refusals ran without a device; a skip keeps their failure.
        Close(&gpu);
        return s_failures == 0 ? opened : 1;
    }
    TestPcg4d(&gpu);
    TestExact(&gpu);
    Close(&gpu);
#ifdef MVFX_RHI_D3D12_DEBUG
    CHECK(mrhiTestD3d12Errors() == 0, "no D3D12 debug layer error");
#endif
    return s_failures == 0 ? 0 : 1;
}
