// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Liquids: a plan's kernels against their formulas and the rest density
// of a lattice summed in binary64, 3D and planar, at the instance's scale;
// a lattice at rest left in place away from its edges; the refusals, the
// instance's scale, the neighbor cap's count, XSPH, confinement, a 3D
// liquid, the tensile correction's size and a planar liquid's Z. The flows
// measured against references are in test_liquid_flow.c.

#include "liquid_scene.h"

#include "maul-vfx/plan.h"

#include <math.h>
#include <string.h>

static void TestRest(void)
{
    // A square lattice of 24 by 24 at the rest spacing, one substep and one
    // iteration, no gravity: particles 6 spacings or more from its edges
    // move less than 1e-4 of the spacing in a step.
    static double places[24 * 24 * 3];
    for (int32_t i = 0; i < 24 * 24; ++i)
    {
        places[3 * i] = 0.05 * (double)(i % 24);
        places[3 * i + 1] = 0.05 * (double)(i / 24);
        places[3 * i + 2] = 0.0;
    }
    mvfxEmitterDef emitter = Emitter(0.05f);
    emitter.liquid.substeps = 1;
    emitter.liquid.iterations = 1;
    Open(&emitter, places, 24 * 24);
    Step(MVFX_TICKS_PER_SECOND / 60);
    float most = 0.0f;
    int32_t inner = 0;
    for (int32_t i = 0; i < Count(); ++i)
    {
        int32_t x = i % 24;
        int32_t y = i / 24;
        bool deep = x >= 6 && x < 18 && y >= 6 && y < 18;
        float dx = Places(0)[i] - (float)places[3 * i];
        float dy = Places(1)[i] - (float)places[3 * i + 1];
        most = deep ? fmaxf(most, sqrtf(dx * dx + dy * dy)) : most;
        inner += deep ? 1 : 0;
    }
    CHECK(Count() == 24 * 24 && inner == 144 && most < 1e-4f * 0.05f, "a lattice at rest stays");
    mvfxDestroyContext(s_context);
}

// Opens a liquid's emitter in an instance of a scale, its particles
// spawned at places, still; false when the effect is refused.
static bool OpenScaled(const mvfxEmitterDef* emitter, const double* places, int32_t count,
                       float scale)
{
    ++s_steps;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(s_context, &def, &effect) != mvfx_success)
    {
        return false;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.scale = scale;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    mvfxSpawnRequest request = {.count = count, .positions = places};
    CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "born");
    return true;
}

static void TestRefusals(void)
{
    // Each parameter just out of its range, and a liquid of units, of
    // rigid bodies or with a beam, is refused; the defaults are taken.
    bool right = true;
    for (int32_t c = 0; c < 17; ++c)
    {
        mvfxEmitterDef e = Emitter(0.05f);
        mvfxLiquidDef* l = &e.liquid;
        l->spacing = c == 0 ? 0.0f : (c == 1 ? MVFX_MAX_NEIGHBOR_RADIUS / 2.0f * 1.01f : 0.05f);
        l->kernelScale = c == 2 ? 1.49f : (c == 3 ? 4.01f : 2.0f);
        l->substeps = c == 4 ? 0 : (c == 5 ? MVFX_MAX_SUBSTEPS + 1 : 4);
        l->iterations = c == 6 ? 0 : (c == 7 ? MVFX_MAX_LIQUID_ITERATIONS + 1 : 3);
        l->relaxation = c == 8 ? 0.0f : (c == 9 ? 1.01f : 0.01f);
        l->tensile = c == 10 ? 1.01f : 0.0f;
        l->tensileDistance = c == 11 ? 1.0f : 0.2f;
        l->tensilePower = c == 12 ? 0 : 4;
        l->viscosity = c == 13 ? -0.01f : 0.01f;
        l->vorticity = c == 14 ? -1.0f : 0.0f;
        l->maxNeighbors = c == 15 ? MVFX_MAX_NEIGHBORS + 1 : 64;
        e.rigid.enable = c == 16;
        const double place[3] = {0, 0, 0};
        bool refused = !OpenScaled(&e, place, 1, 1.0f);
        CHECK(refused, "a liquid's refusal");
        right = right && refused;
        mvfxDestroyContext(s_context);
    }
    // A valid chain, taken alone and refused as a liquid.
    mvfxEmitterDef chain = Emitter(0.05f);
    chain.capacity = 16;
    chain.hostSpawnCapacity = 4;
    chain.unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                               .points = 4,
                               .spacing = 0.5f,
                               .direction = {0, -1, 0},
                               .substeps = 8};
    const double place[3] = {0, 0, 0};
    bool refused = !OpenScaled(&chain, place, 1, 1.0f);
    right = right && refused;
    mvfxDestroyContext(s_context);
    chain.liquid.enable = false;
    bool taken = OpenScaled(&chain, place, 1, 1.0f);
    right = right && taken;
    mvfxDestroyContext(s_context);
    mvfxEmitterDef liquid = Emitter(0.05f);
    bool defaults = OpenScaled(&liquid, place, 1, 1.0f);
    right = right && defaults;
    mvfxDestroyContext(s_context);
    CHECK(right, "a liquid's refusals");
}

