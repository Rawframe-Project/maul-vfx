// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's obstacles: a still sphere's cells solid and empty, about as
// many as its area holds; flow past a still box kept free of divergence,
// the box's faces still; a host's sphere moving at a known speed giving
// the faces beside it that speed from the second step (none at the first
// or after a teleport), in steps of either length; a plane moving its
// faces along its normal alone; an obstacle in the instance's frame
// placed through a moved, turned and scaled instance, in a world gas and
// a local one; a turning box giving its faces its turn's
// velocity; a local gas moving past a still box in the world seeing it
// move; 2D and 3D gases projected around a solid, and a sealed gas's
// expansion from burning kept less its open cells' mean; an obstacle's
// grid held by its effect; the obstacles' refusals. A gas's solid cells
// and faces are read, and its faces and scalars written, through the test
// hooks.

#include "test_harness.h"
#include "test_hooks.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

enum
{
    // The most cells, and faces along an axis, a test's gas has.
    MOST = 4096
};

// The gas's solid cells and its faces along each axis, as read last.
static _Alignas(16) uint8_t s_solid[MOST];
static float s_faces[3][MOST];

// Opens an instance of an effect of one gas.
static void Open(const mvfxGasDef* gas)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
}

// The count of a gas's faces along k.
static int64_t FaceCount(const int32_t* n, int32_t k)
{
    return (int64_t)(n[0] + (k == 0 ? 1 : 0)) * (n[1] + (k == 1 ? 1 : 0)) *
           (n[2] + (k == 2 ? 1 : 0));
}

// Reads the gas's solid cells into s_solid and its faces into s_faces.
static void Read(const int32_t* n)
{
    CHECK(mvfxTestReadGasSolid(s_context, s_instance, 0, s_solid, MOST) ==
              (int64_t)n[0] * n[1] * n[2],
          "solids read");
    for (int32_t k = 0; k < 3; ++k)
    {
        CHECK(mvfxTestReadGasFaces(s_context, s_instance, 0, k, s_faces[k], MOST) ==
                  FaceCount(n, k),
              "faces read");
    }
}

// Writes s_faces into the gas's faces.
static void Write(const int32_t* n)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        CHECK(mvfxTestWriteGasFaces(s_context, s_instance, 0, k, s_faces[k], FaceCount(n, k)),
              "faces written");
    }
}

// Steps the context by n steps of 1 / rate s.
static void StepsOf(int32_t n, int32_t rate)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / rate) == mvfx_success,
              "step");
    }
}

// Steps the context by n steps of 1/64 s.
static void Steps(int32_t n)
{
    StepsOf(n, 64);
}

// A collider of a kind about a center in a space, unturned.
static mvfxCollider Collider(mvfxColliderKind kind, float x, float y, uint8_t space)
{
    return (mvfxCollider){.kind = kind,
                          .space = space,
                          .center = {x, y, 0},
                          .normal = {0, 1, 0},
                          .radius = 0.5f,
                          .halfExtents = {0.4f, 0.3f, 1.0f},
                          .rotation = {0, 0, 0, 1}};
}

static void TestStill(void)
{
    // A 2D gas of 32 by 32 cells of 0.1, a box source pouring density
    // everywhere, a sphere of radius 0.5 at its middle: after 4 steps its
    // solid cells (centers within the sphere) hold nothing and the rest
    // hold density; they number about pi 25, within 8.
    static const mvfxGasSource everywhere = {
        .shape = mvfx_gasBox, .halfExtents = {2, 2, 1}, .density = 1};
    const mvfxCollider sphere = Collider(mvfx_colliderSphere, 0, 0, mvfx_spaceWorld);
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 32;
    def.size[1] = 32;
    def.sources = &everywhere;
    def.sourceCount = 1;
    def.obstacles = &sphere;
    def.obstacleCount = 1;
    Open(&def);
    Steps(4);
    Read(def.size);
    static float density[1024];
    CHECK(mvfxInstance_ReadGas(s_context, s_instance, 0, mvfx_gasDensity, density, 1024) ==
              mvfx_success,
          "density");
    int32_t solids = 0;
    bool right = true;
    for (int32_t i = 0; i < 1024; ++i)
    {
        float x = ((float)(i % 32) + 0.5f - 16.0f) * 0.1f;
        float y = ((float)(i / 32) + 0.5f - 16.0f) * 0.1f;
        bool inside = x * x + y * y < 0.25f;
        solids += s_solid[i] != 0 ? 1 : 0;
        right = right && (s_solid[i] != 0) == inside &&
                (inside ? density[i] == 0.0f : density[i] > 0.0f);
    }
    CHECK(right && solids >= 71 && solids <= 87, "a still sphere's cells solid and empty");
    mvfxDestroyContext(s_context);
}

