// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Convex hulls meeting colliders as rigid hull bodies show it, each body
// against a binary64 oracle of its gap from its hull's faces after every
// step: a ball's hull and a turned cube's rolling over big spheres, never
// sinking into them by more than 2e-5 and touching them at the end; a
// cube's hull dropped on its edge, a ball's hull, and both scaled
// unevenly, falling onto a box collider, never under its top by more than
// 2e-5 and resting on it.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/mesh.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <string.h>

// The most a body sinks into its collider, and is off it at the end. Every
// body here stays within 3e-6 of its collider; a nearest-point search
// ended a hundredth short sinks one by 8.5e-4 or more.
#define GAP 2e-5

static const float s_cube[24] = {-0.5f, -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,
                                 -0.5f, 0.5f,  0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,  0.5f,
                                 -0.5f, 0.5f,  -0.5f, 0.5f,  0.5f,  0.5f,  0.5f,  0.5f};

// 60 points on a ball of radius 0.5 (a Fibonacci sphere): every one a
// vertex of their hull.
static float s_ball[3 * 60];

static void MakeBall(void)
{
    for (int32_t i = 0; i < 60; ++i)
    {
        float y = 1.0f - (2.0f * (float)i + 1.0f) / 60.0f;
        float r = sqrtf(1.0f - y * y);
        float angle = 2.3999632f * (float)i;
        s_ball[3 * i] = 0.5f * r * cosf(angle);
        s_ball[3 * i + 1] = 0.5f * y;
        s_ball[3 * i + 2] = 0.5f * r * sinf(angle);
    }
}

static double Dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// The squared distance from p to triangle a b c, by its regions (Ericson
// 5.1.5).
static double TriangleDistance(const double p[3], const double a[3], const double b[3],
                               const double c[3])
{
    double ab[3];
    double ac[3];
    double ap[3];
    double bp[3];
    double cp[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        ab[k] = b[k] - a[k];
        ac[k] = c[k] - a[k];
        ap[k] = p[k] - a[k];
        bp[k] = p[k] - b[k];
        cp[k] = p[k] - c[k];
    }
    double d1 = Dot(ab, ap);
    double d2 = Dot(ac, ap);
    double d3 = Dot(ab, bp);
    double d4 = Dot(ac, bp);
    double d5 = Dot(ab, cp);
    double d6 = Dot(ac, cp);
    double vc = d1 * d4 - d3 * d2;
    double vb = d5 * d2 - d1 * d6;
    double va = d3 * d6 - d5 * d4;
    // The nearest point, a + u ab + w ac.
    double u = 0.0;
    double w = 0.0;
    if (d1 <= 0.0 && d2 <= 0.0)
    {
        u = 0.0;
    }
    else if (d3 >= 0.0 && d4 <= d3)
    {
        u = 1.0;
    }
    else if (d6 >= 0.0 && d5 <= d6)
    {
        w = 1.0;
    }
    else if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
    {
        u = d1 / (d1 - d3);
    }
    else if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
    {
        w = d2 / (d2 - d6);
    }
    else if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0)
    {
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        u = 1.0 - w;
    }
    else
    {
        double sum = va + vb + vc;
        u = vb / sum;
        w = vc / sum;
    }
    double off[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        off[k] = a[k] + u * ab[k] + w * ac[k] - p[k];
    }
    return Dot(off, off);
}

// v turned by quaternion q (x, y, z, w).
static void Rotate(const float q[4], const double v[3], double out[3])
{
    const double x = (double)q[0];
    const double y = (double)q[1];
    const double z = (double)q[2];
    const double w = (double)q[3];
    const double t[3] = {2.0 * (y * v[2] - z * v[1]), 2.0 * (z * v[0] - x * v[2]),
                         2.0 * (x * v[1] - y * v[0])};
    out[0] = v[0] + w * t[0] + (y * t[2] - z * t[1]);
    out[1] = v[1] + w * t[1] + (z * t[0] - x * t[2]);
    out[2] = v[2] + w * t[2] + (x * t[1] - y * t[0]);
}

// A body's gap from a collider: for a sphere, the distance from its center
// to the hull's faces less its radius; for a box (unturned), the hull's
// lowest vertex over its top. The hull is the view's, scaled by the body's
// size, turned by its orientation and placed at its position.
static double Gap(const mvfxEmitterView* view, const TestParticle* body,
                  const mvfxCollider* collider)
{
    double world[MVFX_MAX_HULL_VERTICES][3];
    for (int32_t v = 0; v < view->hullVertexCount; ++v)
    {
        double local[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            local[k] = (double)(view->hullVertices[3 * v + k] * body->size[k]);
        }
        Rotate(body->orientation, local, world[v]);
        for (int32_t k = 0; k < 3; ++k)
        {
            world[v][k] += (double)body->position[k];
        }
    }
    if (collider->kind == mvfx_colliderBox)
    {
        double lowest = (double)INFINITY;
        for (int32_t v = 0; v < view->hullVertexCount; ++v)
        {
            lowest = fmin(lowest, world[v][1]);
        }
        return lowest - (double)(collider->center[1] + collider->halfExtents[1]);
    }
    const double center[3] = {(double)collider->center[0], (double)collider->center[1],
                              (double)collider->center[2]};
    double nearest = (double)INFINITY;
    for (int32_t f = 0; f < view->hullFaceCount; ++f)
    {
        const int32_t first = view->hullFaceStart[f];
        for (int32_t j = first + 1; j + 1 < view->hullFaceStart[f + 1]; ++j)
        {
            nearest = fmin(nearest, TriangleDistance(center, world[view->hullLoop[first]],
                                                     world[view->hullLoop[j]],
                                                     world[view->hullLoop[j + 1]]));
        }
    }
    return sqrt(nearest) - (double)collider->radius;
}