static void TestScale(void)
{
    // A lattice at spacing 0.05 in the world, in an instance of scale 2 (a
    // rest spacing of 0.1 there), is compressed and spreads; at scale 1 it
    // stays.
    static double places[8 * 8 * 3];
    for (int32_t i = 0; i < 64; ++i)
    {
        places[3 * i] = 0.05 * (double)(i % 8);
        places[3 * i + 1] = 0.05 * (double)(i / 8);
        places[3 * i + 2] = 0.0;
    }
    float spread[2];
    for (int32_t c = 0; c < 2; ++c)
    {
        mvfxEmitterDef emitter = Emitter(0.05f);
        CHECK(OpenScaled(&emitter, places, 64, c == 0 ? 1.0f : 2.0f), "opened");
        Steps(1);
        spread[c] = 0.0f;
        for (int32_t i = 0; i < Count(); ++i)
        {
            spread[c] = fmaxf(spread[c], fabsf(Places(0)[i] - (float)places[3 * i]));
        }
        mvfxDestroyContext(s_context);
    }
    CHECK(spread[0] < 1e-6f && spread[1] > 0.01f, "a liquid's lengths scale with its instance");
}

static void TestCapped(void)
{
    // Most neighbors 3 in a lattice at rest: the particles with more
    // within the kernel radius, by a float test at their places with the
    // radius formed in float, are counted once a step, the same each step.
    static double places[6 * 6 * 3];
    for (int32_t i = 0; i < 36; ++i)
    {
        places[3 * i] = 0.05 * (double)(i % 6);
        places[3 * i + 1] = 0.05 * (double)(i / 6);
        places[3 * i + 2] = 0.0;
    }
    mvfxEmitterDef emitter = Emitter(0.05f);
    emitter.liquid.maxNeighbors = 3;
    CHECK(OpenScaled(&emitter, places, 36, 1.0f), "opened");
    mvfxEmitterStats stats[2];
    for (int32_t s = 0; s < 2; ++s)
    {
        Steps(1);
        CHECK(mvfxInstance_GetEmitterStats(s_context, s_instance, 0, &stats[s]) == mvfx_success,
              "stats");
    }
    float radius = emitter.liquid.spacing * emitter.liquid.kernelScale;
    float within = radius * radius;
    uint64_t expected = 0;
    for (int32_t i = 0; i < Count(); ++i)
    {
        int32_t near = 0;
        for (int32_t j = 0; j < Count(); ++j)
        {
            float square = 0.0f;
            for (int32_t k = 0; k < 3; ++k)
            {
                float d = Places(k)[i] - Places(k)[j];
                square += d * d;
            }
            near += j != i && square < within ? 1 : 0;
        }
        expected += near > 3 ? 1u : 0u;
    }
    CHECK(expected > 0 && stats[0].neighborsCapped == expected &&
              stats[1].neighborsCapped == 2 * expected,
          "a liquid's neighbors capped and counted");
    mvfxDestroyContext(s_context);
}