// The largest divergence's size over a 2D gas's open cells, as read.
static float MaxOpenDivergence(const int32_t* n)
{
    const float* u = s_faces[0];
    const float* v = s_faces[1];
    float most = 0.0f;
    for (int32_t y = 0; y < n[1]; ++y)
    {
        for (int32_t x = 0; x < n[0]; ++x)
        {
            float d = u[y * (n[0] + 1) + x + 1] - u[y * (n[0] + 1) + x] + v[(y + 1) * n[0] + x] -
                      v[y * n[0] + x];
            most = s_solid[y * n[0] + x] == 0 ? fmaxf(most, fabsf(d)) : most;
        }
    }
    return most;
}

static void TestFlowPast(void)
{
    // A 2D gas of 48 by 24, open walls, a box of wind at its left blowing
    // right at 2 into a still box in its middle, 8 V cycles: after 10
    // steps the faces beside the box are still, the flow goes around it
    // (some face beside it is not still across the other axis' faces),
    // and its open cells' divergence is at most 1e-3 of the wind's 2 / 0.1
    // a cell.
    static const mvfxGasSource wind = {.shape = mvfx_gasBox,
                                       .center = {-2.0f, 0, 0},
                                       .halfExtents = {0.3f, 1.0f, 1},
                                       .setsVelocity = true,
                                       .velocity = {2, 0, 0}};
    const mvfxCollider box = Collider(mvfx_colliderBox, 0, 0, mvfx_spaceWorld);
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 48;
    def.size[1] = 24;
    def.pressureCycles = 8;
    def.sources = &wind;
    def.sourceCount = 1;
    def.obstacles = &box;
    def.obstacleCount = 1;
    Open(&def);
    Steps(10);
    Read(def.size);
    const float* u = s_faces[0];
    bool still = true;
    for (int32_t y = 0; y < 24; ++y)
    {
        for (int32_t x = 0; x <= 48; ++x)
        {
            bool beside =
                (x > 0 && s_solid[y * 48 + x - 1] != 0) || (x < 48 && s_solid[y * 48 + x] != 0);
            still = still && (!beside || u[y * 49 + x] == 0.0f);
        }
    }
    float most = MaxOpenDivergence(def.size);
    CHECK(still && most <= 1e-3f * 20.0f, "flow past a still box");
    mvfxDestroyContext(s_context);
}

// The largest and the least velocity across k of the faces beside a 2D
// gas's solid cells, as read.
static void SolidFaces(const int32_t* n, int32_t k, float range[2])
{
    const int32_t m[2] = {n[0] + (k == 0 ? 1 : 0), n[1] + (k == 1 ? 1 : 0)};
    const int32_t stride = k == 0 ? 1 : n[0];
    range[0] = INFINITY;
    range[1] = -INFINITY;
    for (int32_t y = 0; y < m[1]; ++y)
    {
        for (int32_t x = 0; x < m[0]; ++x)
        {
            const int32_t at[2] = {x, y};
            int32_t cell = y * n[0] + x;
            bool beside =
                (at[k] > 0 && s_solid[cell - stride] != 0) || (at[k] < n[k] && s_solid[cell] != 0);
            float value = s_faces[k][y * m[0] + x];
            range[0] = beside ? fminf(range[0], value) : range[0];
            range[1] = beside ? fmaxf(range[1], value) : range[1];
        }
    }
}

