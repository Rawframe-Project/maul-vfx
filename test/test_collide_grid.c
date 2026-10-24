// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Collision with grids: a heightfield's slope met where it lies and its
// normal as computed by hand, nothing outside its footprint, its inside,
// a start below put on it, a radius; a distance grid's sphere met and
// bounced off, a start within put out, its inside kept; no particle
// through the sphere at high speed; a grid scaling with the instance; new
// values read by the next step; grids held by effects and instances; and
// the refusals.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"

#include <math.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static mvfxContext* NewContext(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    return context;
}

static Scene NewScene(mvfxContext* context, const mvfxEmitterDef* emitter)
{
    Scene scene = {.context = context};
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(context, &instanceDef, &scene.instance) == mvfx_success, "instance");
    return scene;
}

static mvfxEmitterDef Held(const mvfxCollider* colliders, int32_t count, int32_t capacity)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = capacity;
    emitter.hostSpawnCapacity = capacity;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitter.collision = (mvfxCollision){.enabled = true, .restitution = 1.0f};
    emitter.colliders = colliders;
    emitter.colliderCount = count;
    emitter.events = mvfx_eventMaskCollision;
    emitter.eventCapacity = capacity;
    return emitter;
}

static void Place(Scene* scene, double x, double y, double z, float vx, float vy, float vz)
{
    const double place[3] = {x, y, z};
    const float velocity[3] = {vx, vy, vz};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = velocity};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "placed");
}

static TestParticle s_particles[1000];

// Reads the particles into s_particles; returns how many.
static int32_t Read(const Scene* scene)
{
    return ReadParticles(scene->context, scene->instance, 0, s_particles, 1000);
}

// The particle at i.
static TestParticle At(const Scene* scene, int32_t i)
{
    CHECK(i < Read(scene), "the particle read");
    return s_particles[i];
}

// The collisions of the last step.
static int32_t Collisions(const Scene* scene)
{
    const mvfxEvent* events = NULL;
    int32_t count = -1;
    CHECK(mvfxInstance_GetEvents(scene->context, scene->instance, 0, &events, &count) ==
              mvfx_success,
          "events");
    return count;
}

static uint32_t s_random = 2026;

// A number in [0, 1).
static float Random(void)
{
    s_random = s_random * 1664525u + 1013904223u;
    return (float)(s_random >> 8u) * 0x1p-24f;
}

static bool Near(float a, float b, float within)
{
    return fabsf(a - b) <= within;
}

// A slope over X from -2 to 2, its heights half of x, three points a side.
static mvfxGridId Slope(mvfxContext* context)
{
    static const float heights[9] = {-1, 0, 1, -1, 0, 1, -1, 0, 1};
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 3;
    def.size[1] = 1;
    def.size[2] = 3;
    def.values = heights;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(context, &def, &grid) == mvfx_success, "slope");
    return grid;
}

// The distance to a sphere of radius 1, over a box of half size 2, 17
// points a side (a cell of a quarter).
static mvfxGridId Ball(mvfxContext* context)
{
    static float distances[17 * 17 * 17];
    for (int32_t z = 0; z < 17; ++z)
    {
        for (int32_t y = 0; y < 17; ++y)
        {
            for (int32_t x = 0; x < 17; ++x)
            {
                float p[3] = {(float)x * 0.25f - 2.0f, (float)y * 0.25f - 2.0f,
                              (float)z * 0.25f - 2.0f};
                distances[(z * 17 + y) * 17 + x] =
                    sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) - 1.0f;
            }
        }
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 17;
    def.size[1] = 17;
    def.size[2] = 17;
    def.values = distances;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(context, &def, &grid) == mvfx_success, "ball");
    return grid;
}

static mvfxCollider Heightfield(mvfxGridId grid)
{
    return (mvfxCollider){.kind = mvfx_colliderHeightfield,
                          .halfExtents = {2, 0, 2},
                          .rotation = {0, 0, 0, 1},
                          .grid = grid};
}

static mvfxCollider Distance(mvfxGridId grid)
{
    return (mvfxCollider){.kind = mvfx_colliderDistance,
                          .halfExtents = {2, 2, 2},
                          .rotation = {0, 0, 0, 1},
                          .grid = grid};
}