static void TestViscosity(void)
{
    // Two particles 0.06 apart along X (below rest density, so no
    // correction), moving 1 and -1 along Y, one substep, viscosity 0.5:
    // each velocity is its own plus 0.5 of the difference times the poly6
    // kernel over the rest density.
    const double places[6] = {0, 0, 0, 0.06, 0, 0};
    const float velocities[6] = {0, 1, 0, 0, -1, 0};
    mvfxEmitterDef emitter = Emitter(0.05f);
    emitter.liquid.substeps = 1;
    emitter.liquid.viscosity = 0.5f;
    OpenWith(&emitter, places, velocities, 2);
    float before[2] = {Velocities(1)[0], Velocities(1)[1]};
    Step(1);
    const float* vy = Velocities(1);
    Kernel kernel = KernelOf(&emitter.liquid, 1.0);
    // The tick moves them by a hair; the kernel is read at their places
    // then.
    float dx = Places(0)[0] - Places(0)[1];
    float dy = Places(1)[0] - Places(1)[1];
    float w = (float)(Poly6(&kernel, (double)(dx * dx + dy * dy)) * kernel.restInverse);
    float expected0 = before[0] + 0.5f * ((before[1] - before[0]) * w);
    float expected1 = before[1] + 0.5f * ((before[0] - before[1]) * w);
    CHECK(fabsf(vy[0] - expected0) <= 1e-5f && fabsf(vy[1] - expected1) <= 1e-5f && w > 0.0f &&
              fabsf(vy[0] + vy[1]) <= 1e-6f,
          "XSPH viscosity blends velocities");
    mvfxDestroyContext(s_context);
}

// The kinetic energy (per unit mass) of the emitter's particles.
static double Energy(void)
{
    const float* vx = Velocities(0);
    const float* vy = Velocities(1);
    double sum = 0.0;
    for (int32_t i = 0; i < Count(); ++i)
    {
        sum += 0.5 * ((double)vx[i] * (double)vx[i] + (double)vy[i] * (double)vy[i]);
    }
    return sum;
}

static void TestVorticity(void)
{
    // A disk of radius 0.3 in a square lattice turning at 2 a second, no
    // gravity, no viscosity: after 30 steps it keeps more of its energy
    // with confinement (5 a second) than without.
    static double places[13 * 13 * 3];
    static float velocities[13 * 13 * 3];
    int32_t count = 0;
    for (int32_t i = 0; i < 13 * 13; ++i)
    {
        double x = 0.05 * (double)(i % 13 - 6);
        double y = 0.05 * (double)(i / 13 - 6);
        if (x * x + y * y <= 0.09 + 1e-9)
        {
            places[3 * count] = x;
            places[3 * count + 1] = y;
            places[3 * count + 2] = 0.0;
            velocities[3 * count] = (float)(-2.0 * y);
            velocities[3 * count + 1] = (float)(2.0 * x);
            velocities[3 * count + 2] = 0.0f;
            count += 1;
        }
    }
    double energy[2];
    for (int32_t c = 0; c < 2; ++c)
    {
        mvfxEmitterDef emitter = Emitter(0.05f);
        emitter.liquid.viscosity = 0.0f;
        emitter.liquid.vorticity = c == 0 ? 0.0f : 5.0f;
        mvfxContextDef contextDef = mvfxDefaultContextDef();
        CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.emitters = &emitter;
        def.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        instanceDef.effect = effect;
        CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
        mvfxSpawnRequest request = {.count = count, .positions = places, .velocities = velocities};
        CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
        Steps(30);
        energy[c] = Energy();
        mvfxDestroyContext(s_context);
    }
    CHECK(count > 100 && energy[1] > energy[0], "vorticity confinement keeps a swirl");
}