static void TestMoving(void)
{
    // A 2D gas of 32 by 32 with no pressure, its host's sphere set at x =
    // -0.5 + 0.75 t each step: at the first step its faces are still (no
    // step before); from the second, its X faces move at 0.75 and its Y
    // faces not at all; after a teleport, still again for a step. The same
    // in steps of 1/64 s and of 1/32 s.
    bool right = true;
    for (int32_t rate = 32; rate <= 64; rate += 32)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 32;
        def.size[1] = 32;
        def.pressureCycles = 0;
        def.hostObstacles = true;
        Open(&def);
        for (int32_t i = 0; i < 6; ++i)
        {
            mvfxCollider sphere = Collider(
                mvfx_colliderSphere, -0.5f + 0.75f * (float)i / (float)rate, 0, mvfx_spaceWorld);
            CHECK(mvfxInstance_SetColliders(s_context, s_instance, &sphere, 1) == mvfx_success,
                  "colliders");
            if (i == 4)
            {
                const double origin[3] = {0, 0, 0};
                const float rotation[4] = {0, 0, 0, 1};
                CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, rotation, 1.0f,
                                                true) == mvfx_success,
                      "teleport");
            }
            StepsOf(1, rate);
            Read(def.size);
            float across[2];
            float up[2];
            SolidFaces(def.size, 0, across);
            SolidFaces(def.size, 1, up);
            float speed = i == 0 || i == 4 ? 0.0f : 0.75f;
            right = right && fabsf(across[0] - speed) <= 1e-3f &&
                    fabsf(across[1] - speed) <= 1e-3f && up[0] == 0.0f && up[1] == 0.0f;
        }
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "a moving sphere's faces move with it");
}

static void TestPlane(void)
{
    // A host's floor plane rising at 0.5 a second while sliding along X at
    // 0.3 in a 2D gas of 32 by 32: the faces beside it rise with it, the
    // slide along the plane moves none of them.
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 32;
    def.size[1] = 32;
    def.pressureCycles = 0;
    def.hostObstacles = true;
    Open(&def);
    bool right = true;
    for (int32_t i = 0; i < 3; ++i)
    {
        mvfxCollider floor = Collider(mvfx_colliderPlane, 0.3f * (float)i / 64.0f,
                                      -0.5f + 0.5f * (float)i / 64.0f, mvfx_spaceWorld);
        CHECK(mvfxInstance_SetColliders(s_context, s_instance, &floor, 1) == mvfx_success,
              "colliders");
        Steps(1);
        Read(def.size);
        float across[2];
        float up[2];
        SolidFaces(def.size, 0, across);
        SolidFaces(def.size, 1, up);
        float rise = i == 0 ? 0.0f : 0.5f;
        right = right && fabsf(up[0] - rise) <= 1e-3f && fabsf(up[1] - rise) <= 1e-3f &&
                across[0] == 0.0f && across[1] == 0.0f;
    }
    CHECK(right, "a plane moves along its normal");
    mvfxDestroyContext(s_context);
}

static void TestInstanceFrame(void)
{
    // A sphere of radius 0.3 at (0.2, 0, 0) in the instance's frame, the
    // instance moved to (0.5, 0, 0), turned a quarter about Z and scaled
    // by 1.5. In a world gas (its box where the instance began, at the
    // origin) it is a sphere of 0.45 at (0.5, 0.3); in a local gas centered
    // at (0.3, 0, 0) in the instance's frame, one of 0.3 at (-0.1, 0) in
    // the box's own. Its cells are solid exactly where those hold them.
    const float quarter[4] = {0, 0, 0.70710678f, 0.70710678f};
    const double origin[3] = {0.5, 0, 0};
    bool right = true;
    for (int32_t world = 0; world < 2; ++world)
    {
        mvfxCollider ball = Collider(mvfx_colliderSphere, 0.2f, 0, mvfx_spaceLocal);
        ball.radius = 0.3f;
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 32;
        def.size[1] = 32;
        def.space = world != 0 ? mvfx_gasWorld : mvfx_gasLocal;
        def.center[0] = world != 0 ? 0.0f : 0.3f;
        def.obstacles = &ball;
        def.obstacleCount = 1;
        Open(&def);
        CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, quarter, 1.5f, false) ==
                  mvfx_success,
              "moved");
        Steps(1);
        Read(def.size);
        const float center[2] = {world != 0 ? 0.5f : -0.1f, world != 0 ? 0.3f : 0.0f};
        float radius = world != 0 ? 0.45f : 0.3f;
        for (int32_t i = 0; i < 1024; ++i)
        {
            float x = ((float)(i % 32) + 0.5f - 16.0f) * 0.1f - center[0];
            float y = ((float)(i / 32) + 0.5f - 16.0f) * 0.1f - center[1];
            float gap = sqrtf(x * x + y * y) - radius;
            // Cells within float's rounding of the surface are not judged.
            right = right && (fabsf(gap) < 1e-4f || (s_solid[i] != 0) == (gap < 0.0f));
        }
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "an obstacle in the instance's frame");
}

