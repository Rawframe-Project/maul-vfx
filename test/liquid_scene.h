// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the liquid tests share: a planar liquid's emitter, an instance of
// it opened with particles spawned still, its places, velocities and count
// as the output writes them, steps of 1/60 s, and the kernels as their
// formulas give them in binary64.

#ifndef MAUL_VFX_TEST_LIQUID_SCENE_H
#define MAUL_VFX_TEST_LIQUID_SCENE_H

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <math.h>

#define MOST 1024

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// Steps taken, and the step the records below were read after.
static uint64_t s_steps;
static uint64_t s_read = UINT64_MAX;
static int32_t s_count;
static float s_places[3][MOST];
static float s_velocities[3][MOST];

// A planar liquid's emitter: host spawns only, long lives, no gravity.
static inline mvfxEmitterDef Emitter(float spacing)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = MOST;
    emitter.hostSpawnCapacity = MOST;
    emitter.lifetime = mvfxMakeRangeSource(1000.0f, 1000.0f);
    emitter.liquid.enable = true;
    emitter.liquid.planar = true;
    emitter.liquid.spacing = spacing;
    return emitter;
}

// Opens an instance of the emitter and spawns particles at places with
// velocities (NULL for still).
static inline void OpenWith(const mvfxEmitterDef* emitter, const double* places,
                            const float* velocities, int32_t count)
{
    ++s_steps;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    mvfxSpawnRequest request = {.count = count, .positions = places, .velocities = velocities};
    CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
    // The spawns are born in the first step, and join the liquid from the
    // next.
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "born");
}

// Opens an instance of the emitter and spawns particles at places, still.
static inline void Open(const mvfxEmitterDef* emitter, const double* places, int32_t count)
{
    OpenWith(emitter, places, NULL, count);
}

// Reads the emitter's records again if a step was taken since.
static inline void Read(void)
{
    if (s_read == s_steps)
    {
        return;
    }
    static TestParticle particles[MOST];
    s_count = ReadParticles(s_context, s_instance, 0, particles, MOST);
    CHECK(s_count >= 0, "records");
    for (int32_t i = 0; i < s_count; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            s_places[k][i] = particles[i].position[k];
            s_velocities[k][i] = particles[i].velocity[k];
        }
    }
    s_read = s_steps;
}

// The emitter's places, by axis.
static inline const float* Places(int32_t k)
{
    Read();
    return s_places[k];
}

// The emitter's velocities, by axis.
static inline const float* Velocities(int32_t k)
{
    Read();
    return s_velocities[k];
}

static inline int32_t Count(void)
{
    Read();
    return s_count;
}

// A step of ticks.
static inline void Step(int64_t ticks)
{
    CHECK(mvfxContext_StepTicks(s_context, ticks) == mvfx_success, "step");
    ++s_steps;
}

// Steps of 1/60 s.
static inline void Steps(int32_t n)
{
    for (int32_t s = 0; s < n; ++s)
    {
        Step(MVFX_TICKS_PER_SECOND / 60);
    }
}

// A liquid's kernels at a scale, from their formulas in binary64: the
// radius, poly6's factor 315 / (64 pi h^9) (4 / (pi h^8) planar) and the
// spiky gradient's 45 / (pi h^6) (30 / (pi h^5)); over the ideal lattice
// about a particle, the rest density's inverse and the squared gradients'
// sum, which give the softening and the tensile correction's scale; the
// tensile correction's reference kernel's inverse.
typedef struct Kernel
{
    double radius;
    double square;
    double poly6;
    double spiky;
    double restInverse;
    double softening;
    double tensileInverse;
    double tensileScale;
} Kernel;

static inline double Poly6(const Kernel* kernel, double square)
{
    double d = kernel->square - square;
    return d > 0.0 ? kernel->poly6 * d * d * d : 0.0;
}

// The spiky gradient's size at a length.
static inline double Spiky(const Kernel* kernel, double length)
{
    double d = kernel->radius - length;
    return d > 0.0 && length > 0.0 ? kernel->spiky * d * d : 0.0;
}

static inline Kernel KernelOf(const mvfxLiquidDef* def, double scale)
{
    const double pi = 3.14159265358979323846;
    double spacing = (double)def->spacing * scale;
    Kernel kernel = {.radius = spacing * (double)def->kernelScale};
    double h = kernel.radius;
    kernel.square = h * h;
    kernel.poly6 = def->planar ? 4.0 / (pi * pow(h, 8)) : 315.0 / (64.0 * pi * pow(h, 9));
    kernel.spiky = def->planar ? 30.0 / (pi * pow(h, 5)) : 45.0 / (pi * pow(h, 6));
    int32_t reach = (int32_t)ceil((double)def->kernelScale);
    int32_t layers = def->planar ? 0 : reach;
    double rest = 0.0;
    double gradients = 0.0;
    for (int32_t c = -layers; c <= layers; ++c)
    {
        for (int32_t b = -reach; b <= reach; ++b)
        {
            for (int32_t a = -reach; a <= reach; ++a)
            {
                double square = spacing * spacing * (double)(a * a + b * b + c * c);
                double g = Spiky(&kernel, sqrt(square));
                rest += Poly6(&kernel, square);
                gradients += g * g;
            }
        }
    }
    kernel.restInverse = 1.0 / rest;
    double full = gradients * kernel.restInverse * kernel.restInverse;
    kernel.softening = (double)def->relaxation * full;
    kernel.tensileScale = (double)def->tensile / full;
    double reference = (double)def->tensileDistance * kernel.radius;
    kernel.tensileInverse = 1.0 / Poly6(&kernel, reference * reference);
    return kernel;
}

#endif
