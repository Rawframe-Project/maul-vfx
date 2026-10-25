// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Output: layouts checked when made (every rule), held to the context's
// limit and destroyed; each format's conversion by hand (binary16,
// normalized with clamps and rounding, sRGB and a linear alpha,
// words); records of a world emitter relative to a view's origin with
// the host's padding untouched, a buffer too small and a count alone; a
// local emitter's records in the world under a moved, turned, scaled
// instance, its custom attribute among them; records written in runs,
// each its own particle's values over life; and the refusals.

#include "test_harness.h"

#include "maul-vfx/context.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

static mvfxContext* NewContext(int32_t layouts)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    def.limits.layouts = layouts;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    return context;
}

static mvfxResult Make(mvfxContext* context, const mvfxLayoutField* fields, int32_t count,
                       uint32_t stride, mvfxLayoutId* id)
{
    mvfxLayoutDef def = mvfxDefaultLayoutDef();
    def.fields = fields;
    def.fieldCount = count;
    def.stride = stride;
    return mvfxCreateLayout(context, &def, id);
}

static void TestLayouts(void)
{
    mvfxContext* context = NewContext(2);
    static const mvfxLayoutField good = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutId id = {0};
    CHECK(Make(context, &good, 1, 12, &id) == mvfx_success && mvfxLayout_IsValid(context, id),
          "a layout");
    uint64_t misuse = mvfxGetContextMisuse(context);
    int32_t refused = 0;
    for (int32_t rule = 0; rule < 20; ++rule)
    {
        mvfxLayoutField f[2] = {good,
                                {.field = mvfx_recordSerial,
                                 .format = mvfx_formatUint32,
                                 .components = 1,
                                 .offset = 12}};
        int32_t count = 1;
        uint32_t stride = 16;
        switch (rule)
        {
        case 0:
            f[0].field = 19;
            break;
        case 1:
            f[0].format = 7;
            break;
        case 2:
            f[0].components = 0;
            break;
        case 3:
            f[0].components = 4;
            break;
        case 4:
            f[0] = (mvfxLayoutField){.field = mvfx_recordOrientation, .components = 3};
            break;
        case 5:
            f[0] = (mvfxLayoutField){.field = mvfx_recordAngle, .components = 2};
            break;
        case 6:
            f[0] = (mvfxLayoutField){.field = mvfx_recordCustom, .components = 5};
            break;
        case 7:
            f[0] = (mvfxLayoutField){.field = mvfx_recordCustom, .components = 1, .custom = 8};
            break;
        case 8:
            f[0].srgb = true;
            f[0].format = mvfx_formatUnorm8;
            break;
        case 9:
            f[0] = (mvfxLayoutField){.field = mvfx_recordColor,
                                     .format = mvfx_formatUnorm16,
                                     .components = 4,
                                     .srgb = true};
            break;
        case 10:
            f[0].offset = 2;
            break;
        case 11:
            stride = 11;
            break;
        case 12:
            count = 2;
            f[1].offset = 8;
            break;
        case 13:
            f[0] = (mvfxLayoutField){
                .field = mvfx_recordFrame, .format = mvfx_formatUnorm8, .components = 1};
            break;
        case 14:
            count = 2;
            f[1].format = mvfx_formatFloat32;
            break;
        case 15:
            f[0].format = mvfx_formatUint32;
            break;
        case 16:
            count = 0;
            break;
        case 17:
            count = MVFX_MAX_LAYOUT_FIELDS + 1;
            break;
        case 18:
            stride = 0;
            break;
        default:
            stride = MVFX_MAX_LAYOUT_STRIDE + 1;
            break;
        }
        mvfxLayoutField many[MVFX_MAX_LAYOUT_FIELDS + 1];
        for (int32_t i = 0; i < MVFX_MAX_LAYOUT_FIELDS + 1; ++i)
        {
            many[i] = f[i < 2 ? i : 1];
        }
        refused += Make(context, rule == 17 ? many : f, count, stride, &id) == mvfx_errorInvalid &&
                           id.index1 == 0
                       ? 1
                       : 0;
    }
    CHECK(refused == 20, "every rule refused");
    mvfxLayoutDef noCookie = {.fields = &good, .fieldCount = 1, .stride = 12};
    CHECK(mvfxCreateLayout(context, &noCookie, &id) == mvfx_errorInvalid &&
              mvfxCreateLayout(context, NULL, &id) == mvfx_errorInvalid &&
              mvfxCreateLayout(context, &noCookie, NULL) == mvfx_errorInvalid &&
              mvfxCreateLayout(NULL, &noCookie, &id) == mvfx_errorInvalid,
          "no cookie, no def, no id, no context");
    CHECK(mvfxGetContextMisuse(context) == misuse + 23, "each counted");
    // Two at most here; destroyed, its id is stale and its slot free.
    mvfxLayoutId second = {0};
    mvfxLayoutId third = {0};
    CHECK(Make(context, &good, 1, 12, &second) == mvfx_success &&
              Make(context, &good, 1, 12, &third) == mvfx_errorLimit,
          "the limit");
    CHECK(mvfxDestroyLayout(context, second) == mvfx_success &&
              !mvfxLayout_IsValid(context, second) &&
              mvfxDestroyLayout(context, second) == mvfx_errorStale &&
              mvfxDestroyLayout(NULL, second) == mvfx_errorInvalid &&
              Make(context, &good, 1, 12, &third) == mvfx_success,
          "destroyed and made again");
    mvfxDestroyContext(context);
}