static void TestTurning(void)
{
    // A host's box turning about Z at 2 radians a second about the middle
    // of a 2D gas of 32 by 32: each face beside it moves as w x r, within
    // 2% of the largest speed (1, at its corners): the velocity is the
    // step's chord over its span, turned from the face's tangent by half
    // the step's turn, 1/64 radian (1.26% seen).
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 32;
    def.size[1] = 32;
    def.pressureCycles = 0;
    def.hostObstacles = true;
    Open(&def);
    float worst = 0.0f;
    for (int32_t i = 0; i < 3; ++i)
    {
        mvfxCollider box = Collider(mvfx_colliderBox, 0, 0, mvfx_spaceWorld);
        float half = (float)i / 64.0f;
        box.rotation[2] = sinf(half);
        box.rotation[3] = cosf(half);
        CHECK(mvfxInstance_SetColliders(s_context, s_instance, &box, 1) == mvfx_success,
              "colliders");
        Steps(1);
    }
    Read(def.size);
    for (int32_t k = 0; k < 2; ++k)
    {
        const int32_t m[2] = {32 + (k == 0 ? 1 : 0), 32 + (k == 1 ? 1 : 0)};
        const int32_t stride = k == 0 ? 1 : 32;
        for (int32_t y = 0; y < m[1]; ++y)
        {
            for (int32_t x = 0; x < m[0]; ++x)
            {
                const int32_t at[2] = {x, y};
                int32_t cell = y * 32 + x;
                bool beside = (at[k] > 0 && s_solid[cell - stride] != 0) ||
                              (at[k] < 32 && s_solid[cell] != 0);
                float px = ((float)x + (k == 0 ? 0.0f : 0.5f) - 16.0f) * 0.1f;
                float py = ((float)y + (k == 1 ? 0.0f : 0.5f) - 16.0f) * 0.1f;
                float expected = k == 0 ? -2.0f * py : 2.0f * px;
                float value = s_faces[k][y * m[0] + x];
                worst = beside ? fmaxf(worst, fabsf(value - expected)) : worst;
            }
        }
    }
    CHECK(worst <= 0.02f * 2.0f * 0.5f, "a turning box's faces turn with it");
    mvfxDestroyContext(s_context);
}

static void TestPassing(void)
{
    // A local 2D gas whose instance moves right at 1.5 a second past a
    // still box in the world: in the box's frame the obstacle moves left,
    // so its X faces take -1.5 from the second step.
    const mvfxCollider box = Collider(mvfx_colliderBox, 0.2f, 0, mvfx_spaceWorld);
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 32;
    def.size[1] = 32;
    def.pressureCycles = 0;
    def.obstacles = &box;
    def.obstacleCount = 1;
    Open(&def);
    bool right = true;
    for (int32_t i = 0; i < 4; ++i)
    {
        const double origin[3] = {1.5 * (double)i / 64.0, 0, 0};
        const float rotation[4] = {0, 0, 0, 1};
        CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, rotation, 1.0f, false) ==
                  mvfx_success,
              "moved");
        Steps(1);
        Read(def.size);
        float across[2];
        SolidFaces(def.size, 0, across);
        float speed = i == 0 ? 0.0f : -1.5f;
        right = right && fabsf(across[0] - speed) <= 1e-3f && fabsf(across[1] - speed) <= 1e-3f;
    }
    CHECK(right, "a still wall moves past a moving gas");
    mvfxDestroyContext(s_context);
}

// A hash's float in [-1, 1).
static float Noise(uint32_t i)
{
    uint32_t h = (i + 1u) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (float)(h & 0xFFFFFFu) / 8388608.0f - 1.0f;
}