// A hull of points, sized and turned, dropped from 0.6 onto a collider with
// gravity and stepped 5 s: whether it never sank more than GAP into it and
// ended within GAP of it.
static bool Drops(const float* points, int32_t count, const float size[3], const float turn[3],
                  const mvfxCollider* collider)
{
    mvfxContext* context = NULL;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = points;
    meshDef.vertexCount = count;
    mvfxMeshId mesh = {0};
    CHECK(mvfxCreateMesh(context, &meshDef, &mesh) == mvfx_success, "mesh");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 1;
    emitter.hostSpawnCapacity = 1;
    emitter.lifetime = mvfxMakeRangeSource(1000.0f, 1000.0f);
    emitter.motion.gravity[1] = -9.81f;
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                      .min = {size[0], size[1], size[2]},
                                      .max = {size[0], size[1], size[2]}};
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                             .min = {turn[0], turn[1], turn[2]},
                                             .max = {turn[0], turn[1], turn[2]}};
    emitter.rigid = (mvfxRigidDef){.enable = true,
                                   .shape = mvfx_rigidHull,
                                   .density = 1.0f,
                                   .substeps = 8,
                                   .staticFriction = 0.6f,
                                   .dynamicFriction = 0.4f,
                                   .hull = mesh};
    emitter.collision.enable = true;
    emitter.colliders = collider;
    emitter.colliderCount = 1;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxEmitterView view;
    CHECK(mvfxEffect_GetEmitterView(context, effect, 0, &view) == mvfx_success, "view");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    const double place[3] = {0.0, 0.6, 0.0};
    const float still[3] = {0.0f, 0.0f, 0.0f};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = still};
    CHECK(mvfxInstance_Spawn(context, instance, 0, &request) == mvfx_success, "spawned");
    double deepest = 0.0;
    double gap = (double)INFINITY;
    for (int32_t i = 0; i < 300; ++i)
    {
        CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success, "step");
        TestParticle body;
        CHECK(ReadParticles(context, instance, 0, &body, 1) == 1, "a body");
        gap = Gap(&view, &body, collider);
        deepest = fmin(deepest, gap);
    }
    mvfxDestroyContext(context);
    return deepest >= -GAP && fabs(gap) <= GAP;
}

static void TestOnSpheres(void)
{
    // A ball's hull and a turned cube's on spheres of radius 8 and 16,
    // their tops at the origin: each rolls but stays on.
    static const float unit[3] = {1.0f, 1.0f, 1.0f};
    static const float turn[3] = {0.2f, 0.3f, 0.1f};
    bool held = true;
    for (int32_t radius = 8; radius <= 16; radius *= 2)
    {
        const mvfxCollider sphere = {.kind = mvfx_colliderSphere,
                                     .center = {0.0f, -(float)radius, 0.0f},
                                     .radius = (float)radius};
        held = held && Drops(s_ball, 60, unit, turn, &sphere);
        held = held && Drops(s_cube, 8, unit, turn, &sphere);
    }
    CHECK(held, "on spheres by the distance to their centers");
}

static void TestOnATable(void)
{
    // A cube's hull turned onto its edge, a ball's hull, and each scaled to
    // 1.5 by 0.7 by 1 (a hull's planes scale by the inverse), dropped onto
    // a box whose top is at the origin.
    static const mvfxCollider table = {.kind = mvfx_colliderBox,
                                       .center = {0.0f, -0.5f, 0.0f},
                                       .halfExtents = {4.0f, 0.5f, 4.0f},
                                       .rotation = {0.0f, 0.0f, 0.0f, 1.0f}};
    static const float unit[3] = {1.0f, 1.0f, 1.0f};
    static const float uneven[3] = {1.5f, 0.7f, 1.0f};
    static const float edge[3] = {0.2f, 0.3f, 0.785398f};
    static const float turn[3] = {0.2f, 0.3f, 0.1f};
    bool held = Drops(s_cube, 8, unit, edge, &table) && Drops(s_ball, 60, unit, turn, &table) &&
                Drops(s_cube, 8, uneven, turn, &table) && Drops(s_ball, 60, uneven, turn, &table);
    CHECK(held, "on a table by its top");
}

int main(void)
{
    MakeBall();
    TestOnSpheres();
    TestOnATable();
    return s_failures == 0 ? 0 : 1;
}