// A particle whose colour and frame a host module sets after motion,
// written by the output alone in a record of 16 bytes.
static float s_paint[4];
static void Paint(const mvfxModuleCall* call, void* user)
{
    (void)user;
    for (int32_t c = 0; c < 4; ++c)
    {
        call->particles.color[c][0] = s_paint[c];
    }
    call->particles.frame[0] = 7;
}

static const mvfxHostModule s_painter = {.stage = mvfx_stageAfterMotion, .function = Paint};

static void Write(mvfxFormat format, bool srgb, int32_t components, const float values[4],
                  unsigned char out[16])
{
    static mvfxContext* context = NULL;
    static mvfxInstanceId instance = {0};
    if (context == NULL)
    {
        context = NewContext(1);
        mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
        emitter.rate = 0.0f;
        emitter.capacity = 1;
        emitter.hostSpawnCapacity = 1;
        emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
        emitter.color =
            (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {1, 1, 1, 1}, .max = {1, 1, 1, 1}};
        emitter.frame = (mvfxFrameStart){.mode = mvfx_frameFixed, .first = 1, .last = 1};
        emitter.hostModules = &s_painter;
        emitter.hostModuleCount = 1;
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = &emitter;
        effectDef.emitterCount = 1;
        mvfxEffectId effect = {0};
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        static const double place[3] = {0};
        mvfxSpawnRequest request = {.count = 1, .positions = place};
        CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success &&
                  (instanceDef.effect = effect,
                   mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success) &&
                  mvfxInstance_Spawn(context, instance, 0, &request) == mvfx_success,
              "the painted particle");
    }
    memcpy(s_paint, values, sizeof s_paint);
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success, "painted");
    bool word = format == mvfx_formatUint32;
    mvfxLayoutField field = {.field = word ? mvfx_recordFrame : mvfx_recordColor,
                             .format = format,
                             .components = (uint8_t)components,
                             .srgb = srgb};
    mvfxLayoutId layout = {0};
    static const mvfxView view = {.origin = {0.0, 0.0, 0.0}};
    int64_t count = 0;
    memset(out, 0xEE, 16);
    CHECK(Make(context, &field, 1, 16, &layout) == mvfx_success &&
              mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, out, 1,
                                          &count) == mvfx_success &&
              count == 1 && mvfxDestroyLayout(context, layout) == mvfx_success,
          "written");
}

