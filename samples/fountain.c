// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A fountain: an emitter spraying water up in a narrow cone under gravity
// and a side wind, fading and growing over life, bouncing on the ground.
// Each second it writes the frame a renderer would draw: the particles
// sorted back to front from a camera, and the same particles as quads
// facing it. It prints what it wrote and returns 0 when the fountain
// stands as it should: as many particles as its rate keeps alive, none
// below the ground, the records in order and a quad for each.

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define CAPACITY 1024

// A record: place, colour and size, 40 bytes.
typedef struct Record
{
    float position[3];
    float color[4];
    float size[3];
} Record;

static Record s_records[CAPACITY];
static float s_vertices[4 * CAPACITY][5];
static uint32_t s_indices[6 * CAPACITY];
static uint32_t s_scratch[4 * CAPACITY];

static mvfxEffectId MakeFountain(mvfxContext* context)
{
    // Blue water fading out over its life, its drops growing from 0.05 to
    // 0.15.
    static const mvfxColorKey colors[2] = {{0.0f, 0.6f, 0.8f, 1.0f}, {1.0f, 0.9f, 0.95f, 1.0f}};
    static const mvfxAlphaKey alphas[2] = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    static const mvfxCurveKey growth[2] = {
        {.time = 0.0f, .value = 0.05f, .interpolation = mvfx_interpolationLinear},
        {.time = 1.0f, .value = 0.15f, .interpolation = mvfx_interpolationLinear}};
    // A wind blowing along +X at 1 a second, and the ground at y = 0.
    static const mvfxForce wind = {
        .kind = mvfx_forceWind, .vector = {1.0f, 0.0f, 0.0f}, .strength = 0.3f};
    static const mvfxCollider ground = {.kind = mvfx_colliderPlane, .normal = {0.0f, 1.0f, 0.0f}};

    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = CAPACITY;
    emitter.rate = 200.0f;
    emitter.lifetime = mvfxRangeSource(2.0f, 2.5f);
    emitter.shape.kind = mvfx_shapeCircle;
    emitter.shape.radius = 0.1f;
    emitter.shape.rotation[0] = 0.70710678f; // the circle turned into the XZ plane
    emitter.shape.rotation[3] = 0.70710678f;
    emitter.velocity = (mvfxVelocity){.mode = mvfx_directionCone,
                                      .direction = {0.0f, 1.0f, 0.0f},
                                      .coneAngle = 0.15f,
                                      .speed = {5.0f, 6.0f}};
    emitter.motion.gravity[1] = -9.8f;
    emitter.motion.drag = 0.1f;
    emitter.forces = &wind;
    emitter.forceCount = 1;
    emitter.colliders = &ground;
    emitter.colliderCount = 1;
    emitter.collision.enable = true;
    emitter.collision.restitution = 0.3f;
    emitter.collision.friction = 0.4f;
    emitter.overLife.color.enable = true;
    emitter.overLife.color.gradient = (mvfxGradient){
        .colorKeys = colors, .colorKeyCount = 2, .alphaKeys = alphas, .alphaKeyCount = 2};
    emitter.overLife.size.enable = true;
    emitter.overLife.size.size[0] =
        (mvfxLifeCurve){.value = 1.0f, .curve = {.keys = growth, .keyCount = 2}};
    emitter.sortMode = mvfx_sortBackToFront;
    emitter.sortMeasure = mvfx_sortByDepth;

    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(context, &effectDef, &effect) != mvfx_success)
    {
        printf("the fountain's effect was refused\n");
    }
    return effect;
}

static bool MakeLayouts(mvfxContext* context, mvfxLayoutId* records, mvfxLayoutId* vertices)
{
    static const mvfxLayoutField recordFields[3] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordColor, .format = mvfx_formatFloat32, .components = 4, .offset = 12},
        {.field = mvfx_recordSize, .format = mvfx_formatFloat32, .components = 3, .offset = 28},
    };
    static const mvfxLayoutField vertexFields[2] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 12},
    };
    mvfxLayoutDef def = mvfxDefaultLayoutDef();
    def.fields = recordFields;
    def.fieldCount = 3;
    def.stride = (uint32_t)sizeof(Record);
    bool made = mvfxCreateLayout(context, &def, records) == mvfx_success;
    def.fields = vertexFields;
    def.fieldCount = 2;
    def.stride = 20;
    return made && mvfxCreateLayout(context, &def, vertices) == mvfx_success;
}

// Writes a frame from a camera 8 units out along +Z, 2 up, and checks it.
static bool WriteFrame(mvfxContext* context, mvfxInstanceId instance, mvfxLayoutId records,
                       mvfxLayoutId vertices, double second)
{
    const mvfxView view = {
        .eye = {0.0, 2.0, 8.0}, .forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
    int64_t count = 0;
    int64_t quads = 0;
    mvfxBounds bounds;
    const mvfxExpandBuffers buffers = {.vertices = s_vertices,
                                       .vertexCapacity = 4 * CAPACITY,
                                       .indices = s_indices,
                                       .indexCapacity = 6 * CAPACITY,
                                       .scratch = s_scratch,
                                       .scratchBytes = (int64_t)sizeof s_scratch};
    if (mvfxInstance_WriteParticles(context, instance, 0, records, &view, s_scratch,
                                    (int64_t)sizeof s_scratch, s_records, CAPACITY,
                                    &count) != mvfx_success ||
        mvfxInstance_ExpandSprites(context, instance, 0, vertices, &view, &buffers, &quads) !=
            mvfx_success ||
        mvfxInstance_GetBounds(context, instance, 0, &bounds) != mvfx_success)
    {
        printf("a frame was refused\n");
        return false;
    }
    bool ordered = true;
    bool grounded = true;
    double alpha = 0.0;
    for (int64_t i = 0; i < count; ++i)
    {
        ordered = ordered && (i == 0 || s_records[i - 1].position[2] <= s_records[i].position[2]);
        grounded = grounded && s_records[i].position[1] > -1e-3f;
        alpha += (double)s_records[i].color[3];
    }
    printf("%.0f s: %lld particles, %lld quads, mean alpha %.2f, bounds (%.2f %.2f %.2f) to "
           "(%.2f %.2f %.2f)\n",
           second, (long long)count, (long long)quads, count > 0 ? alpha / (double)count : 0.0,
           bounds.min[0], bounds.min[1], bounds.min[2], bounds.max[0], bounds.max[1],
           bounds.max[2]);
    // At 200 a second, living 2 to 2.5 seconds: about 450 once full.
    bool full = second < 3.0 || (count > 400 && count < 500);
    return ordered && grounded && full && quads == count;
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
    instanceDef.effect = MakeFountain(context);
    mvfxInstanceId instance = {0};
    mvfxLayoutId records = {0};
    mvfxLayoutId vertices = {0};
    bool ok = mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success &&
              MakeLayouts(context, &records, &vertices);
    // Five seconds at 60 frames a second, a frame written each second.
    for (int32_t frame = 1; ok && frame <= 5 * 60; ++frame)
    {
        ok = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
             (frame % 60 != 0 || WriteFrame(context, instance, records, vertices, frame / 60.0));
    }
    mvfxDestroyContext(context);
    printf(ok ? "the fountain stands\n" : "the fountain failed\n");
    return ok ? 0 : 1;
}
