// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid hull particles: a cube's hull scaled to a box's size falling and
// toppling as that box does; a tetrahedron off its mesh's origin, its
// hull's center given, coming to rest on a face at that face's distance;
// a lopsided hull spun freely keeping its angular momentum and energy,
// its inertia not along its axes; hulls resting on a box collider and a
// sphere's top as boxes do; hulls stacked, meeting head on, and piled in
// a box without deep overlaps; a hull tipped through 45 degrees, where
// its contacts change from faces to a point, following a box; a box's and
// a hull's edge across a heightfield's crest held up by it; a mesh's
// hull counted past the most; the checks. Bodies are read from their
// records, their spins through a host module after motion, and a hull's
// shape from its emitter's view.

#include "particles.h"
#include "peek.h"
#include "test_harness.h"

#include "maul-vfx/grid.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <string.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxEffectId effect;
    mvfxInstanceId instance;
} Scene;

static Scene s_scene;

static const mvfxCollider s_ground = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};

static const float s_cube[24] = {-0.5f, -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,
                                 -0.5f, 0.5f,  0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,  0.5f,
                                 -0.5f, 0.5f,  -0.5f, 0.5f,  0.5f,  0.5f,  0.5f,  0.5f};

// The corner tetrahedron of edge 1 from (1, 2, 3).
static const float s_corner[12] = {1, 2, 3, 2, 2, 3, 1, 3, 3, 1, 2, 4};

// A lopsided hull: a wedge with a spike.
static const float s_lopsided[21] = {0,    0, 0,    1, 0, 0,    0, 0.6f, 0,    0,   0,
                                     0.3f, 1, 0.1f, 0, 0, 0.4f, 0, 0.7f, 0.8f, 1.1f};

static mvfxMeshId MeshOf(const float* positions, int32_t count)
{
    mvfxMeshDef def = mvfxDefaultMeshDef();
    def.positions = positions;
    def.vertexCount = count;
    mvfxMeshId mesh = {0};
    CHECK(mvfxCreateMesh(s_scene.context, &def, &mesh) == mvfx_success, "mesh");
    return mesh;
}

// A rigid emitter of a shape and size, eight substeps, on the ground
// plane with gravity; turned by Euler angles.
static mvfxEmitterDef Rigid(mvfxRigidShape shape, mvfxMeshId hull, const float size[3],
                            const float turn[3], bool gravity)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 4;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.motion.gravity[0] = 0.0f;
    emitter.motion.gravity[1] = gravity ? -9.81f : 0.0f;
    emitter.motion.gravity[2] = 0.0f;
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                      .min = {size[0], size[1], size[2]},
                                      .max = {size[0], size[1], size[2]}};
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                             .min = {turn[0], turn[1], turn[2]},
                                             .max = {turn[0], turn[1], turn[2]}};
    emitter.rigid = (mvfxRigidDef){.enabled = true,
                                   .shape = shape,
                                   .density = 1.0f,
                                   .substeps = 8,
                                   .staticFriction = 0.6f,
                                   .dynamicFriction = 0.4f,
                                   .hull = hull};
    emitter.collision.enabled = gravity;
    emitter.colliders = gravity ? &s_ground : NULL;
    emitter.colliderCount = gravity ? 1 : 0;
    return emitter;
}

static void OpenContext(void)
{
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
}

static mvfxResult EffectOf(const mvfxEmitterDef* emitters, int32_t count, mvfxEffectId* effect)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = count;
    return mvfxCreateEffect(s_scene.context, &def, effect);
}

// Plays emitters, one body each at place with velocity and spin.
static void Play(const mvfxEmitterDef* emitters, int32_t count, const double place[3],
                 const float velocity[3])
{
    mvfxEffectId effect = {0};
    CHECK(EffectOf(emitters, count, &effect) == mvfx_success, "effect");
    s_scene.effect = effect;
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    for (int32_t e = 0; e < count; ++e)
    {
        mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = velocity};
        CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, e, &request) == mvfx_success,
              "spawned");
    }
}

static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_scene.context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success,
              "step");
    }
}

// An emitter's live bodies' records, at most 8.
static int32_t Bodies(int32_t emitter, TestParticle out[8])
{
    int32_t count = ReadParticles(s_scene.context, s_scene.instance, emitter, out, 8);
    CHECK(count >= 1, "a body");
    return count;
}

