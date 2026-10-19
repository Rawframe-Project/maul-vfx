// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Ribbons' points on a real device (lavapipe on CI) beside the CPU path's
// mvfxInstance_WriteRibbons, compared after every step (rhi_harness.h):
// the list and the counts exactly, each point's fields within the
// tolerance; facing the eye and a normal, along the length, by segment and
// by distance with a scroll, by the counter, by batch and by source, the
// buffers too small for all; then a ribbons def refused. Skips (77)
// without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/resources.h"

#include <math.h>

// A point of every ribbon field, its colour, corner and serial.
static const mvfxLayoutField s_fields[12] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
    {.field = mvfx_recordTangent, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
    {.field = mvfx_recordWidth, .format = mvfx_formatFloat32, .components = 1, .offset = 24},
    {.field = mvfx_recordLength, .format = mvfx_formatFloat32, .components = 1, .offset = 28},
    {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 1, .offset = 32},
    {.field = mvfx_recordRibbon, .format = mvfx_formatUint32, .components = 1, .offset = 36},
    {.field = mvfx_recordSide, .format = mvfx_formatFloat32, .components = 3, .offset = 40},
    {.field = mvfx_recordFaceNormal, .format = mvfx_formatFloat32, .components = 3, .offset = 52},
    {.field = mvfx_recordColor, .format = mvfx_formatUnorm8, .components = 4, .offset = 64},
    {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 68},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 76},
    {.field = mvfx_recordSize, .format = mvfx_formatFloat16, .components = 2, .offset = 80}};

static mvfxLayoutDef PointLayout(void)
{
    mvfxLayoutDef layout = mvfxDefaultLayoutDef();
    layout.fields = s_fields;
    layout.fieldCount = 12;
    layout.stride = 84;
    return layout;
}

// The width over the length and the colour over it.
static const mvfxCurveKey s_narrowing[2] = {{.time = 0, .value = 1}, {.time = 1, .value = 0.25f}};
static const mvfxColorKey s_colors[2] = {{.time = 0, .red = 1, .green = 0.5f, .blue = 0.25f},
                                         {.time = 1, .red = 0.25f, .green = 0.5f, .blue = 1}};
static const mvfxAlphaKey s_alphas[2] = {{.time = 0, .alpha = 0.5f}, {.time = 1, .alpha = 1}};

// A sprite drawn as ribbons by ids, its width narrowing and its colour
// changing along them, a stream of births moving out.
static mvfxEmitterDef Ribboned(mvfxRibbonIds ids)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.rate = 240.0f;
    emitter.lifetime = mvfxRangeSource(0.4f, 0.6f);
    emitter.size =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0.1f, 0.2f}, .max = {0.3f, 0.4f}};
    emitter.ribbon.ids = ids;
    emitter.ribbon.width =
        (mvfxLifeCurve){.value = 2, .curve = {.keys = s_narrowing, .keyCount = 2}};
    emitter.ribbon.color = (mvfxGradient){
        .colorKeys = s_colors, .colorKeyCount = 2, .alphaKeys = s_alphas, .alphaKeyCount = 2};
    return emitter;
}

static void TestCounter(Gpu* gpu, mvfxGpu* kernels)
{
    // One ribbon facing the eye, its coordinate along its length, under a
    // moving instance; with the host's breaks every seventh step, a ribbon
    // each.
    const mvfxLayoutDef layout = PointLayout();
    mvfxEmitterDef emitter = Ribboned(mvfx_ribbonOne);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .layout = &layout,
                   .recordCapacity = CAPACITY,
                   .ribbons = true,
                   .ribbonCapacity = 64},
            "points of one ribbon facing the eye");
    emitter.ribbon.texture = mvfx_ribbonTilePerSegment;
    emitter.ribbon.facing = mvfx_ribbonFaceNormal;
    emitter.ribbon.normal[0] = 0.3f;
    emitter.ribbon.normal[1] = 1.0f;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .breaking = true,
                   .layout = &layout,
                   .recordCapacity = CAPACITY,
                   .ribbons = true,
                   .ribbonCapacity = 64},
            "points by segment facing a normal, broken by the host");
}

static void TestDistance(Gpu* gpu, mvfxGpu* kernels)
{
    // Tiled by distance with a scroll, in the world's space; then in the
    // instance's, facing a normal its turn carries.
    const mvfxLayoutDef layout = PointLayout();
    mvfxEmitterDef emitter = Ribboned(mvfx_ribbonOne);
    emitter.ribbon.texture = mvfx_ribbonTileByDistance;
    emitter.ribbon.tileLength = 0.3f;
    emitter.ribbon.scroll = -1.75f;
    emitter.space = mvfx_spaceWorld;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .layout = &layout,
                   .recordCapacity = CAPACITY,
                   .ribbons = true,
                   .ribbonCapacity = 64},
            "points tiled by distance, scrolled, in the world's space");
    emitter.space = mvfx_spaceLocal;
    emitter.ribbon.facing = mvfx_ribbonFaceNormal;
    emitter.ribbon.normal[2] = 1.0f;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .layout = &layout,
                   .recordCapacity = CAPACITY,
                   .ribbons = true,
                   .ribbonCapacity = 64},
            "points tiled by distance in the instance's space, facing its normal");
}