static void TestFormats(void)
{
    unsigned char b[16];
    uint16_t h[4];
    static const float halves[4] = {1.0f, -2.5f, 65504.0f, 1e6f};
    Write(mvfx_formatFloat16, false, 4, halves, b);
    memcpy(h, b, 8);
    CHECK(h[0] == 0x3C00 && h[1] == 0xC100 && h[2] == 0x7BFF && h[3] == 0x7C00, "binary16");
    static const float units[4] = {0.5f, 1.2f, -1.0f, -0.25f};
    Write(mvfx_formatUnorm8, false, 4, units, b);
    CHECK(b[0] == 128 && b[1] == 255 && b[2] == 0 && b[3] == 0 && b[4] == 0xEE, "unorm8");
    static const float signs[4] = {-1.0f, 0.5f, -0.5f, 2.0f};
    Write(mvfx_formatSnorm8, false, 4, signs, b);
    CHECK((int8_t)b[0] == -127 && (int8_t)b[1] == 64 && (int8_t)b[2] == -64 && (int8_t)b[3] == 127,
          "snorm8, half away from 0");
    static const float odd[4] = {-0.0f, -3.0f, 0.0f, 0.0f};
    Write(mvfx_formatSnorm8, false, 2, odd, b);
    CHECK(b[0] == 0 && (int8_t)b[1] == -127, "snorm8: -0 to 0, below -1 to -1");
    Write(mvfx_formatUnorm16, false, 2, units, b);
    memcpy(h, b, 4);
    CHECK(h[0] == 32768 && h[1] == 65535, "unorm16");
    Write(mvfx_formatSnorm16, false, 2, signs, b);
    memcpy(h, b, 4);
    CHECK((int16_t)h[0] == -32767 && (int16_t)h[1] == 16384, "snorm16");
    // sRGB: 0.5 is 0.7354 encoded, 188; 0.002 on the linear part, 7; the
    // alpha stays linear.
    static const float colors[4] = {0.5f, 0.002f, 1.0f, 0.5f};
    Write(mvfx_formatUnorm8, true, 4, colors, b);
    CHECK(b[0] == 188 && b[1] == 7 && b[2] == 255 && b[3] == 128, "sRGB, alpha linear");
    static const float any[4] = {0};
    Write(mvfx_formatUint32, false, 1, any, b);
    uint32_t w[2];
    memcpy(w, b, 8);
    CHECK(w[0] == 7 && w[1] == 0xEEEEEEEEu, "words");
    static const float exact[4] = {0.1f};
    Write(mvfx_formatFloat32, false, 1, exact, b);
    float f = 0.0f;
    memcpy(&f, b, 4);
    CHECK(f == 0.1f && b[4] == 0xEE, "binary32");
}

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static Scene NewScene(const mvfxEmitterDef* emitter)
{
    Scene scene = {.context = NewContext(4)};
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

static mvfxEmitterDef Held(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.hostSpawnCapacity = 8;
    emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
    return emitter;
}

static void Place(Scene* scene, double x, double y, double z, float vx)
{
    const double place[3] = {x, y, z};
    const float velocity[3] = {vx, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = velocity};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "placed");
}

// A record: position, colour as unorm8, size as binary16, age, serial,
// frame, velocity as snorm16, then two bytes of padding.
typedef struct Record
{
    float position[3];
    uint8_t color[4];
    uint16_t size;
    uint16_t pad0;
    float age;
    uint32_t serial;
    uint32_t frame;
    int16_t velocity[3];
    uint16_t pad1;
} Record;

