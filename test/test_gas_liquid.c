// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Grid liquids (APIC on a liquid gas): the refusals; a cell's filled share
// as its density; a still pool staying still; a turning disk keeping its
// angular momentum; a dam break's front against Martin and
// Moyce's measurements and Ritter's frictionless speed, its volume kept;
// a pour the same whatever its emitter's room; a split step's events the
// whole step's.

#include "particles.h"
#include "test_harness.h"
#include "test_hooks.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;
// The next instance's origin and turn about Z (radians).
static double s_origin[3];
static float s_turn;
// Whether Open's instance is planned.
static bool s_planned;

enum
{
    MOST = 1024
};

// Emitter 0's particles as the output writes them, read again after any
// step (s_steps counts them).
static TestParticle s_particles[MOST];
static int32_t s_count;
static uint64_t s_steps;
static uint64_t s_read = UINT64_MAX;

// A 2D liquid gas of n0 by n1 cells of cell, its lower left corner at the
// origin, closed on every side.
static mvfxGasDef Gas(int32_t n0, int32_t n1, float cell)
{
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.kind = mvfx_gasLiquid;
    gas.size[0] = n0;
    gas.size[1] = n1;
    gas.size[2] = 1;
    gas.cellSize = cell;
    gas.center[0] = 0.5f * cell * (float)n0;
    gas.center[1] = 0.5f * cell * (float)n1;
    gas.closedWalls = 63;
    return gas;
}

// A grid liquid's emitter on gas 0: host spawns only, long lives, its
// particles spacing apart at rest, kept in the gas's box.
static mvfxEmitterDef Emitter(float spacing, const mvfxCollider* box, int32_t capacity)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = capacity;
    emitter.hostSpawnCapacity = capacity;
    emitter.lifetime = mvfxMakeRangeSource(1000.0f, 1000.0f);
    emitter.liquid.enable = true;
    emitter.liquid.solver = mvfx_liquidGrid;
    emitter.liquid.gas = 0;
    emitter.liquid.spacing = spacing;
    emitter.collision.enable = box != NULL;
    emitter.colliders = box;
    emitter.colliderCount = box != NULL ? 1 : 0;
    return emitter;
}

// The box collider of a gas's box, kept inside.
static mvfxCollider BoxOf(const mvfxGasDef* gas)
{
    return (mvfxCollider){.kind = mvfx_colliderBox,
                          .inside = true,
                          .center = {gas->center[0], gas->center[1], 0.0f},
                          .halfExtents = {0.5f * gas->cellSize * (float)gas->size[0],
                                          0.5f * gas->cellSize * (float)gas->size[1], 1.0f},
                          .rotation = {0, 0, 0, 1}};
}

// Opens an effect of the emitters and gases; false when refused.
static bool Open(const mvfxEmitterDef* emitters, int32_t emitterCount, const mvfxGasDef* gases,
                 int32_t gasCount)
{
    ++s_steps;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = emitterCount;
    def.gases = gases;
    def.gasCount = gasCount;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(s_context, &def, &effect, NULL) != mvfx_success)
    {
        return false;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    memcpy(instanceDef.origin, s_origin, sizeof s_origin);
    instanceDef.rotation[2] = sinf(0.5f * s_turn);
    instanceDef.rotation[3] = cosf(0.5f * s_turn);
    instanceDef.planned = s_planned;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    return true;
}

// Spawns particles at places with velocities (NULL for still) and steps
// one tick to bear them.
static void Spawn(const double* places, const float* velocities, int32_t count)
{
    mvfxSpawnRequest request = {.count = count, .positions = places, .velocities = velocities};
    CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "born");
    ++s_steps;
}

// Steps of 1/120 s.
// Steps the context n times at rate steps a second.
static void StepsAt(int32_t n, int32_t rate)
{
    for (int32_t s = 0; s < n; ++s)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / rate) == mvfx_success,
              "step");
        ++s_steps;
    }
}

static void Steps(int32_t n)
{
    StepsAt(n, 120);
}

static int32_t Count(void)
{
    if (s_read != s_steps)
    {
        s_count = ReadParticles(s_context, s_instance, 0, s_particles, MOST);
        s_read = s_steps;
    }
    return s_count;
}

// Particle i.
static const TestParticle* At(int32_t i)
{
    (void)Count();
    return &s_particles[i];
}

static void TestRefusals(void)
{
    // A liquid gas with a source, a force or two substeps; a grid liquid on
    // a smoke gas or on no gas; two grid liquids on one gas; a liquid gas
    // no emitter names; an emitter splatting into a liquid gas. Then the
    // valid pair is taken.
    const mvfxGasSource source = {.shape = mvfx_gasSphere, .radius = 0.1f, .density = 1};
    const mvfxForce force = {.kind = mvfx_forceConstant,
                             .strength = {.kind = mvfx_sourceRange, .range = {1, 1}},
                             .vector = {1, 0, 0}};
    bool right = true;
    for (int32_t c = 0; c < 9; ++c)
    {
        mvfxGasDef gas = Gas(8, 8, 0.1f);
        gas.sources = c == 0 ? &source : NULL;
        gas.sourceCount = c == 0 ? 1 : 0;
        gas.forces = c == 1 ? &force : NULL;
        gas.forceCount = c == 1 ? 1 : 0;
        gas.substeps = c == 2 ? 2 : 1;
        gas.kind = c == 3 ? mvfx_gasSmoke : mvfx_gasLiquid;
        mvfxEmitterDef emitters[2] = {Emitter(0.05f, NULL, 16), Emitter(0.05f, NULL, 16)};
        emitters[0].liquid.gas = c == 4 ? 1 : 0;
        emitters[1].liquid.enable = c == 5;
        emitters[0].liquid.enable = c != 6;
        emitters[1].gasSplat = (mvfxGasSplat){.enable = c == 7, .gas = 0, .density = 1};
        bool opened = Open(emitters, 2, &gas, 1);
        right = right && opened == (c == 8);
        CHECK(opened == (c == 8), "a grid liquid's refusal");
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "grid liquids' refusals");
}

