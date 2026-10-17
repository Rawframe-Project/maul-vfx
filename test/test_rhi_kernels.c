// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part's kernels on a real device (lavapipe on CI): the object
// made and its pipelines built, then pcg4d of 4,096 indices dispatched in
// one frame and read back, every word the CPU's (src/random.h) bit for
// bit, and every byte the object took returned. Skips (77) without an
// adapter, unless MVFX_RHI_REQUIRED is set; the refusals of mvfxCreateGpu
// need no device.

#include "kernels.h"
#include "random.h"
#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"
#include "maul-vfx-rhi/gpu.h"

#include <stdlib.h>
#include <time.h>

// The indices hashed, a multiple of the kernel's 64 invocations.
#define WORDS   4096
#define WAIT_NS 10000000000ull

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
} Gpu;

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

int main(void)
{
    TestRefusals();
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
        return s_failures == 0 ? 77 : 1;
    }
    TestPcg4d(&gpu);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