static const mvfxLayoutField s_fields[7] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordColor, .format = mvfx_formatUnorm8, .components = 4, .offset = 12},
    {.field = mvfx_recordSize, .format = mvfx_formatFloat16, .components = 1, .offset = 16},
    {.field = mvfx_recordAge, .format = mvfx_formatFloat32, .components = 1, .offset = 20},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 24},
    {.field = mvfx_recordFrame, .format = mvfx_formatUint32, .components = 1, .offset = 28},
    {.field = mvfx_recordVelocity, .format = mvfx_formatSnorm16, .components = 3, .offset = 32},
};

static void TestRecords(void)
{
    mvfxEmitterDef emitter = Held();
    emitter.color = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                       .min = {1.0f, 0.5f, 0.0f, 0.25f},
                                       .max = {1.0f, 0.5f, 0.0f, 0.25f}};
    Scene scene = NewScene(&emitter);
    mvfxLayoutId layout = {0};
    CHECK(Make(scene.context, s_fields, 7, (uint32_t)sizeof(Record), &layout) == mvfx_success,
          "layout");
    Place(&scene, 1001.5, 2, 3, 0.5f);
    Place(&scene, 999, 0, 0, -0.25f);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 2) == mvfx_success, "step");
    static const mvfxView view = {.origin = {1000.0, 0.0, 0.0}};
    Record records[3];
    memset(records, 0xEE, sizeof records);
    int64_t count = -1;
    CHECK(mvfxInstance_WriteParticles(scene.context, scene.instance, 0, layout, &view, NULL, 0,
                                      records, 3, &count) == mvfx_success &&
              count == 2,
          "two records");
    const Record* r = &records[0];
    CHECK(fabsf(r->position[0] - 1.75f) <= 1e-5f && r->position[1] == 2.0f &&
              r->position[2] == 3.0f,
          "relative to the view's origin");
    CHECK(r->color[0] == 255 && r->color[1] == 128 && r->color[2] == 0 && r->color[3] == 64 &&
              r->size == 0x3C00 && fabsf(r->age - 0.25f) <= 1e-6f && r->serial == 0 &&
              r->frame == 0 && r->velocity[0] == 16384 && r->velocity[1] == 0,
          "its colour, size, age, serial, frame, velocity");
    CHECK(records[1].serial == 1 && records[1].velocity[0] == -8192 && r->pad0 == 0xEEEE &&
              r->pad1 == 0xEEEE && records[2].serial == 0xEEEEEEEEu,
          "the second; padding and the rest untouched");
    // A buffer of one takes the first; none at all, the count.
    memset(records, 0xEE, sizeof records);
    CHECK(mvfxInstance_WriteParticles(scene.context, scene.instance, 0, layout, &view, NULL, 0,
                                      records, 1, &count) == mvfx_success &&
              count == 2 && records[0].serial == 0 && records[1].serial == 0xEEEEEEEEu,
          "a buffer too small");
    CHECK(mvfxInstance_WriteParticles(scene.context, scene.instance, 0, layout, &view, NULL, 0,
                                      NULL, 0, &count) == mvfx_success &&
              count == 2,
          "the count alone");
    mvfxDestroyContext(scene.context);
}

static const mvfxColorKey s_redToBlue[2] = {{0.0f, 1, 0, 0}, {1.0f, 0, 0, 1}};
static const mvfxAlphaKey s_fading[2] = {{0.0f, 1}, {1.0f, 0.5f}};
static const mvfxCurveKey s_rising[2] = {
    {.time = 0.0f, .value = 0.0f, .interpolation = mvfx_interpolationLinear},
    {.time = 1.0f, .value = 1.0f, .interpolation = mvfx_interpolationLinear}};