static void TestDeep(void)
{
    // A 3D cube of 8 particles a side at spacing 0.05 in a box 0.4 wide and
    // deep, gravity 9.81 down, 1.5 s: it stays in the box; its inner
    // particles' density (summed in binary64 over all, 3D kernels) is at
    // most 3% above 1 (the constraint bounds compression only) and at
    // least 0.9 on average.
    static double places[512 * 3];
    for (int32_t i = 0; i < 512; ++i)
    {
        places[3 * i] = 0.025 + 0.05 * (double)(i % 8);
        places[3 * i + 1] = 0.025 + 0.05 * (double)(i / 64);
        places[3 * i + 2] = 0.025 + 0.05 * (double)(i / 8 % 8);
    }
    const mvfxCollider box = {.kind = mvfx_colliderBox,
                              .inside = true,
                              .center = {0.2f, 1.0f, 0.2f},
                              .halfExtents = {0.2f, 1.0f, 0.2f},
                              .rotation = {0, 0, 0, 1}};
    mvfxEmitterDef emitter = Emitter(0.05f);
    emitter.liquid.planar = false;
    emitter.motion.gravity[1] = -9.81f;
    emitter.collision.enable = true;
    emitter.colliders = &box;
    emitter.colliderCount = 1;
    Open(&emitter, places, 512);
    Steps(90);
    Kernel kernel = KernelOf(&emitter.liquid, 1.0);
    float radius = (float)kernel.radius;
    bool inside = true;
    float top = 0.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        top = fmaxf(top, Places(1)[i]);
        for (int32_t k = 0; k < 3; ++k)
        {
            float low = k == 1 ? -1e-4f : -1e-4f;
            float high = k == 1 ? 2.0001f : 0.4001f;
            inside = inside && Places(k)[i] >= low && Places(k)[i] <= high;
        }
    }
    double worst = 0.0;
    double mean = 0.0;
    int32_t inner = 0;
    for (int32_t i = 0; i < Count(); ++i)
    {
        bool deep = Places(1)[i] < top - radius && Places(1)[i] > radius;
        for (int32_t k = 0; k < 3; k += 2)
        {
            deep = deep && Places(k)[i] > radius && Places(k)[i] < 0.4f - radius;
        }
        if (!deep)
        {
            continue;
        }
        double sum = 0.0;
        for (int32_t j = 0; j < Count(); ++j)
        {
            double square = 0.0;
            for (int32_t k = 0; k < 3; ++k)
            {
                double d = (double)Places(k)[i] - (double)Places(k)[j];
                square += d * d;
            }
            sum += Poly6(&kernel, square);
        }
        worst = fmax(worst, sum * kernel.restInverse - 1.0);
        mean += sum * kernel.restInverse;
        inner += 1;
    }
    CHECK(Count() == 512 && inside && inner > 0 && worst <= 0.03 && mean / (double)inner >= 0.9,
          "a 3D liquid settles");
    mvfxDestroyContext(s_context);
}

static void TestTensile(void)
{
    // Two particles 0.01 apart (below rest density, so their multipliers
    // are 0), tensile 0.1 of power 4 at 0.2 of the radius, one substep of
    // one iteration: each moves away from the other by the tensile
    // correction times the gradient over the rest density.
    const double places[6] = {0, 0, 0, 0.01, 0, 0};
    mvfxEmitterDef emitter = Emitter(0.05f);
    emitter.liquid.substeps = 1;
    emitter.liquid.iterations = 1;
    emitter.liquid.viscosity = 0.0f;
    emitter.liquid.tensile = 0.1f;
    CHECK(OpenScaled(&emitter, places, 2, 1.0f), "opened");
    const float before[2] = {Places(0)[0], Places(0)[1]};
    Steps(1);
    Kernel kernel = KernelOf(&emitter.liquid, 1.0);
    // The spiky gradient at the first, as the library forms it: -spiky
    // (h - r)^2 / r times the offset from the second.
    double offset = (double)before[0] - (double)before[1];
    double length = fabs(offset);
    double g = -Spiky(&kernel, length) * offset / length;
    double ratio = Poly6(&kernel, length * length) * kernel.tensileInverse;
    double tensile = -kernel.tensileScale * ratio * ratio * ratio * ratio;
    float expected = (float)((double)before[0] + tensile * g * kernel.restInverse);
    float moved = Places(0)[0];
    CHECK(fabsf(moved - expected) <= 1e-7f && moved < before[0] &&
              fabsf((Places(0)[1] - before[1]) + (moved - before[0])) <= 1e-7f,
          "the tensile correction parts a close pair");
    mvfxDestroyContext(s_context);
}