// What At reads of a body.
typedef enum Field
{
    PositionX,
    PositionY,
    PositionZ,
    VelocityX,
    VelocityY,
    VelocityZ,
    OrientationX,
    OrientationY,
    OrientationZ,
    OrientationW,
    AngularX,
    AngularY,
    AngularZ,
} Field;

// An emitter's first body's field: its place, velocity and orientation
// from its record, its spin from the peeking module's (the peeking
// emitter's alone).
static float At(int32_t emitter, Field field)
{
    TestParticle bodies[8];
    if (field >= AngularX)
    {
        return s_peeked.angular[field - AngularX][0];
    }
    if (Bodies(emitter, bodies) < 1)
    {
        return 0.0f;
    }
    if (field <= PositionZ)
    {
        return bodies[0].position[field - PositionX];
    }
    if (field <= VelocityZ)
    {
        return bodies[0].velocity[field - VelocityX];
    }
    return bodies[0].orientation[field - OrientationX];
}

// An emitter of the scene's effect as compiled.
static mvfxEmitterView s_view;
static void View(int32_t emitter)
{
    CHECK(mvfxEffect_GetEmitterView(s_scene.context, s_scene.effect, emitter, &s_view) ==
              mvfx_success,
          "view");
}

static void TestLikeABox(void)
{
    // A box 1.2 by 0.6 by 0.9 and a cube's hull scaled to it, each turned
    // and dropped from 1.5: after 4 s at the same pose.
    OpenContext();
    mvfxMeshId cube = MeshOf(s_cube, 8);
    static const float size[3] = {1.2f, 0.6f, 0.9f};
    static const float turn[3] = {0.3f, 0.5f, 0.6f};
    const mvfxEmitterDef emitters[2] = {Rigid(mvfx_rigidBox, (mvfxMeshId){0}, size, turn, true),
                                        Rigid(mvfx_rigidHull, cube, size, turn, true)};
    static const double place[3] = {0, 1.5, 0};
    static const float still[3] = {0, 0, 0};
    Play(emitters, 2, place, still);
    Steps(240);
    float apart = 0.0f;
    for (int32_t k = 0; k < 7; ++k)
    {
        Field a = (Field)(k < 3 ? PositionX + k : OrientationX + k - 3);
        apart = fmaxf(apart, fabsf(At(0, a) - At(1, a)));
    }
    CHECK(apart <= 1e-3f && fabsf(At(1, PositionY) - 0.3f) <= 0.01f, "a hull as its box");
    mvfxDestroyContext(s_scene.context);
}

static void TestOffCenter(void)
{
    // The corner tetrahedron's hull is about its centroid (1.25, 2.25,
    // 3.25); turned and dropped it comes to rest on a face, its centroid
    // as high as that face's distance from it: a quarter of the height of
    // the corner over each right face, and a twelfth of root 3 over the
    // slanted face.
    OpenContext();
    mvfxMeshId corner = MeshOf(s_corner, 4);
    float center[3];
    bool hull = false;
    CHECK(mvfxMesh_HullCenter(s_scene.context, corner, &hull, center) == mvfx_success && hull &&
              fabsf(center[0] - 1.25f) <= 1e-5f && fabsf(center[1] - 2.25f) <= 1e-5f &&
              fabsf(center[2] - 3.25f) <= 1e-5f,
          "the hull's center");
    static const float unit[3] = {1, 1, 1};
    static const float turn[3] = {0.4f, 1.1f, 0.2f};
    const mvfxEmitterDef emitter = Rigid(mvfx_rigidHull, corner, unit, turn, true);
    static const double place[3] = {0, 1.0, 0};
    static const float still[3] = {0, 0, 0};
    Play(&emitter, 1, place, still);
    Steps(300);
    float y = At(0, PositionY);
    float speed = sqrtf(At(0, VelocityX) * At(0, VelocityX) + At(0, VelocityY) * At(0, VelocityY) +
                        At(0, VelocityZ) * At(0, VelocityZ));
    bool onFace = fabsf(y - 0.25f) <= 0.005f || fabsf(y - 0.14433757f) <= 0.005f;
    CHECK(onFace && speed <= 0.01f, "resting on a face");
    mvfxDestroyContext(s_scene.context);
}