// Records written in runs: about 150 particles of as many ages, colour,
// alpha and size over life by age, each record the particle's own values
// (more particles than one run, so a run's ages are its own).
static void TestRuns(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 256;
    emitter.rate = 100.0f;
    emitter.lifetime = mvfxRangeSource(4.0f, 4.0f);
    emitter.overLife.color =
        (mvfxColorOverLife){.enabled = true,
                            .gradient = {s_redToBlue, 2, s_fading, 2},
                            .scaleAlpha = true,
                            .alpha = {.value = 1.0f, .curve = {.keys = s_rising, .keyCount = 2}}};
    emitter.overLife.size = (mvfxSizeOverLife){
        .enabled = true, .size = {{.value = 2.0f, .curve = {.keys = s_rising, .keyCount = 2}}}};
    Scene scene = NewScene(&emitter);
    for (int32_t step = 0; step < 90; ++step)
    {
        CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success,
              "step");
    }
    static const mvfxLayoutField fields[4] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordColor, .format = mvfx_formatFloat32, .components = 4, .offset = 12},
        {.field = mvfx_recordSize, .format = mvfx_formatFloat32, .components = 3, .offset = 28},
        {.field = mvfx_recordAge, .format = mvfx_formatFloat32, .components = 1, .offset = 40},
    };
    mvfxLayoutId layout = {0};
    CHECK(Make(scene.context, fields, 4, 44, &layout) == mvfx_success, "layout");
    static float records[256][11];
    static const mvfxView view = {.origin = {0.0, 0.0, 0.0}};
    int64_t count = 0;
    CHECK(mvfxInstance_WriteParticles(scene.context, scene.instance, 0, layout, &view, NULL, 0,
                                      records, 256, &count) == mvfx_success &&
              count > 128,
          "more records than two runs");
    // At age t: red to blue, alpha fading from 1 to 0.5 times t, size 2t.
    int32_t same = 0;
    for (int32_t i = 0; i < (int32_t)count; ++i)
    {
        const float* r = records[i];
        double t = (double)r[10];
        const double color[4] = {1.0 - t, 0.0, t, (1.0 - 0.5 * t) * t};
        bool own = true;
        for (int32_t c = 0; c < 4; ++c)
        {
            own = own && fabs((double)r[3 + c] - color[c]) <= 2e-3;
        }
        for (int32_t c = 0; c < 3; ++c)
        {
            own = own && fabs((double)r[7 + c] - 2.0 * t) <= 2e-3;
        }
        same += own ? 1 : 0;
    }
    CHECK(same == (int32_t)count, "each record holds its own particle's colour and size");
    CHECK(records[0][3] != records[count - 1][3] && records[0][6] != records[count - 1][6] &&
              records[0][7] != records[count - 1][7],
          "the first and last differ in age");
    mvfxDestroyContext(scene.context);
}