static void TestPlanar(void)
{
    // A planar lattice at 0.04, closer than its rest spacing of 0.05, its
    // particles at Z 0 and 0.01 by turns: a step parts them along X and Y
    // and leaves every Z as it was.
    static double places[8 * 8 * 3];
    for (int32_t i = 0; i < 64; ++i)
    {
        places[3 * i] = 0.04 * (double)(i % 8);
        places[3 * i + 1] = 0.04 * (double)(i / 8);
        places[3 * i + 2] = (i + i / 8) % 2 == 0 ? 0.0 : 0.01;
    }
    mvfxEmitterDef emitter = Emitter(0.05f);
    CHECK(OpenScaled(&emitter, places, 64, 1.0f), "opened");
    Steps(1);
    bool kept = true;
    float spread = 0.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        kept = kept && Places(2)[i] == (float)places[3 * i + 2];
        spread = fmaxf(spread, fabsf(Places(0)[i] - (float)places[3 * i]));
    }
    CHECK(kept && spread > 1e-3f, "a planar liquid keeps its Z");
    mvfxDestroyContext(s_context);
}

// Whether a float is within a share of a binary64 value.
static bool Near(float value, double expected, double share)
{
    return fabs((double)value - expected) <= share * fabs(expected);
}

static void TestPlanned(void)
{
    // A planned instance's plan hands each liquid's kernels over as its
    // step makes them: a world emitter's at its instance's scale, a local
    // one's at 1; the view has the def as compiled. A planar liquid of
    // kernel scale 2.5 and a 3D one with a tensile correction: the radius,
    // and in the kernels' unit each kernel as its formula gives it in
    // binary64, the rest density's sum over the lattice summed again
    // there, within 1e-5 (the radius and its square 1e-6).
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEmitterDef emitters[2] = {Emitter(0.05f), Emitter(0.05f)};
    emitters[0].space = mvfx_spaceWorld;
    emitters[0].liquid.kernelScale = 2.5f;
    emitters[1].space = mvfx_spaceLocal;
    emitters[1].liquid.planar = false;
    emitters[1].liquid.tensile = 0.2f;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 2;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.planned = true;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    const double origin[3] = {0, 0, 0};
    const float still[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, still, 2.0f, true) ==
                  mvfx_success &&
              mvfxContext_StepTicks(s_context, 1000) == mvfx_success,
          "a scaled step");
    bool same = true;
    static mvfxEmitterPlan plan;
    static mvfxEmitterView view;
    for (int32_t e = 0; e < 2; ++e)
    {
        float sigma = e == 0 ? 2.0f : 1.0f;
        CHECK(mvfxInstance_GetEmitterPlan(s_context, s_instance, e, &plan) == mvfx_success &&
                  mvfxEffect_GetEmitterView(s_context, effect, e, &view) == mvfx_success,
              "a plan and a view");
        mvfxLiquidDef compiled = view.liquid;
        // The radius in the particles' space; the rest in the unit, the
        // power of two leaving the radius from 1 to 2 in it: the kernels
        // of lengths divided by it.
        const mvfxPlannedLiquid* l = &plan.liquid;
        Kernel world = KernelOf(&emitters[e].liquid, (double)sigma);
        Kernel k = KernelOf(&emitters[e].liquid, (double)sigma / (double)l->unit);
        int exponent = 0;
        double mantissa = frexp((double)l->unit, &exponent);
        same = same && mantissa == 0.5 && world.radius / (double)l->unit >= 1.0 &&
               world.radius / (double)l->unit < 2.0 && Near(l->radius, world.radius, 1e-6) &&
               Near(l->square, k.square, 1e-6) && Near(l->poly6, k.poly6, 1e-5) &&
               Near(l->spiky, k.spiky, 1e-5) && Near(l->restInverse, k.restInverse, 1e-5) &&
               Near(l->softening, k.softening, 1e-5) &&
               Near(l->tensileInverse, k.tensileInverse, 1e-5) &&
               Near(l->tensileScale, k.tensileScale, 1e-5) && l->sigma == sigma &&
               l->spacing == 0.05f * sigma && compiled.enable &&
               compiled.planar == emitters[e].liquid.planar && compiled.spacing == 0.05f;
    }
    CHECK(same, "a plan's liquid kernels at its scale");
    mvfxDestroyContext(s_context);
}