static void TestFill(void)
{
    // Four particles in a cell of 0.1 at spacing 0.05 fill it (density 1);
    // one fills a quarter; eight fill a cell twice, read as 1; one on the
    // box's far face is outside it; an empty cell is 0. The same with the
    // instance far from the origin (a world emitter's places offsets from
    // its anchor). Read as the spawning step marked them.
    bool right = true;
    for (int32_t c = 0; c < 2; ++c)
    {
        s_origin[0] = c == 1 ? 100.0 : 0.0;
        s_origin[1] = c == 1 ? -50.0 : 0.0;
        mvfxGasDef gas = Gas(4, 4, 0.1f);
        mvfxEmitterDef emitter = Emitter(0.05f, NULL, 16);
        CHECK(Open(&emitter, 1, &gas, 1), "opened");
        double places[42] = {0.125, 0.125, 0, 0.175, 0.125, 0, 0.125, 0.175, 0,
                             0.175, 0.175, 0, 0.25,  0.05,  0, 0.4,   0.05,  0};
        for (int32_t i = 0; i < 8; ++i)
        {
            places[18 + 3 * i] = 0.31 + 0.01 * (double)i;
            places[18 + 3 * i + 1] = 0.35;
        }
        for (int32_t i = 0; i < 14; ++i)
        {
            places[3 * i] += s_origin[0];
            places[3 * i + 1] += s_origin[1];
        }
        Spawn(places, NULL, 14);
        static float density[16];
        CHECK(mvfxInstance_ReadGas(s_context, s_instance, 0, mvfx_gasDensity, density, 16) ==
                  mvfx_success,
              "read");
        // Cells (1, 1), (2, 0), (3, 3), (3, 0), (0, 1) and (0, 0).
        right = right && fabsf(density[5] - 1.0f) <= 1e-6f && fabsf(density[2] - 0.25f) <= 1e-6f &&
                density[15] == 1.0f && density[3] == 0.0f && density[4] == 0.0f &&
                density[0] == 0.0f;
        mvfxDestroyContext(s_context);
    }
    s_origin[0] = s_origin[1] = 0.0;
    CHECK(right, "a cell's filled share is its density");
}

static void TestOutside(void)
{
    // A particle beyond the box keeps its own velocity: the gas gives it
    // none.
    mvfxGasDef gas = Gas(4, 4, 0.1f);
    mvfxEmitterDef emitter = Emitter(0.05f, NULL, 4);
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    const double places[6] = {0.125, 0.125, 0, 0.6, 0.2, 0};
    const float velocities[6] = {0, 0, 0, 1, 0, 0};
    Spawn(places, velocities, 2);
    Steps(2);
    CHECK(At(1)->velocity[0] == 1.0f && At(1)->velocity[1] == 0.0f,
          "a particle beyond the box keeps its velocity");
    mvfxDestroyContext(s_context);
}

// Spawns a block of particles, per a side of each cell, from cells x0 to
// x1 and y0 to y1 of a gas of cell, with velocities from a function of the
// place (NULL for still).
static void Block(float cell, int32_t per, const int32_t from[2], const int32_t to[2],
                  void (*velocity)(const double* place, float* out))
{
    int32_t across = (to[0] - from[0]) * per;
    int32_t up = (to[1] - from[1]) * per;
    int32_t count = across * up;
    double* places = malloc(sizeof(double) * 3u * (size_t)count);
    float* velocities = malloc(sizeof(float) * 3u * (size_t)count);
    CHECK(places != NULL && velocities != NULL, "memory");
    double spacing = (double)cell / (double)per;
    double c = cos((double)s_turn);
    double s = sin((double)s_turn);
    for (int32_t i = 0; i < count; ++i)
    {
        double x = (double)from[0] * (double)cell + spacing * (0.5 + (double)(i % across));
        double y = (double)from[1] * (double)cell + spacing * (0.5 + (double)(i / across));
        float u[3] = {0.0f, 0.0f, 0.0f};
        if (velocity != NULL)
        {
            velocity((const double[3]){x, y, 0.0}, u);
        }
        // In the world: turned with the instance.
        places[3 * i] = c * x - s * y;
        places[3 * i + 1] = s * x + c * y;
        places[3 * i + 2] = 0.0;
        velocities[3 * i] = (float)(c * (double)u[0] - s * (double)u[1]);
        velocities[3 * i + 1] = (float)(s * (double)u[0] + c * (double)u[1]);
        velocities[3 * i + 2] = 0.0f;
    }
    Spawn(places, velocities, count);
    free(places);
    free(velocities);
}