static void TestLocal(void)
{
    // A local emitter under an instance at (100, 0, 0) turned a quarter
    // about Z at scale 2: its (1, 0, 0) moving (1, 0, 0) at size 3 is the
    // world's (100, 2, 0) moving (0, 2, 0) at size 6, turned as the
    // instance; its custom attribute's two components, then zero.
    mvfxEmitterDef emitter = Held();
    emitter.space = mvfx_spaceLocal;
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {3, 3, 3, 0}, .max = {3, 3, 3, 0}, .uniform = true};
    mvfxCustomAttribute custom = {
        .components = 2,
        .initial = {.kind = mvfx_sourceRange, .min = {0.25f, 0.75f}, .max = {0.25f, 0.75f}}};
    emitter.custom = &custom;
    emitter.customCount = 1;
    Scene scene = NewScene(&emitter);
    static const double origin[3] = {100.0, 0.0, 0.0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    Place(&scene, 1, 0, 0, 1.0f);
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    static const mvfxLayoutField fields[5] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordVelocity, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
        {.field = mvfx_recordSize, .format = mvfx_formatFloat32, .components = 1, .offset = 24},
        {.field = mvfx_recordOrientation,
         .format = mvfx_formatFloat32,
         .components = 4,
         .offset = 28},
        {.field = mvfx_recordCustom, .format = mvfx_formatFloat32, .components = 3, .offset = 44},
    };
    mvfxLayoutId layout = {0};
    CHECK(Make(scene.context, fields, 5, 56, &layout) == mvfx_success, "layout");
    static const mvfxView view = {.origin = {0.0, 0.0, 0.0}};
    float record[14];
    int64_t count = 0;
    CHECK(mvfxInstance_WriteParticles(scene.context, scene.instance, 0, layout, &view, NULL, 0,
                                      record, 1, &count) == mvfx_success &&
              count == 1,
          "one record");
    CHECK(fabsf(record[0] - 100.0f) <= 1e-4f && fabsf(record[1] - 2.0f) <= 1e-4f &&
              fabsf(record[3]) <= 1e-5f && fabsf(record[4] - 2.0f) <= 1e-5f &&
              fabsf(record[6] - 6.0f) <= 1e-5f,
          "its place, velocity and size in the world");
    CHECK(fabsf(record[9] - 0.70710678f) <= 1e-6f && fabsf(record[10] - 0.70710678f) <= 1e-6f &&
              record[7] == 0.0f,
          "turned as the instance");
    CHECK(record[11] == 0.25f && record[12] == 0.75f && record[13] == 0.0f, "its custom attribute");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    mvfxEmitterDef emitter = Held();
    Scene scene = NewScene(&emitter);
    mvfxLayoutId layout = {0};
    CHECK(Make(scene.context, s_fields, 7, (uint32_t)sizeof(Record), &layout) == mvfx_success,
          "layout");
    static const mvfxView view = {.origin = {0, 0, 0}};
    static const mvfxView far = {.origin = {0x1p53, 0, 0}};
    static const mvfxView farY = {.origin = {0, 0x1p53, 0}};
    static const mvfxView farZ = {.origin = {0, 0, -0x1p53}};
    Record records[1];
    int64_t count = 0;
    uint64_t misuse = mvfxGetContextMisuse(scene.context);
    mvfxContext* c = scene.context;
    mvfxInstanceId i = scene.instance;
    CHECK(mvfxInstance_WriteParticles(c, i, 1, layout, &view, NULL, 0, records, 1, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, layout, NULL, NULL, 0, records, 1, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, layout, &view, NULL, 0, records, 1, NULL) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, layout, &view, NULL, 0, records, -1, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, layout, &view, NULL, 0, NULL, 1, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, layout, &far, NULL, 0, records, 1, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, layout, &farY, NULL, 0, records, 1, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, layout, &farZ, NULL, 0, records, 1, &count) ==
                  mvfx_errorInvalid,
          "refused");
    CHECK(mvfxGetContextMisuse(c) == misuse + 8, "each counted");
    mvfxInstanceId stale = i;
    stale.generation += 1;
    mvfxLayoutId staleLayout = layout;
    staleLayout.generation += 1;
    CHECK(mvfxInstance_WriteParticles(c, stale, 0, layout, &view, NULL, 0, records, 1, &count) ==
                  mvfx_errorStale &&
              mvfxInstance_WriteParticles(c, i, 0, staleLayout, &view, NULL, 0, records, 1,
                                          &count) == mvfx_errorStale &&
              mvfxInstance_WriteParticles(NULL, i, 0, layout, &view, NULL, 0, records, 1, &count) ==
                  mvfx_errorInvalid,
          "stale, no context");
    mvfxDestroyContext(c);

    // A context's layouts: from 1 to 65,536.
    mvfxContextDef def = mvfxDefaultContextDef();
    mvfxContext* other = NULL;
    def.limits.layouts = 0;
    CHECK(mvfxCreateContext(&def, &other) == mvfx_errorInvalid, "no layouts");
    def.limits.layouts = 65537;
    CHECK(mvfxCreateContext(&def, &other) == mvfx_errorInvalid, "too many layouts");
    def.limits.layouts = 65536;
    CHECK(mvfxCreateContext(&def, &other) == mvfx_success, "the most layouts");
    mvfxDestroyContext(other);
}

