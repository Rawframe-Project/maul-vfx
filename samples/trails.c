// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Trails and a beam: an instance circles the origin with a tapering trail
// pinned to it, and a beam strikes from it to a point above the origin the
// host aims it at each frame. Each second it writes the frame a renderer
// would draw: the trail's points (place and coordinate along) and both
// ribbons expanded into strips facing the camera. It prints what it wrote
// and returns 0 when the trail is as long as the arc its source covered
// in a lifetime, it ends at the source, and the beam spans from the
// source to its target.

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TRAIL_CAPACITY 128
#define BEAM_SEGMENTS  32
#define RADIUS         2.0
// A turn every four seconds.
#define TURN_SECONDS 4.0
#define PI           3.14159265358979323846

static float s_points[TRAIL_CAPACITY * 4][4];
static mvfxRibbon s_ribbons[8];
static float s_vertices[2 * TRAIL_CAPACITY * 4][4];
static uint32_t s_indices[6 * TRAIL_CAPACITY * 4];
static uint32_t s_scratch[4 * TRAIL_CAPACITY];

static mvfxEffectId MakeEffect(mvfxContext* context)
{
    // The trail tapers from nothing at its oldest point to 0.2 at its
    // head, and fades in along it.
    static const mvfxCurveKey taper[2] = {
        {.time = 0.0f, .value = 0.0f, .interpolation = mvfx_interpolationLinear},
        {.time = 1.0f, .value = 1.0f, .interpolation = mvfx_interpolationLinear}};
    static const mvfxColorKey white[1] = {{0.0f, 1.0f, 1.0f, 1.0f}};
    static const mvfxAlphaKey fade[2] = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    // The beam's target: parameter 0, which the host sets each frame.
    static const float target[4] = {0.0f, 3.0f, 0.0f, 0.0f};

    mvfxEmitterDef emitters[2] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef()};
    mvfxEmitterDef* trail = &emitters[0];
    trail->capacity = TRAIL_CAPACITY;
    trail->rate = 60.0f;
    trail->lifetime = mvfxMakeRangeSource(1.0f, 1.0f);
    trail->size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.2f, 0.2f, 0.2f}, .max = {0.2f, 0.2f, 0.2f}};
    trail->ribbon.ids = mvfx_ribbonOne;
    trail->ribbon.pieces = 3;
    trail->ribbon.pinHead = true;
    trail->ribbon.width = (mvfxLifeCurve){.value = 1.0f, .curve = {.keys = taper, .keyCount = 2}};
    trail->ribbon.color = (mvfxGradient){
        .colorKeys = white, .colorKeyCount = 1, .alphaKeys = fade, .alphaKeyCount = 2};
    // The beam: 32 segments from the instance to the target, bent by noise
    // a tenth of a unit across, struck anew every tenth of a second.
    mvfxEmitterDef* beam = &emitters[1];
    beam->capacity = BEAM_SEGMENTS + 1;
    beam->rate = 0.0f;
    beam->lifetime = mvfxMakeRangeSource(0.1f, 0.1f);
    beam->ribbon.ids = mvfx_ribbonOne;
    beam->beam = (mvfxBeamDef){.enable = true,
                               .segments = BEAM_SEGMENTS,
                               .end = {.fromParameter = true, .parameter = 0},
                               .noiseAmplitude = 0.1f,
                               .noiseFrequency = 3.0f,
                               .noiseSpeed = 2.0f};

    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = 2;
    effectDef.parameters = target;
    effectDef.parameterCount = 1;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(context, &effectDef, &effect, NULL) != mvfx_success)
    {
        printf("the effect was refused\n");
    }
    return effect;
}

// Where the instance is at a time.
static void Place(double seconds, double at[3])
{
    double turn = 2.0 * PI * seconds / TURN_SECONDS;
    at[0] = RADIUS * cos(turn);
    at[1] = 0.0;
    at[2] = RADIUS * sin(turn);
}

static double Distance(const float a[3], const double b[3])
{
    double x = (double)a[0] - b[0];
    double y = (double)a[1] - b[1];
    double z = (double)a[2] - b[2];
    return sqrt(x * x + y * y + z * z);
}

// The trail's points and length, and its head's distance from the source.
static bool CheckTrail(mvfxContext* context, mvfxInstanceId instance, mvfxLayoutId points,
                       const mvfxView* view, const double source[3])
{
    const mvfxRibbonBuffers buffers = {.points = s_points,
                                       .pointCapacity = TRAIL_CAPACITY * 4,
                                       .ribbons = s_ribbons,
                                       .ribbonCapacity = 8,
                                       .scratch = s_scratch,
                                       .scratchBytes = (int64_t)sizeof s_scratch};
    int64_t count = 0;
    int64_t ribbons = 0;
    if (mvfxInstance_WriteRibbons(context, instance, 0, points, view, &buffers, &count, &ribbons) !=
            mvfx_success ||
        ribbons != 1)
    {
        printf("the trail was not written\n");
        return false;
    }
    double length = 0.0;
    for (int64_t i = 1; i < count; ++i)
    {
        const double before[3] = {(double)s_points[i - 1][0], (double)s_points[i - 1][1],
                                  (double)s_points[i - 1][2]};
        length += Distance(s_points[i], before);
    }
    double head = Distance(s_points[count - 1], source);
    // A lifetime's arc: a second of a turn of TURN_SECONDS.
    double arc = 2.0 * PI * RADIUS / TURN_SECONDS;
    printf("  trail: %lld points, %.3f long (its source covered %.3f), head %.2g from the "
           "source\n",
           (long long)count, length, arc, head);
    return fabs(length - arc) < 0.05 * arc && head < 1e-3;
}