// The largest divergence's size over a gas's open cells, as read.
static float MaxOpenDivergence3D(const int32_t* n)
{
    float* const* v = (float* const[3]){s_faces[0], s_faces[1], s_faces[2]};
    float most = 0.0f;
    for (int32_t z = 0; z < n[2]; ++z)
    {
        for (int32_t y = 0; y < n[1]; ++y)
        {
            for (int32_t x = 0; x < n[0]; ++x)
            {
                int64_t i = ((int64_t)z * n[1] + y) * n[0] + x;
                float d = v[0][((int64_t)z * n[1] + y) * (n[0] + 1) + x + 1] -
                          v[0][((int64_t)z * n[1] + y) * (n[0] + 1) + x] +
                          v[1][((int64_t)z * (n[1] + 1) + y + 1) * n[0] + x] -
                          v[1][((int64_t)z * (n[1] + 1) + y) * n[0] + x];
                d += n[2] > 1 ? v[2][((int64_t)(z + 1) * n[1] + y) * n[0] + x] -
                                    v[2][((int64_t)z * n[1] + y) * n[0] + x]
                              : 0.0f;
                most = s_solid[i] == 0 ? fmaxf(most, fabsf(d)) : most;
            }
        }
    }
    return most;
}

static void TestAround(void)
{
    // A 2D gas of 32 by 32 about a still box and a 3D one of 16 by 12 by 10
    // about a still sphere, open walls, the obstacle marked solid by a
    // step: noise for a velocity, projected by 8 cycles in a step of a
    // tick, leaves its open cells' divergence at most 2e-3 (the noise's is
    // up to 4 and 6).
    const mvfxCollider obstacles[2] = {
        Collider(mvfx_colliderBox, 0, 0, mvfx_spaceWorld),
        Collider(mvfx_colliderSphere, 0.1f, -0.05f, mvfx_spaceWorld)};
    static const int32_t sizes[2][3] = {{32, 32, 1}, {16, 12, 10}};
    bool right = true;
    for (int32_t c = 0; c < 2; ++c)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        memcpy(def.size, sizes[c], sizeof def.size);
        def.pressureCycles = 8;
        def.obstacles = &obstacles[c];
        def.obstacleCount = 1;
        Open(&def);
        Steps(1);
        for (int32_t k = 0; k < 3; ++k)
        {
            for (int64_t f = 0; f < FaceCount(def.size, k); ++f)
            {
                s_faces[k][f] = Noise((uint32_t)(f * 3 + k));
            }
        }
        Write(def.size);
        CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "projected");
        Read(def.size);
        int32_t solids = 0;
        for (int32_t i = 0; i < def.size[0] * def.size[1] * def.size[2]; ++i)
        {
            solids += s_solid[i] != 0 ? 1 : 0;
        }
        right = right && solids > 0 && MaxOpenDivergence3D(def.size) <= 2e-3f;
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "a gas projected around a solid");
}

static void TestSealedSolid(void)
{
    // The same box in a sealed gas, still, fuel of 1 burning in four open
    // cells, each growing by burnExpansion over the step for each unit
    // burned (the fuel the step took from it): 8 cycles leave each open
    // cell's divergence its expansion less their mean over the open
    // cells, within 1e-3.
    const mvfxCollider box = Collider(mvfx_colliderBox, 0, 0, mvfx_spaceWorld);
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 32;
    def.size[1] = 32;
    def.pressureCycles = 8;
    def.closedWalls = 63;
    def.burnRate = 10.0f;
    def.burnExpansion = 1.0f;
    def.obstacles = &box;
    def.obstacleCount = 1;
    Open(&def);
    Steps(1);
    static float fuel[2][1024];
    static float heat[1024];
    for (int32_t i = 0; i < 1024; ++i)
    {
        int32_t x = i % 32;
        int32_t y = i / 32;
        fuel[0][i] = (x == 3 || x == 4) && (y == 3 || y == 4) ? 1.0f : 0.0f;
        heat[i] = 1.0f;
    }
    CHECK(mvfxTestWriteGasScalar(s_context, s_instance, 0, mvfx_gasFuel, fuel[0], 1024) &&
              mvfxTestWriteGasScalar(s_context, s_instance, 0, mvfx_gasTemperature, heat, 1024),
          "fuel");
    Steps(1);
    Read(def.size);
    CHECK(mvfxInstance_ReadGas(s_context, s_instance, 0, mvfx_gasFuel, fuel[1], 1024) ==
              mvfx_success,
          "burned");
    // A unit burned grows its cell by burnExpansion over the step of
    // 1/64 s, in the projection's units its edge times that over the step.
    float grow = def.burnExpansion * def.cellSize * 64.0f;
    static float expansion[1024];
    float sum = 0.0f;
    int32_t open = 0;
    for (int32_t i = 0; i < 1024; ++i)
    {
        expansion[i] = grow * (fuel[0][i] - fuel[1][i]);
        sum += expansion[i];
        open += s_solid[i] == 0 ? 1 : 0;
    }
    float mean = sum / (float)open;
    bool right = sum > 0.0f;
    const float* u = s_faces[0];
    const float* v = s_faces[1];
    for (int32_t i = 0; i < 1024; ++i)
    {
        int32_t x = i % 32;
        int32_t y = i / 32;
        float d = u[y * 33 + x + 1] - u[y * 33 + x] + v[(y + 1) * 32 + x] - v[y * 32 + x];
        right = right && (s_solid[i] != 0 || fabsf(d - (expansion[i] - mean)) <= 1e-3f);
    }
    CHECK(right, "a sealed gas's expansion less its open cells' mean");
    mvfxDestroyContext(s_context);
}

