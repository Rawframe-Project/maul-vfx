// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A cloth on a sphere: a sheet of 24 by 24 points, 2.3 units a side, born
// flat over a ball and dropped onto it, solved in XPBD substeps (stretch,
// shear and bending) and colliding with the ball and the ground. Each
// second it expands the sheet into a mesh, a vertex a point and two
// triangles a quad with normals, as a renderer would draw it; given a path
// it writes the last mesh there as an OBJ file. It returns 0 when the
// cloth has draped: no point inside the ball, its middle resting on the
// ball's top, its edges hanging below the ball's middle, and no edge
// stretched past a tenth of its rest length.

// Before every include: the C library's fopen without MSVC's warning.
#define _CRT_SECURE_NO_WARNINGS

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SIDE    24
#define SPACING 0.1f
#define RADIUS  0.6f
// The ball's center, 1 up.
#define BALL_Y 1.0f

// A vertex: place and face normal, 24 bytes.
static float s_vertices[SIDE * SIDE][6];
static uint32_t s_indices[6 * (SIDE - 1) * (SIDE - 1)];

static mvfxEffectId MakeEffect(mvfxContext* context)
{
    static const mvfxBurst once = {.minCount = 1, .maxCount = 1, .cycles = 1, .probability = 1.0f};
    static const mvfxCollider colliders[2] = {
        {.kind = mvfx_colliderSphere, .center = {0.0f, BALL_Y, 0.0f}, .radius = RADIUS},
        {.kind = mvfx_colliderPlane, .normal = {0.0f, 1.0f, 0.0f}},
    };

    // One sheet, its first point at a corner 2 up so the sheet's middle
    // lies over the ball, across X and down Z.
    mvfxEmitterDef cloth = mvfxDefaultEmitterDef();
    cloth.capacity = SIDE * SIDE;
    cloth.rate = 0.0f;
    cloth.bursts = &once;
    cloth.burstCount = 1;
    cloth.lifetime = mvfxRangeSource(100.0f, 100.0f);
    cloth.shape.offset[0] = -0.5f * SPACING * (SIDE - 1);
    cloth.shape.offset[1] = 2.0f;
    cloth.shape.offset[2] = -0.5f * SPACING * (SIDE - 1);
    cloth.motion.gravity[1] = -9.8f;
    cloth.motion.drag = 0.2f;
    cloth.unit = (mvfxUnitDef){.kind = mvfx_unitSheet,
                               .points = SIDE,
                               .rows = SIDE,
                               .spacing = SPACING,
                               .direction = {1.0f, 0.0f, 0.0f},
                               .downDirection = {0.0f, 0.0f, 1.0f},
                               .substeps = 8,
                               .stretchCompliance = 0.0f,
                               .shearCompliance = 1e-4f,
                               .bendCompliance = 1e-2f,
                               .damping = 1.0f};
    cloth.colliders = colliders;
    cloth.colliderCount = 2;
    cloth.collision = (mvfxCollision){.enable = true, .friction = 0.5f, .radius = 0.02f};

    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &cloth;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(context, &effectDef, &effect, NULL) != mvfx_success)
    {
        printf("the effect was refused\n");
    }
    return effect;
}