static void TestFrameAsFloat(void)
{
    // A fixed frame of 5, as binary32.
    mvfxEmitterDef emitter = Held();
    emitter.frame = (mvfxFrameStart){.mode = mvfx_frameFixed, .first = 5, .last = 5};
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 0.0f);
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    static const mvfxLayoutField field = {
        .field = mvfx_recordFrame, .format = mvfx_formatFloat32, .components = 1};
    mvfxLayoutId layout = {0};
    CHECK(Make(scene.context, &field, 1, 4, &layout) == mvfx_success, "a float frame");
    static const mvfxView view = {.origin = {0, 0, 0}};
    float frame = 0.0f;
    int64_t count = 0;
    CHECK(mvfxInstance_WriteParticles(scene.context, scene.instance, 0, layout, &view, NULL, 0,
                                      &frame, 1, &count) == mvfx_success &&
              frame == 5.0f,
          "frame 5 as a float");
    mvfxDestroyContext(scene.context);
}

static void TestMeshIndex(void)
{
    // Emitter 0 draws one mesh, 3; emitter 1 any of 2 to 5 alike; emitter
    // 2 none, written 0. As uint32 and as binary32.
    mvfxEmitterDef emitters[3] = {Held(), Held(), Held()};
    emitters[0].meshIndex = (mvfxFrameStart){.mode = mvfx_frameFixed, .first = 3, .last = 3};
    emitters[1].meshIndex = (mvfxFrameStart){.mode = mvfx_frameRandom, .first = 2, .last = 5};
    emitters[1].capacity = 400;
    emitters[1].hostSpawnCapacity = 400;
    Scene scene = {.context = NewContext(4)};
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = 3;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    static double places[400 * 3];
    for (int32_t e = 0; e < 3; ++e)
    {
        mvfxSpawnRequest request = {.count = e == 1 ? 400 : 2, .positions = places};
        CHECK(mvfxInstance_Spawn(scene.context, scene.instance, e, &request) == mvfx_success,
              "spawned");
    }
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    static const mvfxLayoutField fields[2] = {
        {.field = mvfx_recordMesh, .format = mvfx_formatUint32, .components = 1},
        {.field = mvfx_recordMesh, .format = mvfx_formatFloat32, .components = 1, .offset = 4},
    };
    mvfxLayoutId layout = {0};
    CHECK(Make(scene.context, fields, 2, 8, &layout) == mvfx_success, "layout");
    static const mvfxLayoutField two = {
        .field = mvfx_recordMesh, .format = mvfx_formatUint32, .components = 2};
    mvfxLayoutId refusedLayout = {0};
    CHECK(Make(scene.context, &two, 1, 8, &refusedLayout) == mvfx_errorInvalid,
          "a mesh index has one component");
    static const mvfxView view = {.origin = {0, 0, 0}};
    static uint32_t records[400 * 2];
    int64_t count = 0;
    CHECK(mvfxInstance_WriteParticles(scene.context, scene.instance, 0, layout, &view, NULL, 0,
                                      records, 400, &count) == mvfx_success &&
              count == 2 && records[0] == 3 && records[2] == 3,
          "one mesh");
    float asFloat = 0.0f;
    memcpy(&asFloat, &records[1], 4);
    CHECK(asFloat == 3.0f, "as binary32");
    CHECK(mvfxInstance_WriteParticles(scene.context, scene.instance, 1, layout, &view, NULL, 0,
                                      records, 400, &count) == mvfx_success &&
              count == 400,
          "four hundred");
    int32_t seen[8] = {0};
    for (int32_t i = 0; i < 400; ++i)
    {
        seen[records[2 * i] < 8 ? records[2 * i] : 7] += 1;
    }
    CHECK(seen[0] == 0 && seen[1] == 0 && seen[6] == 0 && seen[7] == 0 && seen[2] > 60 &&
              seen[3] > 60 && seen[4] > 60 && seen[5] > 60,
          "any of 2 to 5, alike");
    CHECK(mvfxInstance_WriteParticles(scene.context, scene.instance, 2, layout, &view, NULL, 0,
                                      records, 400, &count) == mvfx_success &&
              count == 2 && records[0] == 0 && records[1] == 0,
          "none, 0");
    // Checked: a mode, first below 0 or above last, last too large.
    mvfxEffectId refused = {0};
    int32_t refusals = 0;
    for (int32_t rule = 0; rule < 4; ++rule)
    {
        mvfxFrameStart bad[4] = {{.mode = 3},
                                 {.mode = mvfx_frameFixed, .first = -1, .last = 0},
                                 {.mode = mvfx_frameRandom, .first = 4, .last = 3},
                                 {.mode = mvfx_frameRandom, .last = MVFX_MAX_MESH_INDEX}};
        emitters[2].meshIndex = bad[rule];
        refusals += mvfxCreateEffect(scene.context, &effectDef, &refused) == mvfx_errorInvalid;
    }
    emitters[2].meshIndex =
        (mvfxFrameStart){.mode = mvfx_frameRandom, .last = MVFX_MAX_MESH_INDEX - 1};
    CHECK(refusals == 4 && mvfxCreateEffect(scene.context, &effectDef, &refused) == mvfx_success,
          "mesh indices checked");
    mvfxDestroyContext(scene.context);
}