static void TestBatches(Gpu* gpu, mvfxGpu* kernels)
{
    // A ribbon a tick's births, many at once: all of them; then the points
    // too few for all, and the list too short, so the first that fit are
    // written and the counts still count every one.
    const mvfxLayoutDef layout = PointLayout();
    static const mvfxBurst bursts[1] = {{.time = 0,
                                         .minCount = 4,
                                         .maxCount = 8,
                                         .cycles = 0,
                                         .interval = 0.02f,
                                         .probability = 1}};
    mvfxEmitterDef emitter = Ribboned(mvfx_ribbonPerBatch);
    emitter.capacity = 4096;
    emitter.bursts = bursts;
    emitter.burstCount = 1;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .least = 200,
                   .layout = &layout,
                   .recordCapacity = 4096,
                   .ribbons = true,
                   .ribbonCapacity = 4096},
            "points of ribbons by batch");
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .least = 200,
                   .layout = &layout,
                   .recordCapacity = 60,
                   .ribbons = true,
                   .ribbonCapacity = 4096},
            "points of ribbons by batch, the points too few");
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .least = 200,
                   .layout = &layout,
                   .recordCapacity = 4096,
                   .ribbons = true,
                   .ribbonCapacity = 9},
            "points of ribbons by batch, the list too short");
}

static void TestSources(Gpu* gpu, mvfxGpu* kernels)
{
    // Births from another emitter's deaths in their event's particle's
    // ribbon, beside births of their own by the counter, which tags them
    // apart.
    const mvfxLayoutDef layout = PointLayout();
    mvfxEmitterDef emitters[2] = {Sprite(), Ribboned(mvfx_ribbonPerSource)};
    emitters[0].events = mvfx_eventMaskDeath;
    emitters[0].eventCapacity = 4096;
    emitters[1].rate = 120.0f;
    emitters[1].burstCount = 0;
    emitters[1].lifetime = mvfxRangeSource(0.1f, 0.3f);
    emitters[1].velocity.speed = (mvfxRange){0.5f, 1.0f};
    emitters[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                              .source = 0,
                                              .kind = mvfx_eventDeath,
                                              .minCount = 1,
                                              .maxCount = 3,
                                              .inheritVelocity = 0.5f,
                                              .capacity = 512};
    Compare(gpu, kernels,
            &(Run){.emitters = emitters,
                   .emitterCount = 2,
                   .moving = true,
                   .least = 20,
                   .layout = &layout,
                   .recordCapacity = CAPACITY,
                   .ribbons = true,
                   .ribbonCapacity = 1024,
                   .recordEmitter = 1},
            "points of ribbons by source");
}

static void TestNone(Gpu* gpu, mvfxGpu* kernels)
{
    // An emitter not drawn as ribbons: counts of 0, no point.
    const mvfxLayoutDef layout = PointLayout();
    mvfxEmitterDef emitter = Sprite();
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .layout = &layout,
                   .recordCapacity = CAPACITY,
                   .ribbons = true,
                   .ribbonCapacity = 64},
            "no ribbons");
}

// The layouts the defs refused take, made in each run's context: one
// valid, one gone, one of an uneven stride, one with a neighbor field.
static mvfxLayoutId s_layouts[4];

static void MakeLayouts(void)
{
    static const mvfxLayoutField word = {
        .field = mvfx_recordRibbon, .format = mvfx_formatUint32, .components = 1};
    static const mvfxLayoutField bytes = {
        .field = mvfx_recordColor, .format = mvfx_formatUnorm8, .components = 2};
    static const mvfxLayoutField neighbors = {
        .field = mvfx_recordNeighborCount, .format = mvfx_formatUint32, .components = 1};
    mvfxLayoutDef def = mvfxDefaultLayoutDef();
    def.fieldCount = 1;
    def.stride = 4;
    def.fields = &word;
    CHECK(mvfxCreateLayout(RunContext(), &def, &s_layouts[0]) == mvfx_success &&
              mvfxCreateLayout(RunContext(), &def, &s_layouts[1]) == mvfx_success &&
              mvfxDestroyLayout(RunContext(), s_layouts[1]) == mvfx_success,
          "layouts");
    def.fields = &bytes;
    def.stride = 2;
    CHECK(mvfxCreateLayout(RunContext(), &def, &s_layouts[2]) == mvfx_success, "uneven");
    def.fields = &neighbors;
    def.stride = 4;
    CHECK(mvfxCreateLayout(RunContext(), &def, &s_layouts[3]) == mvfx_success, "neighbors");
}

