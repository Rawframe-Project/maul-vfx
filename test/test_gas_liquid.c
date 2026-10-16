// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Grid liquids (APIC on a liquid gas): the refusals; a cell's filled share
// as its density; a still pool staying still, its pressure rising
// linearly with depth; a turning disk keeping its angular momentum with
// APIC better than with PIC; a dam break's front against Martin and
// Moyce's measurements and Ritter's frictionless speed, its volume kept.

#include "gas.h"
#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;
// The next instance's origin and turn about Z (radians).
static double s_origin[3];
static float s_turn;

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
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.liquid.enabled = true;
    emitter.liquid.solver = mvfx_liquidGrid;
    emitter.liquid.gas = 0;
    emitter.liquid.spacing = spacing;
    emitter.collision.enabled = box != NULL;
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
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = emitterCount;
    def.gases = gases;
    def.gasCount = gasCount;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(s_context, &def, &effect) != mvfx_success)
    {
        return false;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    memcpy(instanceDef.origin, s_origin, sizeof s_origin);
    instanceDef.rotation[2] = sinf(0.5f * s_turn);
    instanceDef.rotation[3] = cosf(0.5f * s_turn);
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
}

// Steps of 1/120 s.
static void Steps(int32_t n)
{
    for (int32_t s = 0; s < n; ++s)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 120) == mvfx_success,
              "step");
    }
}

static float* Array(mvfxAttribute attribute)
{
    return mvfxAttributeArray(mvfxContextInstance(s_context, s_instance), 0, attribute);
}

static int32_t Count(void)
{
    return mvfxContextInstance(s_context, s_instance)->emitters[0].count;
}

static void TestRefusals(void)
{
    // A liquid gas with a source, a force or two substeps; a grid liquid on
    // a smoke gas or on no gas; two grid liquids on one gas; a liquid gas
    // no emitter names; an emitter splatting into a liquid gas. Then the
    // valid pair is taken.
    const mvfxGasSource source = {.shape = mvfx_gasSphere, .radius = 0.1f, .density = 1};
    const mvfxForce force = {.kind = mvfx_forceConstant, .strength = 1, .vector = {1, 0, 0}};
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
        emitters[1].liquid.enabled = c == 5;
        emitters[0].liquid.enabled = c != 6;
        emitters[1].gasSplat = (mvfxGasSplat){.enabled = c == 7, .gas = 0, .density = 1};
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
    CHECK(Array(mvfx_attributeVelocityX)[1] == 1.0f && Array(mvfx_attributeVelocityY)[1] == 0.0f,
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
        float vx = Array(mvfx_attributeVelocityX)[i];
        float vy = Array(mvfx_attributeVelocityY)[i];
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
        top = fmaxf(top, Array(mvfx_attributePositionY)[i]);
        low = fminf(low, Array(mvfx_attributePositionY)[i]);
    }
    const mvfxGasState* state = &mvfxContextInstance(s_context, s_instance)->gases[0];
    const float* p = state->levels[0].pressure;
    float rises[6];
    float mean = 0.0f;
    for (int32_t y = 0; y < 6; ++y)
    {
        rises[y] = p[y * 16 + 8] - p[(y + 1) * 16 + 8];
        mean += rises[y] / 6.0f;
    }
    bool linear = mean > 0.0f;
    for (int32_t y = 0; y < 6; ++y)
    {
        linear = linear && fabsf(rises[y] - mean) <= 0.1f * mean;
    }
    CHECK(Count() == 512 && MaxSpeed() < 0.2f && top < 0.85f && low >= 0.0f, "a pool stays still");
    CHECK(linear, "a pool's pressure rises with depth");
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
        double x = (double)Array(mvfx_attributePositionX)[i] - (c - s) * 0.6;
        double y = (double)Array(mvfx_attributePositionY)[i] - (s + c) * 0.6;
        sum += x * (double)Array(mvfx_attributeVelocityY)[i] -
               y * (double)Array(mvfx_attributeVelocityX)[i];
    }
    return sum;
}