// Both ribbons as strips; the beam's from the source to the target.
static bool CheckStrips(mvfxContext* context, mvfxInstanceId instance, mvfxLayoutId vertices,
                        const mvfxView* view, const double source[3])
{
    const mvfxExpandBuffers buffers = {.vertices = s_vertices,
                                       .vertexCapacity = 2 * TRAIL_CAPACITY * 4,
                                       .indices = s_indices,
                                       .indexCapacity = 6 * TRAIL_CAPACITY * 4,
                                       .scratch = s_scratch,
                                       .scratchBytes = (int64_t)sizeof s_scratch};
    int64_t trailVertices = 0;
    int64_t trailIndices = 0;
    int64_t beamVertices = 0;
    int64_t beamIndices = 0;
    bool written = mvfxInstance_ExpandRibbons(context, instance, 0, vertices, view, &buffers,
                                              &trailVertices, &trailIndices) == mvfx_success &&
                   mvfxInstance_ExpandRibbons(context, instance, 1, vertices, view, &buffers,
                                              &beamVertices, &beamIndices) == mvfx_success;
    if (!written || beamVertices != 2 * (BEAM_SEGMENTS + 1))
    {
        printf("the strips were not written\n");
        return false;
    }
    // The beam's ends: the middles of its first and last pairs of vertices.
    float first[3];
    float last[3];
    int64_t end = beamVertices - 2;
    for (int32_t k = 0; k < 3; ++k)
    {
        first[k] = 0.5f * (s_vertices[0][k] + s_vertices[1][k]);
        last[k] = 0.5f * (s_vertices[end][k] + s_vertices[end + 1][k]);
    }
    const double target[3] = {0.0, 3.0, 0.0};
    double fromSource = Distance(first, source);
    double toTarget = Distance(last, target);
    printf("  strips: the trail's %lld vertices and %lld indices, the beam's %lld and %lld; "
           "the beam's ends %.2g and %.2g from the source and the target\n",
           (long long)trailVertices, (long long)trailIndices, (long long)beamVertices,
           (long long)beamIndices, fromSource, toTarget);
    return trailIndices > 0 && beamIndices == 6 * BEAM_SEGMENTS && fromSource < 1e-3 &&
           toTarget < 1e-3;
}

static bool MakeLayouts(mvfxContext* context, mvfxLayoutId* points, mvfxLayoutId* vertices)
{
    static const mvfxLayoutField fields[2] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordAlong, .format = mvfx_formatFloat32, .components = 1, .offset = 12},
    };
    mvfxLayoutDef def = mvfxDefaultLayoutDef();
    def.fields = fields;
    def.fieldCount = 2;
    def.stride = 16;
    return mvfxCreateLayout(context, &def, points) == mvfx_success &&
           mvfxCreateLayout(context, &def, vertices) == mvfx_success;
}

int main(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        printf("no context\n");
        return 1;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = MakeEffect(context);
    mvfxInstanceId instance = {0};
    mvfxLayoutId points = {0};
    mvfxLayoutId vertices = {0};
    bool ok = mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success &&
              MakeLayouts(context, &points, &vertices);
    static const float still[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const mvfxView view = {
        .eye = {0.0, 6.0, 8.0}, .forward = {0.0f, -0.6f, -0.8f}, .up = {0.0f, 0.8f, -0.6f}};
    // Four seconds at 60 frames a second, a frame written each second.
    for (int32_t frame = 0; ok && frame < 4 * 60; ++frame)
    {
        // The host moves the instance, then aims the beam at the point
        // above the origin in the instance's space.
        double at[3];
        Place((frame + 1) / 60.0, at);
        const float aim[4] = {(float)-at[0], 3.0f, (float)-at[2], 0.0f};
        ok = mvfxInstance_SetTransform(context, instance, at, still, 1.0f, frame == 0) ==
                 mvfx_success &&
             mvfxInstance_SetParameter(context, instance, 0, aim) == mvfx_success &&
             mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
        if (ok && (frame + 1) % 60 == 0)
        {
            printf("%d s:\n", (frame + 1) / 60);
            ok = CheckTrail(context, instance, points, &view, at) &&
                 CheckStrips(context, instance, vertices, &view, at);
        }
    }
    mvfxDestroyContext(context);
    printf(ok ? "the trails hold\n" : "the trails failed\n");
    return ok ? 0 : 1;
}