static float MaxSpeed(void)
{
    float most = 0.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        float vx = At(i)->velocity[0];
        float vy = At(i)->velocity[1];
        most = fmaxf(most, sqrtf(vx * vx + vy * vy));
    }
    return most;
}

static void TestPool(void)
{
    // A pool 16 cells wide and 8 deep in a closed box of 16 by 12 cells of
    // 0.1, 2 by 2 particles a cell, gravity 9.81 down, 2 s: its particles
    // stay nearly still (none faster than 0.2) and its surface stays (no
    // particle above 0.85 nor below 0); in its middle column the pressure
    // rises by depth, each cell's step from the one above within 10% of
    // their mean below the top two cells.
    mvfxGasDef gas = Gas(16, 12, 0.1f);
    mvfxCollider box = BoxOf(&gas);
    mvfxEmitterDef emitter = Emitter(0.05f, &box, 512);
    emitter.motion.gravity[1] = -9.81f;
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    Block(0.1f, 2, (const int32_t[2]){0, 0}, (const int32_t[2]){16, 8}, NULL);
    Steps(240);
    float top = 0.0f;
    float low = 1.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        top = fmaxf(top, At(i)->position[1]);
        low = fminf(low, At(i)->position[1]);
    }
    CHECK(Count() == 512 && MaxSpeed() < 0.2f && top < 0.85f && low >= 0.0f, "a pool stays still");
    mvfxDestroyContext(s_context);
}

static void TestObstacle(void)
{
    // The pool of TestPool around a ball of radius 0.25 on its floor, an
    // obstacle of the gas and a collider of the particles: 2 s on, it
    // stays nearly still (none faster than 0.2), none inside the ball.
    mvfxGasDef gas = Gas(16, 12, 0.1f);
    const mvfxCollider colliders[2] = {
        BoxOf(&gas), {.kind = mvfx_colliderSphere, .center = {0.8f, 0.3f, 0.0f}, .radius = 0.25f}};
    gas.obstacles = &colliders[1];
    gas.obstacleCount = 1;
    mvfxEmitterDef emitter = Emitter(0.05f, colliders, 512);
    emitter.colliderCount = 2;
    emitter.motion.gravity[1] = -9.81f;
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    static double places[3 * 512];
    int32_t count = 0;
    for (int32_t i = 0; i < 512; ++i)
    {
        double x = 0.025 + 0.05 * (double)(i % 32);
        double y = 0.025 + 0.05 * (double)(i / 32);
        if ((x - 0.8) * (x - 0.8) + (y - 0.3) * (y - 0.3) > 0.3 * 0.3)
        {
            places[3 * count] = x;
            places[3 * count + 1] = y;
            places[3 * count + 2] = 0.0;
            ++count;
        }
    }
    Spawn(places, NULL, count);
    Steps(240);
    float nearest = 1.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        float dx = At(i)->position[0] - 0.8f;
        float dy = At(i)->position[1] - 0.3f;
        nearest = fminf(nearest, sqrtf(dx * dx + dy * dy));
    }
    CHECK(Count() == count && MaxSpeed() < 0.2f && nearest >= 0.25f - 1e-4f,
          "a pool stays still about an obstacle");
    mvfxDestroyContext(s_context);
}

// A turn of 2 a second about (0.6, 0.6).
static void Turning(const double* place, float* out)
{
    out[0] = (float)(-2.0 * (place[1] - 0.6));
    out[1] = (float)(2.0 * (place[0] - 0.6));
}

// The particles' angular momentum about (0.6, 0.6) of the box, per unit
// mass.
static double AngularMomentum(void)
{
    double c = cos((double)s_turn);
    double s = sin((double)s_turn);
    double sum = 0.0;
    for (int32_t i = 0; i < Count(); ++i)
    {
        double x = (double)At(i)->position[0] - (c - s) * 0.6;
        double y = (double)At(i)->position[1] - (s + c) * 0.6;
        sum += x * (double)At(i)->velocity[1] - y * (double)At(i)->velocity[0];
    }
    return sum;
}

static void TestTurning(void)
{
    // A square of 12 by 12 cells of 0.05 turning at 2 a second about the
    // middle of a closed box of 24 by 24, 2 by 2 particles a cell, no
    // gravity, 1 s: APIC keeps at least 85% of its angular momentum.
    mvfxGasDef gas = Gas(24, 24, 0.05f);
    mvfxCollider box = BoxOf(&gas);
    mvfxEmitterDef emitter = Emitter(0.025f, &box, 576);
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    Block(0.05f, 2, (const int32_t[2]){6, 6}, (const int32_t[2]){18, 18}, Turning);
    double start = AngularMomentum();
    Steps(120);
    CHECK(AngularMomentum() / start >= 0.85, "APIC keeps a turn");
    mvfxDestroyContext(s_context);
}

static void TestTurnedBox(void)
{
    // The turning square of TestTurning, without walls, in an instance
    // turned by 0 and by 0.5 about Z: the world's particles in a turned box
    // keep the same share of their turn, within 0.5%.
    double kept[2];
    for (int32_t c = 0; c < 2; ++c)
    {
        s_turn = c == 1 ? 0.5f : 0.0f;
        mvfxGasDef gas = Gas(24, 24, 0.05f);
        mvfxEmitterDef emitter = Emitter(0.025f, NULL, 576);
        CHECK(Open(&emitter, 1, &gas, 1), "opened");
        Block(0.05f, 2, (const int32_t[2]){6, 6}, (const int32_t[2]){18, 18}, Turning);
        double start = AngularMomentum();
        Steps(120);
        kept[c] = AngularMomentum() / start;
        mvfxDestroyContext(s_context);
    }
    s_turn = 0.0f;
    CHECK(kept[0] >= 0.85 && fabs(kept[1] - kept[0]) <= 0.005 * kept[0],
          "a turned box keeps a turn as an upright one");
}