static void TestLayoutView(void)
{
    // A layout's view: its fields as given and its stride; refusals.
    mvfxContext* context = NewContext(2);
    static const mvfxLayoutField fields[2] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordColor,
         .format = mvfx_formatUnorm8,
         .components = 4,
         .srgb = true,
         .offset = 12}};
    mvfxLayoutId id = {0};
    CHECK(Make(context, fields, 2, 20, &id) == mvfx_success, "a layout");
    mvfxLayoutView view;
    CHECK(mvfxLayout_GetView(context, id, &view) == mvfx_success && view.fieldCount == 2 &&
              view.stride == 20 && memcmp(view.fields, fields, sizeof fields) == 0,
          "its view");
    CHECK(mvfxLayout_GetView(NULL, id, &view) == mvfx_errorInvalid && view.fields == NULL &&
              mvfxLayout_GetView(context, id, NULL) == mvfx_errorInvalid &&
              mvfxDestroyLayout(context, id) == mvfx_success &&
              mvfxLayout_GetView(context, id, &view) == mvfx_errorStale && view.stride == 0,
          "refusals");
    mvfxDestroyContext(context);
}

static void TestSrgbSteps(void)
{
    // Each step is the least value whose sRGB byte reaches it: its byte is
    // the step's count, the value below's one less.
    float steps[MVFX_SRGB_STEPS];
    mvfxSrgbSteps(steps);
    bool rising = steps[0] > 0.0f && steps[MVFX_SRGB_STEPS - 1] <= 1.0f;
    for (int32_t k = 0; rising && k < MVFX_SRGB_STEPS; ++k)
    {
        const float at[4] = {steps[k], 0, 0, 1};
        const float below[4] = {nextafterf(steps[k], 0.0f), 0, 0, 1};
        unsigned char a[16];
        unsigned char b[16];
        Write(mvfx_formatUnorm8, true, 4, at, a);
        Write(mvfx_formatUnorm8, true, 4, below, b);
        rising = (k == 0 || steps[k] > steps[k - 1]) && a[0] == k + 1 && b[0] == k;
    }
    CHECK(rising, "the sRGB steps");
}

int main(void)
{
    TestLayouts();
    TestLayoutView();
    TestSrgbSteps();
    TestFormats();
    TestRecords();
    TestRuns();
    TestLocal();
    TestRefusals();
    TestFrameAsFloat();
    TestMeshIndex();
    return s_failures == 0 ? 0 : 1;
}