static float Length(const float* a, const float* b)
{
    float x = a[0] - b[0];
    float y = a[1] - b[1];
    float z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

// The sheet's mesh, and how it lies.
static bool Report(mvfxContext* context, mvfxInstanceId instance, mvfxLayoutId layout,
                   int32_t second, bool last)
{
    const mvfxView view = {
        .eye = {0.0, 2.0, 5.0}, .forward = {0.0f, -0.3f, -0.95f}, .up = {0.0f, 0.95f, -0.3f}};
    const mvfxExpandBuffers buffers = {.vertices = s_vertices,
                                       .vertexCapacity = SIDE * SIDE,
                                       .indices = s_indices,
                                       .indexCapacity = 6 * (SIDE - 1) * (SIDE - 1)};
    int64_t vertices = 0;
    int64_t indices = 0;
    if (mvfxInstance_ExpandSheets(context, instance, 0, layout, &view, &buffers, &vertices,
                                  &indices) != mvfx_success ||
        vertices != SIDE * SIDE || indices != 6 * (SIDE - 1) * (SIDE - 1))
    {
        printf("the sheet was not expanded\n");
        return false;
    }
    float deepest = INFINITY;
    float lowest = INFINITY;
    float stretch = 0.0f;
    const float ball[3] = {0.0f, BALL_Y, 0.0f};
    for (int32_t r = 0; r < SIDE; ++r)
    {
        for (int32_t c = 0; c < SIDE; ++c)
        {
            const float* v = s_vertices[r * SIDE + c];
            deepest = fminf(deepest, Length(v, ball) - RADIUS);
            lowest = fminf(lowest, v[1]);
            if (c + 1 < SIDE)
            {
                stretch = fmaxf(stretch, Length(v, s_vertices[r * SIDE + c + 1]) / SPACING - 1.0f);
            }
            if (r + 1 < SIDE)
            {
                stretch =
                    fmaxf(stretch, Length(v, s_vertices[(r + 1) * SIDE + c]) / SPACING - 1.0f);
            }
        }
    }
    const float* middle = s_vertices[(SIDE / 2) * SIDE + SIDE / 2];
    printf("%d s: %lld vertices, %lld indices; middle at %.2f up, lowest %.2f, nearest the ball "
           "%.3f from it, most stretch %.1f%%\n",
           second, (long long)vertices, (long long)indices, (double)middle[1], (double)lowest,
           (double)deepest, 100.0 * (double)stretch);
    bool outside = deepest > -1e-3f;
    bool draped = middle[1] > BALL_Y + RADIUS - 0.05f && lowest < BALL_Y && stretch < 0.1f;
    return outside && (!last || draped);
}

// Writes the last mesh as an OBJ file: positions, normals and faces.
static bool WriteObj(const char* path)
{
    FILE* file = fopen(path, "w");
    if (file == NULL)
    {
        printf("cannot write %s\n", path);
        return false;
    }
    for (int32_t i = 0; i < SIDE * SIDE; ++i)
    {
        const float* v = s_vertices[i];
        fprintf(file, "v %.6f %.6f %.6f\nvn %.6f %.6f %.6f\n", (double)v[0], (double)v[1],
                (double)v[2], (double)v[3], (double)v[4], (double)v[5]);
    }
    for (int32_t i = 0; i < 6 * (SIDE - 1) * (SIDE - 1); i += 3)
    {
        uint32_t a = s_indices[i] + 1;
        uint32_t b = s_indices[i + 1] + 1;
        uint32_t c = s_indices[i + 2] + 1;
        fprintf(file, "f %u//%u %u//%u %u//%u\n", a, a, b, b, c, c);
    }
    bool written = fclose(file) == 0;
    printf(written ? "wrote %s\n" : "cannot write %s\n", path);
    return written;
}

int main(int argc, char** argv)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        printf("no context\n");
        return 1;
    }
    static const mvfxLayoutField fields[2] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordFaceNormal,
         .format = mvfx_formatFloat32,
         .components = 3,
         .offset = 12},
    };
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = fields;
    layoutDef.fieldCount = 2;
    layoutDef.stride = 24;
    mvfxLayoutId layout = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = MakeEffect(context);
    mvfxInstanceId instance = {0};
    bool ok = mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success &&
              mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
    // Four seconds at 60 steps a second.
    for (int32_t step = 1; ok && step <= 4 * 60; ++step)
    {
        ok = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
             (step % 60 != 0 || Report(context, instance, layout, step / 60, step == 4 * 60));
    }
    mvfxDestroyContext(context);
    ok = ok && (argc < 2 || WriteObj(argv[1]));
    printf(ok ? "the cloth lies on the ball\n" : "the cloth failed\n");
    return ok ? 0 : 1;
}
