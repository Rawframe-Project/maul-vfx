// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A liquid poured into a tank: a stream of 150 particles of spacing 0.15
// falls into a tank a unit wide and settles, solved as position-based
// fluids, while a surface gas wraps the particles in a signed distance.
// Each second the host meshes the surface by marching cubes, as it would
// draw the water; given a path it writes the last mesh there as an OBJ
// file. It returns 0 when every particle stays in the tank, the liquid
// has settled, and its surface lies near the level its volume fills.

// Before every include: the C library's fopen without MSVC's warning.
#define _CRT_SECURE_NO_WARNINGS

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define DROPS         150
#define SPACING       0.15f
#define CELLS         16
#define MOST_VERTICES 32768
#define MOST_INDICES  (6 * MOST_VERTICES)

static float s_positions[MOST_VERTICES][3];
static float s_normals[MOST_VERTICES][3];
static uint32_t s_indices[MOST_INDICES];
static uint32_t s_scratch[16 * CELLS * CELLS];
static float s_drops[DROPS][3];

static mvfxEffectId MakeEffect(mvfxContext* context)
{
    // The tank: particles kept inside a box a unit on a side, its floor at
    // y = 0.
    static const mvfxCollider tank = {.kind = mvfx_colliderBox,
                                      .inside = true,
                                      .center = {0.0f, 0.5f, 0.0f},
                                      .halfExtents = {0.5f, 0.5f, 0.5f},
                                      .rotation = {0.0f, 0.0f, 0.0f, 1.0f}};

    // A stream for a second from a ring near the top, pointing down.
    mvfxEmitterDef water = mvfxDefaultEmitterDef();
    water.capacity = DROPS;
    water.rate = 150.0f;
    water.duration = 1.0f;
    water.loops = 1;
    water.lifetime = mvfxRangeSource(100.0f, 100.0f);
    water.shape.kind = mvfx_shapeCircle;
    water.shape.offset[1] = 0.9f;
    water.shape.radius = 0.08f;
    water.shape.thickness = 1.0f;
    water.shape.rotation[0] = 0.70710678f; // the circle turned into the XZ plane
    water.shape.rotation[3] = 0.70710678f;
    water.velocity = (mvfxVelocity){
        .mode = mvfx_directionFixed, .direction = {0.0f, -1.0f, 0.0f}, .speed = {1.0f, 1.0f}};
    water.motion.gravity[1] = -9.8f;
    water.colliders = &tank;
    water.colliderCount = 1;
    water.collision = (mvfxCollision){.enable = true, .friction = 0.1f, .radius = 0.5f * SPACING};
    water.liquid.enable = true;
    water.liquid.spacing = SPACING;
    water.liquid.viscosity = 0.05f;

    // The surface: a box of 16 cubed cells of 0.075 holding the tank.
    mvfxGasDef surface = mvfxDefaultGasDef();
    surface.kind = mvfx_gasSurface;
    surface.size[0] = CELLS;
    surface.size[1] = CELLS;
    surface.size[2] = CELLS;
    surface.cellSize = 1.2f / CELLS;
    surface.center[1] = 0.5f;
    surface.surface.emitter = 0;
    surface.surface.radius = SPACING;

    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &water;
    effectDef.emitterCount = 1;
    effectDef.gases = &surface;
    effectDef.gasCount = 1;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(context, &effectDef, &effect, NULL) != mvfx_success)
    {
        printf("the effect was refused\n");
    }
    return effect;
}

// Where the drops are: whether all are in the tank, their mean and
// highest height.
static bool Drops(mvfxContext* context, mvfxInstanceId instance, mvfxLayoutId layout,
                  int64_t* countOut, double* meanOut, double* highestOut)
{
    const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
    if (mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, s_drops, DROPS,
                                    countOut) != mvfx_success)
    {
        return false;
    }
    bool inside = true;
    *meanOut = 0.0;
    *highestOut = 0.0;
    for (int64_t i = 0; i < *countOut; ++i)
    {
        const float* d = s_drops[i];
        inside =
            inside && fabsf(d[0]) <= 0.5f && fabsf(d[2]) <= 0.5f && d[1] >= 0.0f && d[1] <= 1.0f;
        *meanOut += (double)d[1] / (double)*countOut;
        *highestOut = fmax(*highestOut, (double)d[1]);
    }
    return inside;
}