// A shear of 1 a second: X's velocity rising with Y from 0.4.
static void Shear(const double* place, float* out)
{
    out[0] = (float)(place[1] - 0.4);
}

// The worst of a shear's velocity at the particles 2 cells or more inside
// its block, turned by cs and sn.
static float ShearMiss(float cs, float sn)
{
    float worst = 0.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        float wx = At(i)->position[0];
        float wy = At(i)->position[1];
        float x = cs * wx + sn * wy;
        float y = -sn * wx + cs * wy;
        if (x > 0.3f && x < 0.5f && y > 0.3f && y < 0.5f)
        {
            float vx = At(i)->velocity[0];
            float vy = At(i)->velocity[1];
            worst = fmaxf(worst, fabsf(cs * vx + sn * vy - (y - 0.4f)));
            worst = fmaxf(worst, fabsf(-sn * vx + cs * vy));
        }
    }
    return worst;
}

static void TestShear(void)
{
    // A block of 8 by 8 cells of 0.05 in shear, 2 by 2 particles a cell, no
    // gravity, in an instance turned by 0 and by 0.5 about Z: after a step,
    // each particle 2 cells or more inside it has the shear's velocity at
    // its place within 1e-4 (a linear field is carried exactly), and after
    // a second, carried from the gradient each particle took, still.
    float worst = 0.0f;
    for (int32_t c = 0; c < 2; ++c)
    {
        s_turn = c == 1 ? 0.5f : 0.0f;
        float cs = cosf(s_turn);
        float sn = sinf(s_turn);
        mvfxGasDef gas = Gas(16, 16, 0.05f);
        mvfxEmitterDef emitter = Emitter(0.025f, NULL, 256);
        CHECK(Open(&emitter, 1, &gas, 1), "opened");
        Block(0.05f, 2, (const int32_t[2]){4, 4}, (const int32_t[2]){12, 12}, Shear);
        for (int32_t s = 0; s < 2; ++s)
        {
            Steps(1);
            worst = fmaxf(worst, ShearMiss(cs, sn));
        }
        mvfxDestroyContext(s_context);
    }
    s_turn = 0.0f;
    CHECK(worst <= 1e-4f, "a shear is carried exactly");
}

// Gas 0's faces along an axis, count of them.
static const float* Faces(int32_t axis, int64_t count)
{
    static float faces[2][1024];
    CHECK(mvfxTestReadGasFaces(s_context, s_instance, 0, axis, faces[axis], 1024) == count,
          "faces");
    return faces[axis];
}

// A block of 2 by 2 cells of 0.1 filled four times over, spawned in a
// tick into a gas of a fill time: the worst of its cells' faces'
// divergence over 3 / fillTime, less 1.
static double Growth(float fillTime)
{
    mvfxGasDef gas = Gas(8, 8, 0.1f);
    gas.fillTime = fillTime;
    mvfxCollider box = BoxOf(&gas);
    mvfxEmitterDef emitter = Emitter(0.05f, &box, 64);
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    Block(0.1f, 4, (const int32_t[2]){3, 3}, (const int32_t[2]){5, 5}, NULL);
    const float* u = Faces(0, 72);
    const float* v = Faces(1, 72);
    double worst = 0.0;
    for (int32_t c = 0; c < 4; ++c)
    {
        int32_t x = 3 + c % 2;
        int32_t y = 3 + c / 2;
        double out = (double)u[y * 9 + x + 1] - (double)u[y * 9 + x] + (double)v[(y + 1) * 8 + x] -
                     (double)v[y * 8 + x];
        worst = fmax(worst, fabs(out / 0.1 * (double)fillTime / 3.0 - 1.0));
    }
    mvfxDestroyContext(s_context);
    return worst;
}

// The steps a planned instance's plans of a step of ticks hold.
static int32_t PlanSteps(int64_t ticks)
{
    CHECK(mvfxContext_StepTicks(s_context, ticks) == mvfx_success, "step");
    int32_t steps = 0;
    CHECK(mvfxInstance_GetPlanSteps(s_context, s_instance, &steps) == mvfx_success, "plan steps");
    return steps;
}

// One particle at 2.5 a second along X under gravity 9.81, in cells of
// 0.05 at a courant number, stepped 1/30 s in context steps of 1/30 s
// over steps: its record.
static TestParticle Thrown(float courant, int32_t steps)
{
    mvfxGasDef gas = Gas(32, 32, 0.05f);
    gas.courant = courant;
    mvfxEmitterDef emitter = Emitter(0.025f, NULL, 4);
    emitter.motion.gravity[1] = -9.81f;
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    const double at[3] = {0.4, 1.2, 0.0};
    const float velocity[3] = {2.5f, 0.0f, 0.0f};
    Spawn(at, velocity, 1);
    StepsAt(steps, 30 * steps);
    CHECK(Count() == 1, "thrown");
    TestParticle thrown = *At(0);
    mvfxDestroyContext(s_context);
    return thrown;
}