// Ribbons def i of a buffer: the valid def for 0; then no cookie, an
// emitter out of range, a null buffer, an offset not a multiple of 4, a
// negative capacity, a view out of range, a layout gone, uneven or with a
// neighbor field.
#define BAD_DEFS 15
static mvfxGpuRibbonsDef RibbonsDef(mrhiResourceId buffer, int32_t i)
{
    mvfxGpuRibbonsDef def = mvfxDefaultGpuRibbonsDef();
    def.layout = s_layouts[0];
    def.points = buffer;
    def.pointCapacity = 8;
    def.ribbons = buffer;
    def.ribbonOffset = 512;
    def.ribbonCapacity = 8;
    switch (i)
    {
    case 0:
        break;
    case 1:
        def.cookie = 0;
        break;
    case 2:
        def.emitter = 1;
        break;
    case 3:
        def.emitter = -1;
        break;
    case 4:
        def.points = (mrhiResourceId){0};
        break;
    case 5:
        def.ribbons = (mrhiResourceId){0};
        break;
    case 6:
        def.pointOffset = 2;
        break;
    case 7:
        def.ribbonOffset = 6;
        break;
    case 8:
        def.countsOffset = 3;
        break;
    case 9:
        def.pointCapacity = -1;
        break;
    case 10:
        def.ribbonCapacity = -1;
        break;
    case 11:
        def.view.origin[0] = 0x1p53;
        break;
    case 12:
        def.view.up[2] = NAN;
        break;
    case 13:
        def.layout = s_layouts[1];
        break;
    case 14:
        def.layout = s_layouts[2];
        break;
    default:
        def.layout = s_layouts[3];
        break;
    }
    return def;
}

// Opens a run of one emitter and counts the ribbons defs from..to-1
// refused (as stale for the layout gone, else as invalid) in a frame.
static int32_t Refused(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, int32_t from,
                       int32_t to)
{
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(kernels, &(Run){.emitters = emitter, .emitterCount = 1}, &effect);
    MakeLayouts();
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 1024;
    bufferDef.usage = mrhi_bufferStorage;
    mrhiBufferId buffer = {0};
    mrhiResourceId resource = {0};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(sim != NULL && mrhiCreateBuffer(gpu->device, &bufferDef, &buffer) == mrhi_success &&
              mrhiBeginFrame(gpu->device, &frame) == mrhi_success &&
              mrhiImportBuffer(gpu->device, buffer, &resource) == mrhi_success,
          "a buffer in a frame");
    int32_t refused = 0;
    for (int32_t i = from; i < to; ++i)
    {
        const mvfxGpuRibbonsDef def = RibbonsDef(resource, i);
        mvfxResult result = mvfxGpuSim_AddRibbons(sim, &def);
        refused += result == (i == 13 ? mvfx_errorStale : mvfx_errorInvalid) ? 1 : 0;
    }
    const mvfxGpuRibbonsDef valid = RibbonsDef(resource, 0);
    CHECK(mvfxGpuSim_AddRibbons(NULL, &valid) == mvfx_errorInvalid &&
              mvfxGpuSim_AddRibbons(sim, NULL) == mvfx_errorInvalid,
          "NULL refused");
    CHECK(mrhiDropFrame(gpu->device) == mrhi_success, "dropped");
    (void)mrhiDestroyBuffer(gpu->device, buffer);
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
    return refused;
}

static void TestRefused(Gpu* gpu, mvfxGpu* kernels)
{
    // Each malformed def refused; then the valid def for ribbons of more
    // pieces a segment, or with a pinned head, which this part does not
    // write yet; then taken.
    mvfxEmitterDef emitter = Ribboned(mvfx_ribbonOne);
    CHECK(Refused(gpu, kernels, &emitter, 1, BAD_DEFS + 1) == BAD_DEFS, "ribbons defs refused");
    emitter.ribbon.pieces = 4;
    CHECK(Refused(gpu, kernels, &emitter, 0, 1) == 1, "ribbons of pieces refused");
    emitter.ribbon.pieces = 1;
    emitter.ribbon.pinHead = true;
    CHECK(Refused(gpu, kernels, &emitter, 0, 1) == 1, "a pinned head refused");
    emitter.ribbon.pinHead = false;
    CHECK(Refused(gpu, kernels, &emitter, 0, 1) == 0, "the valid def taken");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestCounter(gpu, kernels);
    TestDistance(gpu, kernels);
    TestBatches(gpu, kernels);
    TestSources(gpu, kernels);
    TestNone(gpu, kernels);
    TestRefused(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