static void Step(Scene* scene)
{
    CHECK(mvfxContext_StepTicks(scene->context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
}

static void TestHeightfield(void)
{
    mvfxContext* context = NewContext();
    mvfxCollider slope = Heightfield(Slope(context));
    // Stopped where it lands: at x = 1 the slope is at 0.5.
    mvfxEmitterDef emitter = Held(&slope, 1, 8);
    emitter.collision.restitution = 0.0f;
    emitter.collision.friction = 1.0f;
    Scene scene = NewScene(context, &emitter);
    Place(&scene, 1, 5, 0, 0, -10, 0);
    // Outside its footprint it falls on.
    Place(&scene, 3, 5, 0, 0, -10, 0);
    // Below it at rest: put on it.
    Place(&scene, 1, -3, 1, 0, 0, 0);
    Step(&scene);
    CHECK(Near(At(&scene, 0).position[1], 0.5f, 2e-3f), "landed on the slope");
    CHECK(Near(At(&scene, 1).position[1], -5.0f, 1e-4f), "outside its footprint");
    CHECK(Near(At(&scene, 2).position[1], 0.5f, 1e-4f), "a start below put on it");
    mvfxDestroyContext(context);

    // Its normal, (-0.5, 1) made unit: a fall at 10 bounces to (-8, 6).
    context = NewContext();
    slope = Heightfield(Slope(context));
    emitter = Held(&slope, 1, 8);
    scene = NewScene(context, &emitter);
    Place(&scene, 1, 5, 0, 0, -10, 0);
    Step(&scene);
    CHECK(Near(At(&scene, 0).velocity[0], -8.0f, 1e-2f) &&
              Near(At(&scene, 0).velocity[1], 6.0f, 1e-2f),
          "bounced off the slope's normal");
    mvfxDestroyContext(context);

    // Inside, kept below it: rising at x = 0 it stops at 0; a radius of
    // 0.25 stops it a quarter lower.
    for (int32_t r = 0; r < 2; ++r)
    {
        context = NewContext();
        slope = Heightfield(Slope(context));
        slope.inside = true;
        emitter = Held(&slope, 1, 8);
        emitter.collision.restitution = 0.0f;
        emitter.collision.friction = 1.0f;
        emitter.collision.radius = r == 0 ? 0.0f : 0.25f;
        scene = NewScene(context, &emitter);
        Place(&scene, 0, -1, 0, 0, 10, 0);
        Step(&scene);
        CHECK(Near(At(&scene, 0).position[1], r == 0 ? 0.0f : -0.25f, 2e-3f),
              r == 0 ? "kept below" : "kept below by its radius");
        mvfxDestroyContext(context);
    }

    // As an instance's own collider: met the same.
    context = NewContext();
    slope = Heightfield(Slope(context));
    emitter = Held(NULL, 0, 8);
    emitter.collision.hostColliders = true;
    emitter.collision.restitution = 0.0f;
    emitter.collision.friction = 1.0f;
    scene = NewScene(context, &emitter);
    CHECK(mvfxInstance_SetColliders(context, scene.instance, &slope, 1) == mvfx_success, "set");
    Place(&scene, 1, 5, 0, 0, -10, 0);
    Step(&scene);
    CHECK(Near(At(&scene, 0).position[1], 0.5f, 2e-3f), "the instance's heightfield");
    mvfxDestroyContext(context);

    // Inside, a start above it is put down onto it.
    context = NewContext();
    slope = Heightfield(Slope(context));
    slope.inside = true;
    emitter = Held(&slope, 1, 8);
    scene = NewScene(context, &emitter);
    Place(&scene, 1, 3, 0, 0, 0, 0);
    Step(&scene);
    CHECK(Near(At(&scene, 0).position[1], 0.5f, 1e-4f), "a start above put under");
    mvfxDestroyContext(context);

    // Under an instance at scale 2 the slope spans x from -4 to 4 and its
    // heights double: at x = 3 it lies at 1.5.
    context = NewContext();
    slope = Heightfield(Slope(context));
    slope.space = mvfx_spaceLocal;
    emitter = Held(&slope, 1, 8);
    emitter.collision.restitution = 0.0f;
    emitter.collision.friction = 1.0f;
    scene = NewScene(context, &emitter);
    static const double origin[3] = {0, 0, 0};
    static const float still[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(context, scene.instance, origin, still, 2.0f, true) ==
              mvfx_success,
          "scaled");
    Place(&scene, 3, 5, 0, 0, -10, 0);
    Step(&scene);
    CHECK(Near(At(&scene, 0).position[1], 1.5f, 4e-3f), "scaled with the instance");
    mvfxDestroyContext(context);

    // New heights, read by the next step: a particle at rest on the slope
    // is lifted onto a flat 2.
    context = NewContext();
    mvfxGridId grid = Slope(context);
    slope = Heightfield(grid);
    emitter = Held(&slope, 1, 8);
    scene = NewScene(context, &emitter);
    Place(&scene, 1, 0.75, 0, 0, 0, 0);
    Step(&scene);
    CHECK(Near(At(&scene, 0).position[1], 0.75f, 1e-6f), "above it, untouched");
    static const float flat[9] = {2, 2, 2, 2, 2, 2, 2, 2, 2};
    CHECK(mvfxGrid_SetValues(context, grid, flat, 9) == mvfx_success, "new heights");
    Step(&scene);
    CHECK(Near(At(&scene, 0).position[1], 2.0f, 1e-4f), "lifted onto the new heights");
    mvfxDestroyContext(context);
}

static void TestDistance(void)
{
    mvfxContext* context = NewContext();
    mvfxCollider ball = Distance(Ball(context));
    // From y = 3 down at 10: met at y = 1 a fifth of the way, back up the
    // rest, to y = 9 at 10.
    mvfxEmitterDef emitter = Held(&ball, 1, 1000);
    Scene scene = NewScene(context, &emitter);
    Place(&scene, 0, 3, 0, 0, -10, 0);
    // Started within the ball: put out onto it.
    Place(&scene, 0.3, 0.2, 0.1, 0, 0, 0);
    Step(&scene);
    CHECK(Near(At(&scene, 0).position[1], 9.0f, 2e-2f) &&
              Near(At(&scene, 0).velocity[1], 10.0f, 1e-2f),
          "bounced off the ball");
    float p[3] = {At(&scene, 1).position[0], At(&scene, 1).position[1], At(&scene, 1).position[2]};
    CHECK(Near(sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]), 1.0f, 2e-2f), "put out onto it");
    mvfxDestroyContext(context);

    // A thousand particles about it at up to 1,000 a second, three steps:
    // none ends within it.
    context = NewContext();
    ball = Distance(Ball(context));
    emitter = Held(&ball, 1, 1000);
    scene = NewScene(context, &emitter);
    double places[1000][3];
    float velocities[1000][3];
    for (uint32_t i = 0; i < 1000; ++i)
    {
        float q[3];
        do
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                q[k] = Random() * 4.0f - 2.0f;
            }
        } while (sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]) < 1.05f);
        float speed = powf(10.0f, Random() * 3.0f);
        for (int32_t k = 0; k < 3; ++k)
        {
            places[i][k] = (double)q[k];
            velocities[i][k] = speed * (Random() * 2.0f - 1.0f);
        }
    }
    mvfxSpawnRequest request = {
        .count = 1000, .positions = &places[0][0], .velocities = &velocities[0][0]};
    CHECK(mvfxInstance_Spawn(context, scene.instance, 0, &request) == mvfx_success, "placed");
    bool kept = true;
    int32_t count = 0;
    for (int32_t step = 0; step < 3; ++step)
    {
        CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 3) == mvfx_success, "step");
        count = Read(&scene);
        for (int32_t i = 0; i < count; ++i)
        {
            const float* q = s_particles[i].position;
            kept = kept && sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]) >= 0.98f;
        }
    }
    CHECK(kept && count == 1000, "none through the ball");
    mvfxDestroyContext(context);

    // Inside, kept within it: from its center along X at 10, never past it.
    context = NewContext();
    ball = Distance(Ball(context));
    ball.inside = true;
    emitter = Held(&ball, 1, 8);
    scene = NewScene(context, &emitter);
    Place(&scene, 0, 0, 0, 10, 0, 0);
    Step(&scene);
    float x = At(&scene, 0).position[0];
    CHECK(fabsf(x) <= 1.0f + 1e-3f && Collisions(&scene) == 1, "kept within the ball");
    mvfxDestroyContext(context);
}