static void TestSubsteps(void)
{
    // One particle at 3 a second along X in cells of 0.05, no gravity, in
    // a planned instance told its speed: a step of 1/30 s crosses 2 cells,
    // so 2 substeps at courant 1, 4 at 0.5, 1 at 2; at 300 a second, the
    // most. With gravity 9.81, Bridson's sqrt(5 h g) = 1.566 more, 3.04
    // cells: 4 substeps at 1. A smoke gas asks none.
    const float courants[4] = {1.0f, 0.5f, 2.0f, 1.0f};
    const float speeds[5] = {3.0f, 3.0f, 3.0f, 300.0f, 3.0f};
    const int32_t expected[5] = {2, 4, 1, MVFX_MAX_LIQUID_SUBSTEPS, 4};
    // Simulated, the particle and gravity cross 2.71 cells in 1/30 s: 2
    // substeps at courant 2, the same bits as two steps of 1/60 s (1.36
    // cells each, 1 substep); 1 at courant 8, which differ.
    TestParticle halves = Thrown(2.0f, 2);
    TestParticle split = Thrown(2.0f, 1);
    TestParticle whole = Thrown(8.0f, 1);
    CHECK(memcmp(&split, &halves, sizeof split) == 0 && memcmp(&whole, &halves, sizeof whole) != 0,
          "a simulated liquid's substeps by its particles' speed");
    int32_t wrong = 0;
    s_planned = true;
    for (int32_t c = 0; c < 5; ++c)
    {
        mvfxGasDef gas = Gas(32, 32, 0.05f);
        gas.courant = courants[c % 4];
        mvfxEmitterDef emitter = Emitter(0.025f, NULL, 4);
        emitter.motion.gravity[1] = c == 4 ? -9.81f : 0.0f;
        CHECK(Open(&emitter, 1, &gas, 1), "opened");
        const double at[3] = {0.0, 0.0, 0.0};
        const mvfxSpawnRequest request = {.count = 1, .positions = at};
        CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
        CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "born");
        CHECK(mvfxInstance_SetLiquidSpeed(s_context, s_instance, 0, 1, speeds[c]) == mvfx_success,
              "told");
        wrong += PlanSteps(MVFX_TICKS_PER_SECOND / 30) != expected[c];
        mvfxDestroyContext(s_context);
    }
    CHECK(wrong == 0, "a liquid's substeps by its courant number");
    mvfxGasDef smoke = mvfxDefaultGasDef();
    smoke.size[0] = 8;
    smoke.size[1] = 8;
    mvfxEmitterDef plain = mvfxDefaultEmitterDef();
    CHECK(Open(&plain, 1, &smoke, 1), "opened");
    s_planned = false;
    CHECK(PlanSteps(MVFX_TICKS_PER_SECOND) == 1, "none");
    mvfxDestroyContext(s_context);
}

// A dam break's column in its own context, its instance of seed 1 seen or
// throttled unseen every 64th step.
typedef struct Twin
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Twin;