static void TestTurning(void)
{
    // A square of 12 by 12 cells of 0.05 turning at 2 a second about the
    // middle of a closed box of 24 by 24, 2 by 2 particles a cell, no
    // gravity, 1 s: APIC keeps at least 85% of its angular momentum; PIC
    // (the affine matrices zeroed each step) keeps less.
    double kept[2];
    for (int32_t c = 0; c < 2; ++c)
    {
        mvfxGasDef gas = Gas(24, 24, 0.05f);
        mvfxCollider box = BoxOf(&gas);
        mvfxEmitterDef emitter = Emitter(0.025f, &box, 576);
        CHECK(Open(&emitter, 1, &gas, 1), "opened");
        Block(0.05f, 2, (const int32_t[2]){6, 6}, (const int32_t[2]){18, 18}, Turning);
        double start = AngularMomentum();
        for (int32_t s = 0; s < 120; ++s)
        {
            Steps(1);
            float* affine = Array(mvfx_attributeAffine);
            for (int32_t i = 0; c == 1 && i < 9 * Count(); ++i)
            {
                affine[i] = 0.0f;
            }
        }
        kept[c] = AngularMomentum() / start;
        mvfxDestroyContext(s_context);
    }
    CHECK(kept[0] >= 0.85 && kept[1] < kept[0], "APIC keeps a turn better than PIC");
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

static void TestShear(void)
{
    // A block of 8 by 8 cells of 0.05 in shear, 2 by 2 particles a cell, no
    // gravity: after a step, each particle 2 cells or more inside it has
    // the shear's velocity at its place within 1e-4 (a linear field is
    // carried exactly).
    mvfxGasDef gas = Gas(16, 16, 0.05f);
    mvfxEmitterDef emitter = Emitter(0.025f, NULL, 256);
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    Block(0.05f, 2, (const int32_t[2]){4, 4}, (const int32_t[2]){12, 12}, Shear);
    Steps(1);
    float worst = 0.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        float x = Array(mvfx_attributePositionX)[i];
        float y = Array(mvfx_attributePositionY)[i];
        if (x > 0.3f && x < 0.5f && y > 0.3f && y < 0.5f)
        {
            worst = fmaxf(worst, fabsf(Array(mvfx_attributeVelocityX)[i] - (y - 0.4f)));
            worst = fmaxf(worst, fabsf(Array(mvfx_attributeVelocityY)[i]));
        }
    }
    CHECK(worst <= 1e-4f, "a shear is carried exactly");
    mvfxDestroyContext(s_context);
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
    const mvfxGasState* state = &mvfxContextInstance(s_context, s_instance)->gases[0];
    const float* u = state->velocity[0][state->current];
    const float* v = state->velocity[1][state->current];
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
    const float* x = Array(mvfx_attributePositionX);
    const float* y = Array(mvfx_attributePositionY);
    float front = 0.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        int32_t near = 0;
        for (int32_t j = 0; j < Count() && near < 2; ++j)
        {
            float dx = x[i] - x[j];
            float dy = y[i] - y[j];
            near += j != i && dx * dx + dy * dy < (float)(2.25 * spacing * spacing) ? 1 : 0;
        }
        front = near >= 2 ? fmaxf(front, x[i]) : front;
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

static void TestDamBreak(void)
{
    // A square column of side a = 0.6 in the corner of a closed box 4.8 by
    // 2, cells of 0.05 with 2 by 2 particles each, stepped at 120 a second:
    // its front, timed as Martin and Moyce's were, is within 10% of theirs
    // from Z 1.67 to 3; from their Z 3.22 to 4.78 it is no slower than
    // theirs (their floor slowed their water) and no faster than Ritter's
    // frictionless surge, 2 (g a)^(1/2), with 5% for the measure (Z gains
    // at most 2.1 a unit of T); its volume, the cells' filled shares, is
    // within 5% of the column's at every step.
    mvfxGasDef gas = Gas(96, 40, 0.05f);
    mvfxCollider box = BoxOf(&gas);
    mvfxEmitterDef emitter = Emitter(0.025f, &box, 576);
    emitter.motion.gravity[1] = -9.81f;
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    Block(0.05f, 2, (const int32_t[2]){0, 0}, (const int32_t[2]){12, 12}, NULL);
    const double a = 0.6;
    static double times[120];
    static double zs[120];
    double unit = sqrt(9.81 / a);
    double worstVolume = 0.0;
    for (int32_t s = 0; s < 120; ++s)
    {
        Steps(1);
        times[s] = (double)(s + 1) / 120.0 * unit;
        zs[s] = Front(a, 0.025);
        worstVolume = fmax(worstVolume, fabs(Volume(96 * 40, 0.05f) / (a * a) - 1.0));
    }
    double shift = 0.0;
    for (int32_t k = 1; k < 120; ++k)
    {
        if (zs[k - 1] < 1.44 && zs[k] >= 1.44)
        {
            double share = (1.44 - zs[k - 1]) / (zs[k] - zs[k - 1]);
            shift = 0.80 - (times[k - 1] + share * (times[k] - times[k - 1]));
            break;
        }
    }
    double worst = 0.0;
    double z[16];
    for (int32_t m = 0; m < 16; ++m)
    {
        double t = s_damT[m] - shift;
        z[m] = -1.0;
        for (int32_t k = 1; k < 120; ++k)
        {
            if (times[k - 1] <= t && times[k] >= t)
            {
                double share = (t - times[k - 1]) / (times[k] - times[k - 1]);
                z[m] = zs[k - 1] + share * (zs[k] - zs[k - 1]);
                break;
            }
        }
        worst = s_damZ[m] <= 3.0 ? fmax(worst, fabs(z[m] - s_damZ[m]) / s_damZ[m]) : worst;
    }
    // From T 2.07 to 3.21 (their Z 3.22 to 4.78).
    double speed = (z[14] - z[7]) / (s_damT[14] - s_damT[7]);
    double measured = (s_damZ[14] - s_damZ[7]) / (s_damT[14] - s_damT[7]);
    CHECK(shift != 0.0 && worst <= 0.10, "a grid dam break's front as Martin and Moyce measured");
    CHECK(speed >= measured && speed <= 2.1, "a grid dam break's front within Ritter's speed");
    CHECK(worstVolume <= 0.05, "a grid liquid keeps its volume");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestRefusals();
    TestFill();
    TestOutside();
    TestPool();
    TestTurning();
    TestTurnedBox();
    TestShear();
    TestExpansion();
    TestDamBreak();
    return s_failures;
}
