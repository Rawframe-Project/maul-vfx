// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part's object on Maul RHI's test driver, which every build of
// the part's tests has: each refusal of mvfxCreateGpu with a device at
// hand, its memory failing, a device failing an object, the object not
// ready before its pipelines' notifications, a pipeline that fails,
// notifications that are not its own, and its shaders and pipelines all
// returned, on a device that holds no more than one object's.

#include "kernels.h"
#include "test_harness.h"

#include "maul-rhi/instance.h"
#include "maul-rhi/test.h"
#include "maul-vfx-rhi/gpu.h"

#include <stdlib.h>
#include <string.h>

static mrhiTestAdapter s_adapter;
static mrhiTestDriverDef s_driver;

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
} Gpu;

// A device on the test driver, its pipelines answered with an outcome,
// failing every object after objects (0 for none), holding the shaders
// and pipelines of one GPU object alone.
static bool Open(Gpu* gpu, mrhiResult pipelines, uint32_t objects)
{
    *gpu = (Gpu){0};
    s_adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
        .pipelineOutcome = pipelines,
        .objectsBeforeFailure = objects,
    };
    s_driver = (mrhiTestDriverDef){
        .chain = {.next = NULL, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &s_driver.chain;
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter;
    size_t count = 0;
    if (mrhiCreateInstance(&def, &gpu->instance) != mrhi_success ||
        mrhiRequestAdapters(gpu->instance, &search, &request) != mrhi_success ||
        mrhiNextInstanceNotification(gpu->instance, &record) != mrhi_success ||
        mrhiGetAdapters(gpu->instance, &adapter, 1, &count) != mrhi_success || count != 1)
    {
        return false;
    }
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    deviceDef.deviceLimits.shaders = MVFX_CONTAINER_COUNT;
    deviceDef.deviceLimits.pipelines = MVFX_KERNEL_COUNT;
    return mrhiCreateDevice(gpu->instance, &deviceDef, &gpu->device, &request) == mrhi_success &&
           mrhiNextInstanceNotification(gpu->instance, &record) == mrhi_success &&
           record.kind == mrhi_instanceDeviceReady && record.outcome == mrhi_success;
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

// The object's memory: the bytes held counted, the calls counted,
// allocation number s_failAt failing (from 1, 0 for none), every block
// handed out filled with a pattern, so a field never written reads as
// garbage. Blocks are aligned by hand, as not every C library has
// aligned_alloc.
static int64_t s_held;
static int32_t s_calls;
static int32_t s_failAt;

static void* Allocate(size_t size, size_t alignment, void* context)
{
    (void)context;
    if (++s_calls == s_failAt)
    {
        return NULL;
    }
    uint8_t* raw = malloc(size + alignment + sizeof(void*));
    if (raw == NULL)
    {
        return NULL;
    }
    uintptr_t at = ((uintptr_t)(raw + sizeof(void*)) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void**)at)[-1] = raw;
    memset((void*)at, 0xA5, size);
    s_held += (int64_t)size;
    return (void*)at;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_held -= (int64_t)size;
    free(((void**)memory)[-1]);
}

static mvfxGpuDef CountedDef(mrhiDevice* device)
{
    mvfxGpuDef def = mvfxDefaultGpuDef();
    def.device = device;
    def.allocator = (mvfxAllocator){.alloc = Allocate, .free = Free};
    return def;
}

// Hands the object every notification the device has; how many were its
// own. The last of its own is kept in mine.
static int32_t Pump(Gpu* gpu, mvfxGpu* kernels, mrhiDeviceNotification* mine)
{
    int32_t its = 0;
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
    {
        if (mvfxGpu_Notify(kernels, &record))
        {
            *mine = record;
            ++its;
        }
    }
    return its;
}

static void TestRefusals(Gpu* gpu)
{
    s_calls = 0;
    mvfxGpu* kernels = (mvfxGpu*)&kernels;
    mvfxGpuDef def = CountedDef(gpu->device);
    bool refused = mvfxCreateGpu(NULL, &kernels) == mvfx_errorInvalid && kernels == NULL &&
                   mvfxCreateGpu(&def, NULL) == mvfx_errorInvalid;
    def.cookie = 0;
    refused = refused && mvfxCreateGpu(&def, &kernels) == mvfx_errorInvalid;
    def = CountedDef(NULL);
    refused = refused && mvfxCreateGpu(&def, &kernels) == mvfx_errorInvalid;
    def = CountedDef(gpu->device);
    def.allocator.free = NULL;
    refused = refused && mvfxCreateGpu(&def, &kernels) == mvfx_errorInvalid;
    def = CountedDef(gpu->device);
    def.allocator.alloc = NULL;
    refused = refused && mvfxCreateGpu(&def, &kernels) == mvfx_errorInvalid;
    CHECK(refused && kernels == NULL && s_calls == 0, "each refusal, nothing allocated");
    CHECK(!mvfxGpu_IsReady(NULL) && !mvfxGpu_Notify(NULL, NULL), "NULL objects");
    mvfxDestroyGpu(NULL);

    s_failAt = 1;
    def = CountedDef(gpu->device);
    kernels = (mvfxGpu*)&kernels;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_errorCapacity && kernels == NULL && s_held == 0,
          "memory refused");
    s_failAt = 0;
}