static Twin OpenTwin(bool throttled)
{
    mvfxGasDef gas = Gas(32, 16, 0.05f);
    mvfxCollider box = BoxOf(&gas);
    mvfxEmitterDef emitter = Emitter(0.025f, &box, 576);
    emitter.motion.gravity[1] = -9.81f;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    Twin twin = {0};
    CHECK(mvfxCreateContext(&contextDef, &twin.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    effectDef.gases = &gas;
    effectDef.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(twin.context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    def.seed = 1u;
    def.offscreen = throttled ? mvfx_offscreenThrottle : mvfx_offscreenSimulate;
    def.offscreenInterval = 64;
    CHECK(mvfxCreateInstance(twin.context, &def, &twin.instance) == mvfx_success, "twin");
    s_context = twin.context;
    s_instance = twin.instance;
    // Born seen, then unseen: its held ticks from the next step.
    Block(0.05f, 2, (const int32_t[2]){0, 0}, (const int32_t[2]){12, 12}, NULL);
    CHECK(mvfxInstance_SetVisible(twin.context, twin.instance, !throttled) == mvfx_success, "seen");
    return twin;
}

// Whether two twins' particles are where each other's are, as old and
// moving as each other's.
static bool Alike(const Twin* a, const Twin* b)
{
    static TestParticle x[576];
    static TestParticle y[576];
    int32_t count = ReadParticles(a->context, a->instance, 0, x, 576);
    bool alike = count > 0 && count == ReadParticles(b->context, b->instance, 0, y, 576);
    return alike && memcmp(x, y, (size_t)count * sizeof x[0]) == 0;
}

static void TestSubstepPaths(void)
{
    // A prewarm of a second in steps of 1/32 takes the substeps the
    // context's steps take; so does a throttled instance stepping the
    // second it held, against a seen one stepping a second at once.
    Twin prewarmed = OpenTwin(false);
    Twin stepped = OpenTwin(false);
    CHECK(mvfxInstance_Prewarm(prewarmed.context, prewarmed.instance, 1.0, 1.0 / 32.0, 32, NULL) ==
              mvfx_success,
          "prewarmed");
    for (int32_t s = 0; s < 32; ++s)
    {
        CHECK(mvfxContext_StepTicks(stepped.context, MVFX_TICKS_PER_SECOND / 32) == mvfx_success,
              "stepped");
    }
    CHECK(Alike(&prewarmed, &stepped), "a prewarm in substeps");
    mvfxDestroyContext(prewarmed.context);
    mvfxDestroyContext(stepped.context);
    Twin held = OpenTwin(true);
    Twin whole = OpenTwin(false);
    for (int32_t s = 0; s < 33; ++s)
    {
        CHECK(mvfxContext_StepTicks(held.context, MVFX_TICKS_PER_SECOND / 32) == mvfx_success,
              "held");
    }
    CHECK(mvfxContext_StepTicks(whole.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "whole");
    CHECK(Alike(&held, &whole), "held ticks in substeps");
    mvfxDestroyContext(held.context);
    mvfxDestroyContext(whole.context);
}

static void TestExpansion(void)
{
    // A block of 2 by 2 cells of 0.1 filled four times over (4 by 4
    // particles a cell at spacing 0.05), spawned in a tick: the projection
    // grows each cell back to full over 1/60 s, its faces' divergence
    // within 5% of 3 * 60. Then, in a closed box, it spreads without
    // flying apart: none faster than 3, and none faster than 0.05 after
    // 0.5 s.
    mvfxGasDef gas = Gas(8, 8, 0.1f);
    mvfxCollider box = BoxOf(&gas);
    mvfxEmitterDef emitter = Emitter(0.05f, &box, 64);
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    Block(0.1f, 4, (const int32_t[2]){3, 3}, (const int32_t[2]){5, 5}, NULL);
    const float* u = Faces(0, 72);
    const float* v = Faces(1, 72);
    bool grows = true;
    for (int32_t y = 3; y < 5; ++y)
    {
        for (int32_t x = 3; x < 5; ++x)
        {
            double out = (double)u[y * 9 + x + 1] - (double)u[y * 9 + x] +
                         (double)v[(y + 1) * 8 + x] - (double)v[y * 8 + x];
            double divergence = out / 0.1;
            grows = grows && fabs(divergence / 60.0 - 3.0) <= 0.15;
        }
    }
    CHECK(grows, "an over-full cell grows back to full");
    float fastest = 0.0f;
    for (int32_t k = 0; k < 60; ++k)
    {
        Steps(1);
        fastest = fmaxf(fastest, MaxSpeed());
    }
    CHECK(Count() == 64 && fastest < 3.0f && MaxSpeed() < 0.05f,
          "an over-full block spreads calmly");
    mvfxDestroyContext(s_context);
    CHECK(Growth(0.05f) <= 0.05, "over the gas's fill time");
}

// The surge front of Martin and Moyce's square column (1952, part IV,
// table 1, n^2 = 1, the mean of the runs with a = 2.25 in): Z = z / a
// against T = t (g / a)^(1/2), their times set so that Z is 1.44 at T 0.80.
static const double s_damZ[] = {1.67, 1.89, 2.11, 2.33, 2.56, 2.78, 3.00, 3.22,
                                3.44, 3.67, 3.89, 4.11, 4.33, 4.56, 4.78, 5.00};
static const double s_damT[] = {0.97, 1.14, 1.29, 1.45, 1.62, 1.76, 1.93, 2.07,
                                2.24, 2.40, 2.54, 2.71, 2.87, 3.04, 3.21, 3.32};

// The front over a: the farthest particle with two others within 1.5
// spacings (a lone particle ahead is spray), plus half a spacing.
static double Front(double a, double spacing)
{
    float front = 0.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        const float* p = At(i)->position;
        int32_t near = 0;
        for (int32_t j = 0; j < Count() && near < 2; ++j)
        {
            float dx = p[0] - At(j)->position[0];
            float dy = p[1] - At(j)->position[1];
            near += j != i && dx * dx + dy * dy < (float)(2.25 * spacing * spacing) ? 1 : 0;
        }
        front = near >= 2 ? fmaxf(front, p[0]) : front;
    }
    return ((double)front + 0.5 * spacing) / a;
}

// The liquid's volume: its density summed over the cells, times a cell's
// area.
static double Volume(int64_t cells, float cell)
{
    static float density[96 * 40];
    CHECK(mvfxInstance_ReadGas(s_context, s_instance, 0, mvfx_gasDensity, density, cells) ==
              mvfx_success,
          "read");
    double sum = 0.0;
    for (int64_t c = 0; c < cells; ++c)
    {
        sum += (double)density[c];
    }
    return sum * (double)cell * (double)cell;
}

// A dam break's measures at a rate of steps a second: its front's worst
// error against Martin and Moyce's to Z 3, its speed from their Z 3.22 to
// 4.78 with theirs, its volume's worst error, and whether it was timed.
typedef struct Dam
{
    double worst;
    double speed;
    double measured;
    double volume;
    bool timed;
} Dam;

// The front at time t from the stepped times and fronts (-1 past them).
static double FrontAt(const double* times, const double* zs, int32_t steps, double t)
{
    for (int32_t k = 1; k < steps; ++k)
    {
        if (times[k - 1] <= t && times[k] >= t)
        {
            double share = (t - times[k - 1]) / (times[k] - times[k - 1]);
            return zs[k - 1] + share * (zs[k] - zs[k - 1]);
        }
    }
    return -1.0;
}

static Dam DamBreak(int32_t rate)
{
    mvfxGasDef gas = Gas(96, 40, 0.05f);
    mvfxCollider box = BoxOf(&gas);
    mvfxEmitterDef emitter = Emitter(0.025f, &box, 576);
    emitter.motion.gravity[1] = -9.81f;
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    Block(0.05f, 2, (const int32_t[2]){0, 0}, (const int32_t[2]){12, 12}, NULL);
    const double a = 0.6;
    static double times[240];
    static double zs[240];
    double unit = sqrt(9.81 / a);
    Dam dam = {0};
    for (int32_t s = 0; s < rate; ++s)
    {
        StepsAt(1, rate);
        times[s] = (double)(s + 1) / (double)rate * unit;
        zs[s] = Front(a, 0.025);
        dam.volume = fmax(dam.volume, fabs(Volume(96 * 40, 0.05f) / (a * a) - 1.0));
    }
    // Their times set so that Z is 1.44 at T 0.80: the shift that does.
    double shift = 0.0;
    for (int32_t k = 1; k < rate && shift == 0.0; ++k)
    {
        double share = (1.44 - zs[k - 1]) / (zs[k] - zs[k - 1]);
        bool crosses = zs[k - 1] < 1.44 && zs[k] >= 1.44;
        shift = crosses ? 0.80 - (times[k - 1] + share * (times[k] - times[k - 1])) : 0.0;
    }
    double z[16];
    for (int32_t m = 0; m < 16; ++m)
    {
        z[m] = FrontAt(times, zs, rate, s_damT[m] - shift);
        dam.worst =
            s_damZ[m] <= 3.0 ? fmax(dam.worst, fabs(z[m] - s_damZ[m]) / s_damZ[m]) : dam.worst;
    }
    // From T 2.07 to 3.21 (their Z 3.22 to 4.78).
    dam.speed = (z[14] - z[7]) / (s_damT[14] - s_damT[7]);
    dam.measured = (s_damZ[14] - s_damZ[7]) / (s_damT[14] - s_damT[7]);
    dam.timed = shift != 0.0;
    mvfxDestroyContext(s_context);
    return dam;
}

// A pour of 1 s from a small sphere at the box's middle, rate a second,
// the emitter holding capacity: its particles' places into places (two
// floats each), their count returned.
static int32_t Pour(int32_t capacity, float rate, float* places)
{
    mvfxGasDef gas = Gas(16, 16, 0.1f);
    mvfxCollider box = BoxOf(&gas);
    mvfxEmitterDef emitter = Emitter(0.05f, &box, capacity);
    emitter.rate = rate;
    emitter.shape.radius = 0.1f;
    emitter.velocity.speed = (mvfxRange){0.2f, 0.5f};
    emitter.motion.gravity[1] = -9.81f;
    s_origin[0] = (double)gas.center[0];
    s_origin[1] = (double)gas.center[1];
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    s_origin[0] = 0.0;
    s_origin[1] = 0.0;
    Steps(120);
    int32_t count = Count();
    for (int32_t i = 0; i < count; ++i)
    {
        places[2 * i] = At(i)->position[0];
        places[2 * i + 1] = At(i)->position[1];
    }
    mvfxDestroyContext(s_context);
    return count;
}

// Two instances in one context of an effect with a liquid gas of a
// courant number, an emitter whose lives, longer than a step, write
// their deaths, and one spawning two children a death: the effects into
// effects, the instances into out.
static void OpenTwins(const float courant[2], mvfxEffectId effects[2], mvfxInstanceId out[2])
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    for (int32_t k = 0; k < 2; ++k)
    {
        mvfxGasDef gas = Gas(8, 8, 0.1f);
        gas.courant = courant[k];
        mvfxEmitterDef emitters[3] = {Emitter(0.05f, NULL, 64), mvfxDefaultEmitterDef(),
                                      mvfxDefaultEmitterDef()};
        emitters[0].motion.gravity[1] = -9.81f;
        emitters[1].rate = 600.0f;
        emitters[1].lifetime = mvfxMakeRangeSource(0.02f, 0.05f);
        emitters[1].events = mvfx_eventMaskDeath;
        emitters[1].eventCapacity = 64;
        // Two children a death, from their step's deaths alone.
        emitters[2].rate = 0.0f;
        emitters[2].burstCount = 0;
        emitters[2].capacity = 512;
        emitters[2].lifetime = mvfxMakeRangeSource(1.0f, 1.0f);
        emitters[2].eventSpawn = (mvfxEventSpawn){.enable = true,
                                                  .source = 1,
                                                  .kind = mvfx_eventDeath,
                                                  .minCount = 2,
                                                  .maxCount = 2,
                                                  .capacity = 64};
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.emitters = emitters;
        def.emitterCount = 3;
        def.gases = &gas;
        def.gasCount = 1;
        CHECK(mvfxCreateEffect(s_context, &def, &effects[k], NULL) == mvfx_success, "effect");
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        instanceDef.effect = effects[k];
        // One seed: the same draws.
        instanceDef.seed = 7;
        CHECK(mvfxCreateInstance(s_context, &instanceDef, &out[k]) == mvfx_success, "instance");
    }
}

// An event's tick and serial as one key, for the order of a sort.
static int CompareEvents(const void* a, const void* b)
{
    const mvfxEvent* x = a;
    const mvfxEvent* y = b;
    if (x->tick != y->tick)
    {
        return x->tick < y->tick ? -1 : 1;
    }
    return x->serial < y->serial ? -1 : (x->serial > y->serial ? 1 : 0);
}

// Whether two instances' events of a step are the same ones, whatever
// their order.
static bool SameEvents(const mvfxEvent* const events[2], const int32_t counts[2])
{
    static mvfxEvent sorted[2][64];
    if (counts[0] != counts[1] || counts[0] > 64)
    {
        return false;
    }
    for (int32_t k = 0; k < 2; ++k)
    {
        if (counts[k] > 0)
        {
            memcpy(sorted[k], events[k], sizeof(mvfxEvent) * (size_t)counts[k]);
            qsort(sorted[k], (size_t)counts[k], sizeof(mvfxEvent), CompareEvents);
        }
    }
    bool same = true;
    for (int32_t e = 0; same && e < counts[0]; ++e)
    {
        same = sorted[0][e].serial == sorted[1][e].serial &&
               sorted[0][e].tick == sorted[1][e].tick && sorted[0][e].kind == sorted[1][e].kind;
    }
    return same;
}

static void TestSplitEvents(void)
{
    // An instance whose steps split into substeps writes the deaths of
    // the whole context step, those of one that does not split (births
    // and deaths fall on the same ticks; the order differs, deaths by age
    // coming at a step's start), not its last substep's alone; the
    // children of those deaths, each born from its own substep's. A
    // planned instance of the split one's effect shows it splits.
    const float courant[2] = {4.0f, 0.05f};
    mvfxEffectId effects[2];
    mvfxInstanceId twins[2];
    OpenTwins(courant, effects, twins);
    mvfxInstanceDef planDef = mvfxDefaultInstanceDef();
    planDef.effect = effects[1];
    planDef.seed = 7;
    planDef.planned = true;
    CHECK(mvfxCreateInstance(s_context, &planDef, &s_instance) == mvfx_success, "planned");
    bool same = true;
    int32_t deaths = 0;
    int32_t most = 0;
    for (int32_t s = 0; s < 30; ++s)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success, "step");
        const mvfxEvent* events[2] = {NULL, NULL};
        int32_t counts[2] = {0, 0};
        for (int32_t k = 0; k < 2; ++k)
        {
            CHECK(mvfxInstance_GetEvents(s_context, twins[k], 1, &events[k], &counts[k]) ==
                      mvfx_success,
                  "events");
        }
        mvfxEmitterStats children[2];
        for (int32_t k = 0; k < 2; ++k)
        {
            CHECK(mvfxInstance_GetEmitterStats(s_context, twins[k], 2, &children[k]) ==
                      mvfx_success,
                  "children");
        }
        same = same && SameEvents(events, counts) && children[0].live == children[1].live;
        deaths += counts[0];
        int32_t substeps = 0;
        CHECK(mvfxInstance_GetPlanSteps(s_context, s_instance, &substeps) == mvfx_success, "split");
        most = substeps > most ? substeps : most;
    }
    mvfxDestroyContext(s_context);
    CHECK(same && deaths > 100 && most > 1, "a split step's events are the whole step's");
}