// The world's angular momentum of a body of inertia (xx, yy, zz, xy, xz,
// yz) about its axes at orientation q spinning at w, and its energy.
static void MomentumOf(const float inertia[6], const float q[4], const float w[3],
                       float momentum[3], float* energy)
{
    // w in the body's frame.
    float x = q[0];
    float y = q[1];
    float z = q[2];
    float s = q[3];
    float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - s * z), 2 * (x * z + s * y)},
                     {2 * (x * y + s * z), 1 - 2 * (x * x + z * z), 2 * (y * z - s * x)},
                     {2 * (x * z - s * y), 2 * (y * z + s * x), 1 - 2 * (x * x + y * y)}};
    float local[3];
    for (int32_t i = 0; i < 3; ++i)
    {
        local[i] = r[0][i] * w[0] + r[1][i] * w[1] + r[2][i] * w[2];
    }
    const float* m = inertia;
    float l[3] = {m[0] * local[0] + m[3] * local[1] + m[4] * local[2],
                  m[3] * local[0] + m[1] * local[1] + m[5] * local[2],
                  m[4] * local[0] + m[5] * local[1] + m[2] * local[2]};
    for (int32_t i = 0; i < 3; ++i)
    {
        momentum[i] = r[i][0] * l[0] + r[i][1] * l[1] + r[i][2] * l[2];
    }
    *energy = 0.5f * (l[0] * local[0] + l[1] * local[1] + l[2] * local[2]);
}

static void State(const float inertia[6], float momentum[3], float* energy)
{
    const float q[4] = {At(0, OrientationX), At(0, OrientationY), At(0, OrientationZ),
                        At(0, OrientationW)};
    const float w[3] = {At(0, AngularX), At(0, AngularY), At(0, AngularZ)};
    MomentumOf(inertia, q, w, momentum, energy);
}

static void TestFreeSpin(void)
{
    // The lopsided hull spun at (2, 3, -1) without gravity: its angular
    // momentum in the world and its energy held over 2 s while it tumbles.
    OpenContext();
    mvfxMeshId lopsided = MeshOf(s_lopsided, 7);
    static const float unit[3] = {1, 1, 1};
    static const float turn[3] = {0, 0, 0};
    mvfxEmitterDef emitter = Rigid(mvfx_rigidHull, lopsided, unit, turn, false);
    emitter.angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {2, 3, -1}, .max = {2, 3, -1}};
    emitter = Peeking(&emitter);
    static const double place[3] = {0, 0, 0};
    static const float still[3] = {0, 0, 0};
    Play(&emitter, 1, place, still);
    View(0);
    const float* c = s_view.hullMoments;
    const float inertia[6] = {c[1] + c[2], c[0] + c[2], c[0] + c[1], -c[3], -c[4], -c[5]};
    CHECK(fabsf(c[3]) + fabsf(c[4]) + fabsf(c[5]) > 1e-3f, "inertia across its axes");
    Steps(1);
    float before[3];
    float energy = 0.0f;
    State(inertia, before, &energy);
    Steps(120);
    float after[3];
    float later = 0.0f;
    State(inertia, after, &later);
    float size = sqrtf(before[0] * before[0] + before[1] * before[1] + before[2] * before[2]);
    float drift = sqrtf((after[0] - before[0]) * (after[0] - before[0]) +
                        (after[1] - before[1]) * (after[1] - before[1]) +
                        (after[2] - before[2]) * (after[2] - before[2]));
    CHECK(drift <= 0.01f * size && fabsf(later - energy) <= 0.01f * energy,
          "momentum and energy held");
    mvfxDestroyContext(s_scene.context);
}

// Plays emitters at places (one body each), each with its colliders.
static void PlayAt(const mvfxEmitterDef* emitters, int32_t count, const double* places,
                   const float* velocities, int32_t bodies)
{
    mvfxEffectId effect = {0};
    CHECK(EffectOf(emitters, count, &effect) == mvfx_success, "effect");
    s_scene.effect = effect;
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    for (int32_t e = 0; e < count; ++e)
    {
        mvfxSpawnRequest request = {.count = bodies, .positions = places, .velocities = velocities};
        CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, e, &request) == mvfx_success,
              "spawned");
    }
}