// Notifications that are not the object's own: another kind, another
// request's generation or index, a failed build.
static void TestNotifications(mvfxGpu* kernels, const mrhiDeviceNotification* mine)
{
    CHECK(!mvfxGpu_Notify(kernels, NULL), "no notification");
    mrhiDeviceNotification other = *mine;
    other.kind = mrhi_devicePipelineReady + 1;
    bool ignored = !mvfxGpu_Notify(kernels, &other);
    other = *mine;
    other.request.generation += 1;
    ignored = ignored && !mvfxGpu_Notify(kernels, &other);
    other = *mine;
    other.request.index1 = 0xFFFFFFF0u;
    ignored = ignored && !mvfxGpu_Notify(kernels, &other);
    CHECK(ignored && mvfxGpu_IsReady(kernels), "others' notifications ignored");
    other = *mine;
    other.outcome = mrhi_errorPlatform;
    CHECK(mvfxGpu_Notify(kernels, &other) && !mvfxGpu_IsReady(kernels), "a failed build");
    CHECK(mvfxGpu_Notify(kernels, mine) && mvfxGpu_IsReady(kernels), "built again");
}

static void TestLife(void)
{
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_success, 0), "a device");
    TestRefusals(&gpu);
    // Three objects in turn on a device that holds one's shaders and
    // pipelines: each returns them.
    for (int32_t round = 0; round < 3; ++round)
    {
        mvfxGpuDef def = CountedDef(gpu.device);
        mvfxGpu* kernels = NULL;
        CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success, "created");
        CHECK(!mvfxGpu_IsReady(kernels), "not ready before its notifications");
        mrhiDeviceNotification mine = {0};
        CHECK(Pump(&gpu, kernels, &mine) == MVFX_KERNEL_COUNT && mvfxGpu_IsReady(kernels),
              "ready on its own notifications");
        if (round == 0)
        {
            TestNotifications(kernels, &mine);
        }
        mvfxDestroyGpu(kernels);
        CHECK(s_held == 0, "its memory returned");
    }
    Close(&gpu);

    // The C library's memory, by a zeroed allocator.
    CHECK(Open(&gpu, mrhi_success, 0), "a device");
    mvfxGpuDef def = mvfxDefaultGpuDef();
    def.device = gpu.device;
    mvfxGpu* kernels = NULL;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success, "on the C library's memory");
    mvfxDestroyGpu(kernels);
    Close(&gpu);
}

// The root def's checks, in order: the cookie first, then the ABI
// version, then the rest, so that a def of another layout is refused
// before any field after the first two is read.
static void TestDefChecks(void)
{
    mvfxGpu* kernels = (mvfxGpu*)&kernels;
    mvfxGpuDef zeroed = {0};
    zeroed.version = MVFX_ABI_VERSION + 1;
    CHECK(mvfxCreateGpu(&zeroed, &kernels) == mvfx_errorInvalid && kernels == NULL,
          "a def without its cookie refused before its version is read");
    mvfxGpuDef other = mvfxDefaultGpuDef();
    other.version = MVFX_ABI_VERSION + 1;
    CHECK(mvfxCreateGpu(&other, &kernels) == mvfx_errorVersion,
          "a def of other headers refused before its missing device is read");
    other.version = 0;
    CHECK(mvfxCreateGpu(&other, &kernels) == mvfx_errorVersion, "a zeroed version refused");
    mvfxGpuDef stamped = mvfxDefaultGpuDef();
    CHECK(stamped.version == MVFX_ABI_VERSION &&
              mvfxCreateGpu(&stamped, &kernels) == mvfx_errorInvalid,
          "the default stamps these headers, and still needs a device");
}

static void TestFailures(void)
{
    // A pipeline the device fails to build: answered, never ready.
    Gpu gpu;
    CHECK(Open(&gpu, mrhi_errorPlatform, 0), "a device failing pipelines");
    mvfxGpuDef def = CountedDef(gpu.device);
    mvfxGpu* kernels = NULL;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success, "created");
    mrhiDeviceNotification mine = {0};
    CHECK(Pump(&gpu, kernels, &mine) == MVFX_KERNEL_COUNT && !mvfxGpu_IsReady(kernels),
          "a failed pipeline answered, not ready");
    mvfxDestroyGpu(kernels);
    Close(&gpu);

    // A device failing its objects after a few: refused with the state
    // error and all returned, until the device makes the shaders and the
    // first requests; made then, but ready only once it makes every
    // kernel's pipeline (the later ones requested as the first are
    // answered).
    int32_t refusedCount = 0;
    int32_t madeCount = 0;
    bool readyAsMade = true;
    for (uint32_t objects = 1; objects <= MVFX_CONTAINER_COUNT + MVFX_KERNEL_COUNT + 2; ++objects)
    {
        CHECK(Open(&gpu, mrhi_success, objects), "a device failing objects");
        def = CountedDef(gpu.device);
        kernels = (mvfxGpu*)&kernels;
        mvfxResult result = mvfxCreateGpu(&def, &kernels);
        refusedCount += result == mvfx_errorState && kernels == NULL && s_held == 0;
        madeCount += result == mvfx_success;
        if (result == mvfx_success)
        {
            mrhiDeviceNotification last = {0};
            (void)Pump(&gpu, kernels, &last);
            bool every = objects >= MVFX_CONTAINER_COUNT + MVFX_KERNEL_COUNT;
            readyAsMade = readyAsMade && mvfxGpu_IsReady(kernels) == every;
        }
        mvfxDestroyGpu(result == mvfx_success ? kernels : NULL);
        CHECK(s_held == 0, "its memory returned");
        Close(&gpu);
    }
    CHECK(refusedCount > 0 && madeCount > 0 &&
              refusedCount + madeCount == MVFX_CONTAINER_COUNT + MVFX_KERNEL_COUNT + 2,
          "refused while the device fails, made once it does not");
    CHECK(readyAsMade && refusedCount < MVFX_CONTAINER_COUNT + MVFX_KERNEL_COUNT,
          "made with its first requests, ready once every pipeline is");
}

int main(void)
{
    TestDefChecks();
    TestLife();
    TestFailures();
    return s_failures == 0 ? 0 : 1;
}