// Meshes the surface; the highest vertex is its level.
static bool Mesh(mvfxContext* context, mvfxInstanceId instance, int64_t* verticesOut,
                 int64_t* indicesOut, double* levelOut)
{
    int64_t scratchBytes = 0;
    const mvfxSurfaceMeshBuffers buffers = {.positions = &s_positions[0][0],
                                            .normals = &s_normals[0][0],
                                            .vertexCapacity = MOST_VERTICES,
                                            .indices = s_indices,
                                            .indexCapacity = MOST_INDICES,
                                            .scratch = s_scratch,
                                            .scratchBytes = (int64_t)sizeof s_scratch};
    const double origin[3] = {0.0, 0.0, 0.0};
    if (mvfxInstance_GetSurfaceMeshScratchBytes(context, instance, 0, &scratchBytes) !=
            mvfx_success ||
        scratchBytes > (int64_t)sizeof s_scratch ||
        mvfxInstance_WriteSurfaceMesh(context, instance, 0, origin, 0.0f, &buffers, verticesOut,
                                      indicesOut) != mvfx_success ||
        *verticesOut > MOST_VERTICES || *indicesOut > MOST_INDICES)
    {
        printf("the surface was not meshed\n");
        return false;
    }
    *levelOut = 0.0;
    for (int64_t i = 0; i < *verticesOut; ++i)
    {
        *levelOut = fmax(*levelOut, (double)s_positions[i][1]);
    }
    return true;
}

// Writes the last mesh as an OBJ file: positions, normals and triangles.
static bool WriteObj(const char* path, int64_t vertices, int64_t indices)
{
    FILE* file = fopen(path, "w");
    if (file == NULL)
    {
        printf("cannot write %s\n", path);
        return false;
    }
    for (int64_t i = 0; i < vertices; ++i)
    {
        const float* p = s_positions[i];
        const float* n = s_normals[i];
        fprintf(file, "v %.6f %.6f %.6f\nvn %.6f %.6f %.6f\n", (double)p[0], (double)p[1],
                (double)p[2], (double)n[0], (double)n[1], (double)n[2]);
    }
    for (int64_t i = 0; i + 2 < indices; i += 3)
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
    static const mvfxLayoutField place = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &place;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 12;
    mvfxLayoutId layout = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = MakeEffect(context);
    mvfxInstanceId instance = {0};
    bool ok = mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success &&
              mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
    int64_t drops = 0;
    double mean = 0.0;
    double highest = 0.0;
    int64_t vertices = 0;
    int64_t indices = 0;
    double level = 0.0;
    // Two seconds: one pouring, one settling.
    for (int32_t step = 1; ok && step <= 2 * 60; ++step)
    {
        ok = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
        if (ok && step % 60 == 0)
        {
            ok = Drops(context, instance, layout, &drops, &mean, &highest) &&
                 Mesh(context, instance, &vertices, &indices, &level);
            printf("%d s: %lld drops, %.2f up on average, the highest %.2f; the surface %lld "
                   "vertices, %lld triangles, its top %.2f up\n",
                   step / 60, (long long)drops, mean, highest, (long long)vertices,
                   (long long)(indices / 3), level);
        }
    }
    mvfxDestroyContext(context);
    // The volume, 150 drops of 0.15 cubed, would fill the tank to 0.5; the
    // drops at the walls, which have fewer neighbors, pack closer, more so
    // in a tank under seven drops wide, so the surface settles somewhat below.
    double fill = DROPS * (double)(SPACING * SPACING * SPACING);
    ok = ok && drops == DROPS && highest < fill + 0.2 && fabs(level - fill) < 0.15 &&
         (argc < 2 || WriteObj(argv[1], vertices, indices));
    printf(ok ? "the tank holds its water\n" : "the liquid failed\n");
    return ok ? 0 : 1;
}