static void TestOnColliders(void)
{
    // A cube's hull and a box, each dropped turned onto a box collider (a
    // table), and square onto a big sphere's top: at rest where the box
    // rests (turned, both roll off the sphere alike).
    static const mvfxCollider table = {.kind = mvfx_colliderBox,
                                       .center = {0, -0.5f, 0},
                                       .halfExtents = {2, 0.5f, 2},
                                       .rotation = {0, 0, 0, 1}};
    static const mvfxCollider ball = {
        .kind = mvfx_colliderSphere, .center = {0, -3, 0}, .radius = 3.0f};
    static const float unit[3] = {1, 1, 1};
    static const float turn[3] = {0.2f, 0.3f, 0.1f};
    static const double place[3] = {0, 1.2, 0};
    static const float still[3] = {0, 0, 0};
    for (int32_t on = 0; on < 2; ++on)
    {
        OpenContext();
        mvfxMeshId cube = MeshOf(s_cube, 8);
        static const float square[3] = {0, 0, 0};
        const float* tilt = on == 0 ? turn : square;
        mvfxEmitterDef emitters[2] = {Rigid(mvfx_rigidBox, (mvfxMeshId){0}, unit, tilt, true),
                                      Rigid(mvfx_rigidHull, cube, unit, tilt, true)};
        for (int32_t e = 0; e < 2; ++e)
        {
            emitters[e].colliders = on == 0 ? &table : &ball;
        }
        PlayAt(emitters, 2, place, still, 1);
        Steps(180);
        float speed = fabsf(At(1, VelocityX)) + fabsf(At(1, VelocityY)) + fabsf(At(1, VelocityZ));
        CHECK(fabsf(At(0, PositionY) - At(1, PositionY)) <= 0.005f && At(1, PositionY) >= 0.45f &&
                  speed <= 0.02f,
              on == 0 ? "on a table as a box" : "on a ball as a box");
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestMeeting(void)
{
    // Three cube hulls stacked on the ground stay stacked and still; two
    // meeting head on at restitution 1 trade their speeds.
    static const float unit[3] = {1, 1, 1};
    static const float turn[3] = {0, 0, 0};
    OpenContext();
    mvfxMeshId cube = MeshOf(s_cube, 8);
    mvfxEmitterDef stack = Rigid(mvfx_rigidHull, cube, unit, turn, true);
    stack.rigid.bodyContacts = 8;
    static const double stacked[9] = {0, 0.5, 0, 0, 1.501, 0, 0, 2.502, 0};
    PlayAt(&stack, 1, stacked, NULL, 3);
    Steps(180);
    TestParticle bodies[8];
    bool still = Bodies(0, bodies) == 3;
    for (int32_t i = 0; still && i < 3; ++i)
    {
        still = fabsf(bodies[i].position[1] - (0.5f + (float)bodies[i].serial)) <= 0.02f &&
                fabsf(bodies[i].position[0]) <= 0.01f;
    }
    CHECK(still, "a stack of hulls");
    mvfxDestroyContext(s_scene.context);
    OpenContext();
    cube = MeshOf(s_cube, 8);
    mvfxEmitterDef pair = Rigid(mvfx_rigidHull, cube, unit, turn, false);
    pair.rigid.bodyContacts = 8;
    pair.rigid.restitution = 1.0f;
    pair.collision.enabled = true;
    static const double apart[6] = {-1.5, 0, 0, 0, 0, 0};
    static const float toward[6] = {2, 0, 0, 0, 0, 0};
    PlayAt(&pair, 1, apart, toward, 2);
    Steps(60);
    CHECK(Bodies(0, bodies) == 2, "two bodies");
    int32_t first = bodies[0].serial == 0 ? 0 : 1;
    CHECK(fabsf(bodies[first].velocity[0]) <= 0.02f &&
              fabsf(bodies[1 - first].velocity[0] - 2.0f) <= 0.02f,
          "hulls trade speeds");
    mvfxDestroyContext(s_scene.context);
}

// A hull body in the world: its vertices and edges, binary64.
typedef struct Solid
{
    int32_t vertexCount;
    double vertices[64][3];
    int32_t edgeCount;
    double edges[192][3];
    int32_t faceCount;
    double normals[64][3];
} Solid;

// A quaternion (x, y, z, w) turning a vector.
static void Turn(const float q[4], const double v[3], double out[3])
{
    double x = (double)q[0];
    double y = (double)q[1];
    double z = (double)q[2];
    double w = (double)q[3];
    double t[3] = {2.0 * (y * v[2] - z * v[1]), 2.0 * (z * v[0] - x * v[2]),
                   2.0 * (x * v[1] - y * v[0])};
    out[0] = v[0] + w * t[0] + (y * t[2] - z * t[1]);
    out[1] = v[1] + w * t[1] + (z * t[0] - x * t[2]);
    out[2] = v[2] + w * t[2] + (x * t[1] - y * t[0]);
}

// The viewed hull scaled by size (the same along each axis), turned by q
// and placed at x: its vertices, its faces' normals and its faces' edges.
static void Place(const float size[3], const float x[3], const float q[4], Solid* out)
{
    const mvfxEmitterView* v = &s_view;
    CHECK(v->hullVertexCount <= 64 && v->hullFaceCount <= 64 && size[0] == size[1] &&
              size[1] == size[2],
          "a small uniform hull");
    out->vertexCount = v->hullVertexCount;
    for (int32_t i = 0; i < v->hullVertexCount; ++i)
    {
        double local[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            local[k] = (double)size[k] * (double)v->hullVertices[3 * i + k];
        }
        Turn(q, local, out->vertices[i]);
        for (int32_t k = 0; k < 3; ++k)
        {
            out->vertices[i][k] += (double)x[k];
        }
    }
    out->faceCount = v->hullFaceCount;
    out->edgeCount = 0;
    for (int32_t f = 0; f < v->hullFaceCount; ++f)
    {
        const double normal[3] = {(double)v->hullPlanes[4 * f], (double)v->hullPlanes[4 * f + 1],
                                  (double)v->hullPlanes[4 * f + 2]};
        Turn(q, normal, out->normals[f]);
        int32_t first = v->hullFaceStart[f];
        int32_t end = v->hullFaceStart[f + 1];
        for (int32_t c = first; c < end && out->edgeCount < 192; ++c)
        {
            const double* a = out->vertices[v->hullLoop[c]];
            const double* b = out->vertices[v->hullLoop[c + 1 < end ? c + 1 : first]];
            for (int32_t k = 0; k < 3; ++k)
            {
                out->edges[out->edgeCount][k] = b[k] - a[k];
            }
            out->edgeCount += 1;
        }
    }
}

// The gap between two solids along a unit axis: the larger of each one's
// least projection less the other's largest.
static double GapAlong(const Solid* a, const Solid* b, const double axis[3])
{
    double low[2] = {(double)INFINITY, (double)INFINITY};
    double high[2] = {-(double)INFINITY, -(double)INFINITY};
    const Solid* solids[2] = {a, b};
    for (int32_t s = 0; s < 2; ++s)
    {
        for (int32_t i = 0; i < solids[s]->vertexCount; ++i)
        {
            const double* p = solids[s]->vertices[i];
            double d = p[0] * axis[0] + p[1] * axis[1] + p[2] * axis[2];
            low[s] = fmin(low[s], d);
            high[s] = fmax(high[s], d);
        }
    }
    return fmax(low[1] - high[0], low[0] - high[1]);
}

// How far apart two convex solids are along their separating axes (each
// face's normal, each pair of edges' cross): above 0 apart, below 0 by
// how deep they overlap.
static double Apart(const Solid* a, const Solid* b)
{
    double apart = -(double)INFINITY;
    const Solid* solids[2] = {a, b};
    for (int32_t s = 0; s < 2; ++s)
    {
        for (int32_t f = 0; f < solids[s]->faceCount; ++f)
        {
            apart = fmax(apart, GapAlong(a, b, solids[s]->normals[f]));
        }
    }
    for (int32_t i = 0; i < a->edgeCount; ++i)
    {
        for (int32_t j = 0; j < b->edgeCount; ++j)
        {
            const double* e = a->edges[i];
            const double* g = b->edges[j];
            double axis[3] = {e[1] * g[2] - e[2] * g[1], e[2] * g[0] - e[0] * g[2],
                              e[0] * g[1] - e[1] * g[0]};
            double length = sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
            if (length > 1e-9)
            {
                for (int32_t k = 0; k < 3; ++k)
                {
                    axis[k] /= length;
                }
                apart = fmax(apart, GapAlong(a, b, axis));
            }
        }
    }
    return apart;
}

static void TestPile(void)
{
    // Six lopsided hulls dropped into a box settle in it, still, none
    // overlapping another by more than 0.01 (by separating axes over the
    // hull the effect compiled), no search stopped at its steps (GJK
    // cycled between faces about as near, 226 times, until it stopped
    // where it came no nearer).
    OpenContext();
    mvfxMeshId lopsided = MeshOf(s_lopsided, 7);
    static const float size[3] = {0.5f, 0.5f, 0.5f};
    static const float turn[3] = {0.7f, 0.4f, 1.3f};
    static const mvfxCollider box = {.kind = mvfx_colliderBox,
                                     .center = {0, 2, 0},
                                     .halfExtents = {0.6f, 2, 0.6f},
                                     .rotation = {0, 0, 0, 1},
                                     .inside = true};
    mvfxEmitterDef pile = Rigid(mvfx_rigidHull, lopsided, size, turn, true);
    pile.capacity = 8;
    pile.hostSpawnCapacity = 8;
    pile.rigid.bodyContacts = 8;
    pile.colliders = &box;
    double places[18];
    for (int32_t i = 0; i < 6; ++i)
    {
        places[3 * i] = 0.06 * (double)(i % 3) - 0.06;
        places[3 * i + 1] = 0.4 + 0.6 * (double)i;
        places[3 * i + 2] = 0.05 * (double)(i % 2) - 0.025;
    }
    PlayAt(&pile, 1, places, NULL, 6);
    Steps(300);
    View(0);
    TestParticle bodies[8];
    int32_t count = Bodies(0, bodies);
    static Solid solids[6];
    float fastest = 0.0f;
    for (int32_t i = 0; i < 6 && i < count; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            fastest = fmaxf(fastest, fabsf(bodies[i].velocity[k]));
        }
        Place(size, bodies[i].position, bodies[i].orientation, &solids[i]);
    }
    double deepest = 0.0;
    for (int32_t i = 0; i < 6 && i < count; ++i)
    {
        for (int32_t j = i + 1; j < 6 && j < count; ++j)
        {
            deepest = fmin(deepest, Apart(&solids[i], &solids[j]));
        }
    }
    mvfxEmitterStats stats;
    CHECK(mvfxInstance_GetEmitterStats(s_scene.context, s_scene.instance, 0, &stats) ==
              mvfx_success,
          "stats");
    CHECK(count == 6 && deepest >= -0.01 && fastest <= 0.05f && stats.rigidSearchesCapped == 0,
          "a pile of hulls");
    mvfxDestroyContext(s_scene.context);
}

static void TestTipping(void)
{
    // A cube's hull and a box on a table, each balanced on an edge and
    // tipped 1 degree past it either way: as they fall through 45 degrees
    // the hull's contacts change from clipped faces to a point, and still
    // it falls as the box (met by its own clipping) does within 5 mm (its
    // pivot slides on its own contacts), its spin never turning back, and
    // rests on a face; landing, they stop within 2 cm of each other.
    // Static friction is 1: at 0.6 the slide a landing must hold is at
    // its limit, and whether a body stops or slides on hung on rounding
    // (a box and its mirror image ended 4 cm apart).
    static const mvfxCollider table = {.kind = mvfx_colliderBox,
                                       .center = {0, -0.5f, 0},
                                       .halfExtents = {3, 0.5f, 3},
                                       .rotation = {0, 0, 0, 1}};
    static const float unit[3] = {1, 1, 1};
    static const float still[3] = {0, 0, 0};
    float falling = 0.0f;
    float landed = 0.0f;
    bool steady = true;
    for (int32_t side = 0; side < 2; ++side)
    {
        float angle = (side == 0 ? 44.0f : 46.0f) * 0.017453293f;
        const float turn[3] = {0, 0, angle};
        OpenContext();
        mvfxMeshId cube = MeshOf(s_cube, 8);
        mvfxEmitterDef emitters[2] = {Rigid(mvfx_rigidBox, (mvfxMeshId){0}, unit, turn, true),
                                      Rigid(mvfx_rigidHull, cube, unit, turn, true)};
        emitters[0].colliders = &table;
        emitters[1].colliders = &table;
        emitters[0].rigid.staticFriction = 1.0f;
        emitters[1].rigid.staticFriction = 1.0f;
        emitters[1] = Peeking(&emitters[1]);
        // Its lowest edge on the table.
        const double place[3] = {0, 0.70710678 * cos((double)angle - 0.78539816), 0};
        PlayAt(emitters, 2, place, still, 1);
        float spun = 0.0f;
        for (int32_t i = 0; i < 240; ++i)
        {
            Steps(1);
            bool down = At(1, PositionY) <= 0.501f;
            for (int32_t k = 0; k < 3; ++k)
            {
                Field x = (Field)(PositionX + k);
                float d = fabsf(At(0, x) - At(1, x));
                falling = down ? falling : fmaxf(falling, d);
                landed = fmaxf(landed, d);
            }
            float w = At(1, AngularZ);
            steady = steady && (down || spun * w >= 0.0f);
            spun = fabsf(w) > 0.05f ? w : spun;
        }
        float speed = fabsf(At(1, VelocityX)) + fabsf(At(1, VelocityY)) + fabsf(At(1, AngularZ));
        steady = steady && fabsf(At(1, PositionY) - 0.5f) <= 0.002f && speed <= 0.01f;
        mvfxDestroyContext(s_scene.context);
    }
    CHECK(falling <= 0.005f && landed <= 0.02f && steady, "tipped through 45 degrees as a box");
}

// A unit box's or a cube's hull, turned an eighth about z, its lower edge
// along z laid across a ridge along x 0.3 high (0.5 wide at its foot),
// its corners 0.29 above the flat on either side, the crest under the
// edge's middle or a quarter along it: how far its center has fallen
// after steps of 1/60 s.
static float CrestFall(bool hull, float along, int32_t steps)
{
    OpenContext();
    static float heights[3 * 17];
    memset(heights, 0, sizeof heights);
    for (int32_t x = 0; x < 3; ++x)
    {
        heights[8 * 3 + x] = 0.3f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 3;
    def.size[2] = 17;
    def.values = heights;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(s_scene.context, &def, &grid) == mvfx_success, "ridge");
    const mvfxCollider ridge = {.kind = mvfx_colliderHeightfield,
                                .halfExtents = {2, 1, 2},
                                .rotation = {0, 0, 0, 1},
                                .grid = grid};
    static const float unit[3] = {1, 1, 1};
    static const float turn[3] = {0, 0, 0.78539816f};
    mvfxEmitterDef emitter = hull ? Rigid(mvfx_rigidHull, MeshOf(s_cube, 8), unit, turn, true)
                                  : Rigid(mvfx_rigidBox, (mvfxMeshId){0}, unit, turn, true);
    emitter.colliders = &ridge;
    const double place[3] = {0, 0.3 + 0.70710678 + 0.001, (double)along};
    PlayAt(&emitter, 1, place, NULL, 1);
    Steps(steps);
    float fall = (float)place[1] - At(0, PositionY);
    mvfxDestroyContext(s_scene.context);
    return fall;
}

static void TestCrest(void)
{
    // An edge across a crest rests on it, not left to sink until a corner
    // meets the flat: a box and a hull, the crest under the edge's middle
    // or a quarter along it, fall at most 0.02 in 0.1 s (0.007 seen; free,
    // 0.049).
    float worst = 0.0f;
    for (int32_t c = 0; c < 4; ++c)
    {
        worst = fmaxf(worst, CrestFall(c % 2 == 1, c < 2 ? 0.0f : 0.25f, 6));
    }
    CHECK(worst <= 0.02f, "an edge across a crest held up");
}

static void TestVertexCount(void)
{
    // A mesh of 100 points on a ball, refused as a hull, counted: 100 for
    // its artist to bring under the most; arguments refused.
    OpenContext();
    static float ball[300];
    for (int32_t i = 0; i < 100; ++i)
    {
        float z = 1.0f - (2.0f * (float)i + 1.0f) / 100.0f;
        float r = sqrtf(1.0f - z * z);
        float angle = 2.3999632f * (float)i;
        ball[3 * i] = r * cosf(angle);
        ball[3 * i + 1] = r * sinf(angle);
        ball[3 * i + 2] = z;
    }
    mvfxMeshId mesh = MeshOf(ball, 100);
    int32_t count = 0;
    float center[3] = {1, 1, 1};
    bool hull = true;
    uint64_t misuse = mvfxGetContextMisuse(s_scene.context);
    bool counted = mvfxMesh_HullVertexCount(s_scene.context, mesh, &count) == mvfx_success &&
                   count == 100 &&
                   mvfxMesh_HullCenter(s_scene.context, mesh, &hull, center) == mvfx_success &&
                   !hull && center[0] == 0.0f && center[1] == 0.0f && center[2] == 0.0f &&
                   mvfxGetContextMisuse(s_scene.context) == misuse;
    // A flat mesh has no hull: none of its vertices, no center, and no
    // misuse, since a model file's vertices may well be flat.
    static const float flat[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
    mvfxMeshId plane = MeshOf(flat, 4);
    count = 1;
    hull = true;
    bool none = mvfxMesh_HullVertexCount(s_scene.context, plane, &count) == mvfx_success &&
                count == 0 &&
                mvfxMesh_HullCenter(s_scene.context, plane, &hull, center) == mvfx_success &&
                !hull && mvfxGetContextMisuse(s_scene.context) == misuse;
    bool refused =
        mvfxMesh_HullVertexCount(s_scene.context, mesh, NULL) == mvfx_errorInvalid &&
        mvfxMesh_HullVertexCount(NULL, mesh, &count) == mvfx_errorInvalid && count == 0 &&
        mvfxMesh_HullVertexCount(s_scene.context, (mvfxMeshId){0}, &count) == mvfx_errorStale &&
        mvfxMesh_HullCenter(s_scene.context, mesh, NULL, center) == mvfx_errorInvalid &&
        mvfxMesh_HullCenter(s_scene.context, (mvfxMeshId){0}, &hull, center) == mvfx_errorStale &&
        mvfxGetContextMisuse(s_scene.context) == misuse + 2;
    CHECK(counted, "a hull's vertices counted past the most, with no center");
    CHECK(none, "a flat mesh: no hull, no misuse");
    CHECK(refused, "NULL arguments counted as misuse, a stale mesh not");
    mvfxDestroyContext(s_scene.context);
}

static void TestChecks(void)
{
    OpenContext();
    static const float unit[3] = {1, 1, 1};
    static const float turn[3] = {0, 0, 0};
    const float flat[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
    float ball[3 * 200];
    for (int32_t i = 0; i < 200; ++i)
    {
        float z = 1.0f - (2.0f * (float)i + 1.0f) / 200.0f;
        float r = sqrtf(1.0f - z * z);
        ball[3 * i] = r * cosf(2.3999632f * (float)i);
        ball[3 * i + 1] = r * sinf(2.3999632f * (float)i);
        ball[3 * i + 2] = z;
    }
    mvfxMeshId cube = MeshOf(s_cube, 8);
    mvfxMeshId gone = MeshOf(s_cube, 8);
    CHECK(mvfxDestroyMesh(s_scene.context, gone) == mvfx_success, "a mesh gone");
    mvfxMeshId shapes[3] = {MeshOf(flat, 4), MeshOf(ball, 200), gone};
    mvfxResult expected[3] = {mvfx_errorInvalid, mvfx_errorInvalid, mvfx_errorStale};
    mvfxResult centered[3] = {mvfx_success, mvfx_success, mvfx_errorStale};
    uint64_t misuse = mvfxGetContextMisuse(s_scene.context);
    int32_t refused = 0;
    for (int32_t i = 0; i < 3; ++i)
    {
        mvfxEmitterDef emitter = Rigid(mvfx_rigidHull, shapes[i], unit, turn, true);
        mvfxEffectId effect = {0};
        float center[3];
        bool hull = true;
        refused +=
            EffectOf(&emitter, 1, &effect) == expected[i] &&
                    mvfxMesh_HullCenter(s_scene.context, shapes[i], &hull, center) == centered[i] &&
                    !hull
                ? 1
                : 0;
    }
    CHECK(refused == 3, "the checks");
    CHECK(mvfxGetContextMisuse(s_scene.context) == misuse,
          "a mesh with no hull a particle takes is refused, not misuse");
    mvfxEmitterDef inside = Rigid(mvfx_rigidHull, cube, unit, turn, true);
    static const mvfxCollider room = {.kind = mvfx_colliderBox,
                                      .halfExtents = {3, 3, 3},
                                      .rotation = {0, 0, 0, 1},
                                      .inside = true};
    inside.colliders = &room;
    mvfxEffectId effect = {0};
    CHECK(EffectOf(&inside, 1, &effect) == mvfx_success, "inside a box");
    mvfxDestroyContext(s_scene.context);
}

int main(void)
{
    TestLikeABox();
    TestOffCenter();
    TestFreeSpin();
    TestOnColliders();
    TestMeeting();
    TestPile();
    TestTipping();
    TestCrest();
    TestVertexCount();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