static void TestCapacity(void)
{
    // A pour's births join the liquid while its first particles carry
    // affine motion: the same particles at the same places whatever room
    // the emitter keeps (each matrix is its particle's, not a span of the
    // attributes' arrays).
    static float small[2 * 400];
    static float large[2 * 400];
    int32_t count = Pour(300, 240.0f, small);
    bool same = count > 200 && Pour(400, 240.0f, large) == count &&
                memcmp(small, large, sizeof(float) * 2u * (size_t)count) == 0;
    CHECK(same, "a grid liquid's step whatever its emitter's room");
}

static void TestDamBreak(void)
{
    // A square column of side a = 0.6 in the corner of a closed box 4.8 by
    // 2, cells of 0.05 with 2 by 2 particles each, stepped at 30, 60, 120
    // and 240 a second (the slower in substeps by the courant number 1):
    // its front, timed as Martin and Moyce's were, is within 11% of theirs
    // from Z 1.67 to 3 (10.0, 9.6, 9.4 and 7.0%, measured: the scheme's
    // error in time, first order); from their Z 3.22 to 4.78 it is no
    // slower than theirs (their floor slowed their water) and no faster
    // than Ritter's frictionless surge, 2 (g a)^(1/2), with 5% for the
    // measure (Z gains at most 2.1 a unit of T); its volume, the cells'
    // filled shares, is within 5% of the column's at every step (4.0, 4.0,
    // 3.8 and 4.7%). Stepped at 30 a second in one step each, it lost 8%.
    const int32_t rates[4] = {30, 60, 120, 240};
    for (int32_t r = 0; r < 4; ++r)
    {
        Dam dam = DamBreak(rates[r]);
        CHECK(dam.timed && dam.worst <= 0.11,
              "a grid dam break's front as Martin and Moyce measured");
        CHECK(dam.speed >= dam.measured && dam.speed <= 2.1,
              "a grid dam break's front within Ritter's speed");
        CHECK(dam.volume <= 0.05, "a grid liquid keeps its volume");
    }
}

int main(void)
{
    TestRefusals();
    TestFill();
    TestOutside();
    TestPool();
    TestObstacle();
    TestTurning();
    TestTurnedBox();
    TestShear();
    TestExpansion();
    TestDamBreak();
    TestCapacity();
    TestSplitEvents();
    TestSubsteps();
    TestSubstepPaths();
    return s_failures;
}