static void TestThinWall(void)
{
    // A wall half a unit thick at x = 1, in a grid fine along X (a cell of
    // a quarter) and coarse along Y and Z (one of 4): sampled by the fine
    // cell, a crossing at 3.8 a second is met at its face.
    mvfxContext* context = NewContext();
    float distances[17 * 2 * 2];
    for (int32_t k = 0; k < 17 * 2 * 2; ++k)
    {
        float x = (float)(k % 17) * 0.25f - 2.0f;
        distances[k] = fabsf(x - 1.0f) - 0.25f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 17;
    def.size[1] = 2;
    def.size[2] = 2;
    def.values = distances;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(context, &def, &grid) == mvfx_success, "wall");
    mvfxCollider wall = Distance(grid);
    mvfxEmitterDef emitter = Held(&wall, 1, 8);
    emitter.collision.restitution = 0.0f;
    emitter.collision.friction = 1.0f;
    Scene scene = NewScene(context, &emitter);
    Place(&scene, -1.9, 0, 0, 3.8f, 0, 0);
    Step(&scene);
    CHECK(Near(At(&scene, 0).position[0], 0.75f, 2e-3f), "met at the wall's face");
    mvfxDestroyContext(context);
}

static void TestHeldAndRefused(void)
{
    mvfxContext* context = NewContext();
    mvfxGridId slopeGrid = Slope(context);
    mvfxGridId ballGrid = Ball(context);
    mvfxCollider slope = Heightfield(slopeGrid);
    mvfxEmitterDef emitter = Held(&slope, 1, 8);
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    CHECK(mvfxDestroyGrid(context, slopeGrid) == mvfx_errorBusy, "held by the effect");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_success, "effect gone");
    CHECK(mvfxDestroyGrid(context, slopeGrid) == mvfx_success, "let go");

    // Stale, of three components, a heightfield two points tall.
    uint64_t misuse = mvfxGetContextMisuse(context);
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_errorStale, "a stale grid");
    static const float vectors[6] = {0};
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 2;
    def.components = 3;
    def.values = vectors;
    mvfxGridId three = {0};
    CHECK(mvfxCreateGrid(context, &def, &three) == mvfx_success, "three components");
    slope.grid = three;
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_errorInvalid, "three refused");
    def.size[0] = 1;
    def.size[1] = 2;
    def.components = 1;
    mvfxGridId tall = {0};
    CHECK(mvfxCreateGrid(context, &def, &tall) == mvfx_success, "two tall");
    slope.grid = tall;
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_errorInvalid, "tall refused");
    CHECK(mvfxGetContextMisuse(context) == misuse + 2, "each counted");
    // A distance grid may be two tall, and a heightfield need not set its Y.
    mvfxCollider tallDistance = Distance(tall);
    emitter.colliders = &tallDistance;
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "a distance grid");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_success, "gone");
    tallDistance.halfExtents[1] = 0.0f;
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_errorInvalid,
          "a distance grid reads its Y");

    // Held by an instance's own colliders: busy until replaced, cleared or
    // the instance is gone.
    emitter = Held(NULL, 0, 8);
    emitter.collision.hostColliders = true;
    Scene scene = NewScene(context, &emitter);
    mvfxCollider own = Distance(ballGrid);
    CHECK(mvfxInstance_SetColliders(context, scene.instance, &own, 1) == mvfx_success, "set");
    CHECK(mvfxDestroyGrid(context, ballGrid) == mvfx_errorBusy, "held by the instance");
    CHECK(mvfxInstance_SetColliders(context, scene.instance, &own, 1) == mvfx_success, "again");
    CHECK(mvfxInstance_SetColliders(context, scene.instance, NULL, 0) == mvfx_success, "cleared");
    CHECK(mvfxDestroyGrid(context, tall) == mvfx_success, "an unheld grid goes");
    CHECK(mvfxInstance_SetColliders(context, scene.instance, &own, 1) == mvfx_success, "set");
    own.grid = three;
    CHECK(mvfxInstance_SetColliders(context, scene.instance, &own, 1) == mvfx_errorInvalid,
          "three refused, the last kept");
    own.grid = (mvfxGridId){0};
    CHECK(mvfxInstance_SetColliders(context, scene.instance, &own, 1) == mvfx_errorStale,
          "a stale grid");
    CHECK(mvfxDestroyGrid(context, ballGrid) == mvfx_errorBusy, "still held");
    CHECK(mvfxDestroyInstance(context, scene.instance) == mvfx_success, "instance gone");
    CHECK(mvfxDestroyGrid(context, ballGrid) == mvfx_success, "let go");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestHeightfield();
    TestDistance();
    TestThinWall();
    TestHeldAndRefused();
    return s_failures == 0 ? 0 : 1;
}