static void TestHeld(void)
{
    // A distance grid as an obstacle: its effect holds it (destroying it
    // is refused as busy) until the effect is destroyed; cells where it is
    // negative are solid.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    static float values[8 * 8];
    for (int32_t i = 0; i < 64; ++i)
    {
        values[i] = (float)(i % 8) - 3.5f;
    }
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 8;
    gridDef.size[1] = 8;
    gridDef.values = values;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(s_context, &gridDef, &grid) == mvfx_success, "grid");
    mvfxCollider field = Collider(mvfx_colliderDistance, 0, 0, mvfx_spaceWorld);
    field.halfExtents[0] = 1.0f;
    field.halfExtents[1] = 1.0f;
    field.halfExtents[2] = 1.0f;
    field.grid = grid;
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 16;
    gas.size[1] = 16;
    gas.obstacles = &field;
    gas.obstacleCount = 1;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect, NULL) == mvfx_success, "effect");
    bool right = mvfxDestroyGrid(s_context, grid) == mvfx_errorBusy;
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    Steps(1);
    Read(gas.size);
    // The field crosses 0 at the box's middle along X: its left half solid.
    for (int32_t i = 0; i < 256; ++i)
    {
        int32_t x = i % 16;
        right = right && (s_solid[i] != 0) == (x < 8);
    }
    CHECK(mvfxDestroyInstance(s_context, s_instance) == mvfx_success, "instance gone");
    CHECK(mvfxDestroyEffect(s_context, effect) == mvfx_success, "effect gone");
    right = right && mvfxDestroyGrid(s_context, grid) == mvfx_success;
    CHECK(right, "an obstacle's grid held");
    mvfxDestroyContext(s_context);
}

static void TestRefusals(void)
{
    // Too many obstacles, NULL obstacles, and an invalid one are refused;
    // the most are taken.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxCollider many[MVFX_MAX_COLLIDERS + 1];
    for (int32_t c = 0; c <= MVFX_MAX_COLLIDERS; ++c)
    {
        many[c] = Collider(mvfx_colliderSphere, 0, 0, mvfx_spaceWorld);
    }
    bool right = true;
    for (int32_t c = 0; c < 3; ++c)
    {
        mvfxGasDef gas = mvfxDefaultGasDef();
        gas.obstacles = c == 1 ? NULL : many;
        gas.obstacleCount = c == 0 ? MVFX_MAX_COLLIDERS + 1 : 1;
        if (c == 2)
        {
            many[0].radius = 0.0f;
        }
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.gases = &gas;
        def.gasCount = 1;
        mvfxEffectId effect = {0};
        mvfxResult made = mvfxCreateEffect(s_context, &def, &effect, NULL);
        // Past MVFX_MAX_COLLIDERS is past a named bound.
        right = right && made == (c == 0 ? mvfx_errorLimit : mvfx_errorInvalid);
        many[0].radius = 0.5f;
    }
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.obstacles = many;
    gas.obstacleCount = MVFX_MAX_COLLIDERS;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    right = right && mvfxCreateEffect(s_context, &def, &effect, NULL) == mvfx_success;
    CHECK(right, "the obstacles' refusals");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestStill();
    TestFlowPast();
    TestMoving();
    TestPlane();
    TestInstanceFrame();
    TestTurning();
    TestPassing();
    TestAround();
    TestSealedSolid();
    TestHeld();
    TestRefusals();
    return s_failures;
}