// A compressed lattice of a world-space liquid, its spacing and its
// instance's scale each a power of two, 3D or planar, with viscosity,
// confinement and a tensile correction: after each of 6 steps its places
// and velocities into places and velocities.
static void Lattice(float spacing, float scale, bool planar, float* places, float* velocities)
{
    static double lattice[216 * 3];
    mvfxEmitterDef emitter = Emitter(spacing);
    emitter.space = mvfx_spaceWorld;
    emitter.liquid.planar = planar;
    emitter.liquid.viscosity = 0.05f;
    emitter.liquid.vorticity = 2.0f;
    emitter.liquid.tensile = 0.2f;
    int32_t side = planar ? 12 : 6;
    int32_t count = planar ? side * side : side * side * side;
    double gap = 0.8 * (double)spacing * (double)scale;
    for (int32_t i = 0; i < count; ++i)
    {
        lattice[3 * i] = gap * (double)(i % side);
        lattice[3 * i + 1] = gap * (double)(i / side % side);
        lattice[3 * i + 2] = planar ? 0.0 : gap * (double)(i / (side * side));
    }
    CHECK(OpenScaled(&emitter, lattice, count, scale), "opened");
    for (int32_t step = 0; step < 6; ++step)
    {
        Steps(1);
        for (int32_t k = 0; k < 3; ++k)
        {
            for (int32_t i = 0; i < count; ++i)
            {
                size_t at = ((size_t)step * 3u + (size_t)k) * 216u + (size_t)i;
                places[at] = i < Count() ? Places(k)[i] : NAN;
                velocities[at] = i < Count() ? Velocities(k)[i] : NAN;
            }
        }
    }
    mvfxDestroyContext(s_context);
}

static void TestRange(void)
{
    // The kernels measure lengths in a power of two of the radius, so a
    // liquid's results scale exactly with its lengths over the radius's
    // whole range: lattices whose radius is 2^38 (spacing 2^18 under a
    // scale of 2^19) and 2^-31 (spacing 2^-13 under 2^-19), where binary32
    // holds neither the kernels' factors nor the cube of a squared radius,
    // move as the lattice of radius 2^-3 does, every place and velocity
    // that one's times 2^41 and 2^-28 (the radii's ratios), finite.
    static float places[3][6 * 3 * 216];
    static float velocities[3][6 * 3 * 216];
    static const float spacings[3] = {0x1p-4f, 0x1p18f, 0x1p-13f};
    static const float scales[3] = {1.0f, 0x1p19f, 0x1p-19f};
    static const float ratios[3] = {1.0f, 0x1p41f, 0x1p-28f};
    for (int32_t p = 0; p < 2; ++p)
    {
        for (int32_t c = 0; c < 3; ++c)
        {
            Lattice(spacings[c], scales[c], p == 1, places[c], velocities[c]);
        }
        bool exact = true;
        for (int32_t c = 1; c < 3; ++c)
        {
            for (size_t i = 0; i < 6u * 3u * 216u; ++i)
            {
                bool counted = !isnan(places[0][i]);
                exact = exact && (!counted || (isfinite(places[c][i]) &&
                                               places[c][i] == places[0][i] * ratios[c] &&
                                               velocities[c][i] == velocities[0][i] * ratios[c]));
            }
        }
        CHECK(exact, p == 1 ? "a planar liquid scales exactly to its range's ends"
                            : "a 3D liquid scales exactly to its range's ends");
    }
}

int main(void)
{
    TestRange();
    TestPlanned();
    TestRefusals();
    TestScale();
    TestCapped();
    TestViscosity();
    TestVorticity();
    TestDeep();
    TestTensile();
    TestPlanar();
    TestRest();
    return s_failures;
}
