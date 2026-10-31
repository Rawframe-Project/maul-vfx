// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Curl noise as a turbulence force gives it, read as particles' velocities
// after a step from rest: the bake into a periodic grid is the force's
// curl bit for bit within the period, with no seam where it repeats, and
// writes nothing when it refuses; octaves, gain, seeds and scroll; the
// field continuous across a cell's face and without divergence; the
// lattice's 32-bit wrap, and a place 2^52 out at the highest frequency.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

// The most particles a scene places.
#define MOST 1536

static const mvfxNoise s_noise = {
    .frequency = 0.75f, .octaves = 3, .lacunarity = 2.0f, .gain = 0.5f, .scroll = {0, 0, 0}};

static TestParticle s_read[MOST];

static const double s_zero[3] = {0.0, 0.0, 0.0};

// The curl of noise at count places, in an instance at origin (which
// keeps a place near it exact) that has run for seconds: each a
// particle's velocity after a step of 1/1024 s from rest under a world
// turbulence of strength 1, times 1,024, which is exact.
static void Curls(const mvfxNoise* noise, const double origin[3], double seconds,
                  const double* places, int32_t count, float* out)
{
    mvfxForce force = {
        .kind = mvfx_forceTurbulence, .strength = mvfxMakeRangeSource(1.0f, 1.0f), .noise = *noise};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = MOST;
    emitter.hostSpawnCapacity = MOST;
    emitter.lifetime = mvfxMakeRangeSource(100.0f, 100.0f);
    emitter.forces = &force;
    emitter.forceCount = 1;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    memcpy(instanceDef.origin, origin, sizeof instanceDef.origin);
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    // In steps of at most a second.
    for (double left = seconds; left > 0.0; left -= 1.0)
    {
        double part = left < 1.0 ? left : 1.0;
        CHECK(mvfxContext_StepTicks(context, (int64_t)(part * MVFX_TICKS_PER_SECOND)) ==
                  mvfx_success,
              "run");
    }
    static float rest[MOST * 3];
    mvfxSpawnRequest request = {.count = count, .positions = places, .velocities = rest};
    CHECK(mvfxInstance_Spawn(context, instance, 0, &request) == mvfx_success, "placed");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 1024) == mvfx_success, "step");
    CHECK(ReadParticles(context, instance, 0, s_read, MOST) == count, "read");
    for (int32_t i = 0; i < count; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            out[3 * i + k] = 1024.0f * s_read[i].velocity[k];
        }
    }
    mvfxDestroyContext(context);
}

// The bake into a periodic grid: a noise of two octaves at lacunarity 2,
// frequency 1 so a lattice place is a place, period 4 cells over 16, 8
// and 12 points along X, Y and Z.
enum
{
    BAKE_X = 16,
    BAKE_Y = 8,
    BAKE_Z = 12,
    BAKE_PERIOD = 4,
    BAKE_POINTS = BAKE_X * BAKE_Y * BAKE_Z,
    BAKE_VALUES = 3 * BAKE_POINTS
};

static float s_baked[BAKE_VALUES];

static const float* BakedAt(int32_t x, int32_t y, int32_t z)
{
    return &s_baked[3 * ((z * BAKE_Y + y) * BAKE_X + x)];
}

static void TestBakeSeam(void)
{
    // Along X, the last point to the first is a step like any other: no
    // seam where the tile repeats.
    float step = 0.0f;
    float seam = 0.0f;
    for (int32_t z = 0; z < BAKE_Z; ++z)
    {
        for (int32_t y = 0; y < BAKE_Y; ++y)
        {
            for (int32_t x = 0; x < BAKE_X; ++x)
            {
                const float* a = BakedAt(x, y, z);
                const float* b = BakedAt((x + 1) % BAKE_X, y, z);
                for (int32_t c = 0; c < 3; ++c)
                {
                    float d = fabsf(b[c] - a[c]);
                    step = x + 1 < BAKE_X ? fmaxf(step, d) : step;
                    seam = x + 1 == BAKE_X ? fmaxf(seam, d) : seam;
                }
            }
        }
    }
    CHECK(seam > 0.0f && seam <= step, "no seam where it repeats");
}

static void TestBakeRefusals(const mvfxNoise* baked, const int32_t size[3])
{
    mvfxNoise noise = *baked;
    // Refusals write nothing.
    mvfxNoise fractional = noise;
    fractional.lacunarity = 2.5f;
    const int32_t empty[3] = {BAKE_X, 0, BAKE_Z};
    s_baked[0] = 12345.0f;
    bool refused =
        mvfxBakeCurlNoise(NULL, BAKE_PERIOD, size, s_baked, BAKE_VALUES) == mvfx_errorInvalid &&
        mvfxBakeCurlNoise(&fractional, BAKE_PERIOD, size, s_baked, BAKE_VALUES) ==
            mvfx_errorInvalid &&
        mvfxBakeCurlNoise(&noise, 0, size, s_baked, BAKE_VALUES) == mvfx_errorInvalid &&
        mvfxBakeCurlNoise(&noise, MVFX_MAX_NOISE_PERIOD + 1, size, s_baked, BAKE_VALUES) ==
            mvfx_errorLimit &&
        mvfxBakeCurlNoise(&noise, BAKE_PERIOD, empty, s_baked, BAKE_VALUES) == mvfx_errorInvalid &&
        mvfxBakeCurlNoise(&noise, BAKE_PERIOD, size, s_baked, BAKE_VALUES - 1) == mvfx_errorInvalid;
    CHECK(refused && s_baked[0] == 12345.0f, "refusals write nothing");
}

// Whether a place in the bake's grid lies, at every octave, outside its
// period's last cell.
static bool IsInner(const double place[3])
{
    bool inner = true;
    for (int32_t k = 0; k < 3; ++k)
    {
        for (int32_t o = 0; o < 2; ++o)
        {
            double scale = (double)(1 << o);
            inner = inner && floor(place[k] * scale) < BAKE_PERIOD * scale - 1.0;
        }
    }
    return inner;
}

static void TestBake(void)
{
    mvfxNoise noise = s_noise;
    noise.frequency = 1.0f;
    noise.octaves = 2;
    noise.seed = 7u;
    const int32_t size[3] = {BAKE_X, BAKE_Y, BAKE_Z};
    CHECK(mvfxBakeCurlNoise(&noise, BAKE_PERIOD, size, s_baked, BAKE_VALUES) == mvfx_success,
          "baked");
    // Where no octave's cell is its period's last, the turbulence's curl's
    // bits: a layer along Z at a time, its instance's origin at its place,
    // so a third of a cell is held exactly.
    static double places[MOST * 3];
    static float curls[MOST * 3];
    int32_t compared = 0;
    int32_t same = 0;
    for (int32_t z = 0; z < BAKE_Z; ++z)
    {
        const double layer[3] = {0.0, 0.0, (double)z * BAKE_PERIOD / BAKE_Z};
        int32_t count = 0;
        for (int32_t y = 0; y < BAKE_Y; ++y)
        {
            for (int32_t x = 0; x < BAKE_X; ++x)
            {
                const double place[3] = {(double)x * BAKE_PERIOD / BAKE_X,
                                         (double)y * BAKE_PERIOD / BAKE_Y, layer[2]};
                if (IsInner(place))
                {
                    memcpy(&places[3 * count], place, sizeof place);
                    count += 1;
                }
            }
        }
        if (count == 0)
        {
            continue;
        }
        Curls(&noise, layer, 0.0, places, count, curls);
        for (int32_t i = 0; i < count; ++i)
        {
            int32_t x = (int32_t)(places[3 * i] * BAKE_X / BAKE_PERIOD);
            int32_t y = (int32_t)(places[3 * i + 1] * BAKE_Y / BAKE_PERIOD);
            same += memcmp(BakedAt(x, y, z), &curls[3 * i], 3 * sizeof(float)) == 0 ? 1 : 0;
        }
        compared += count;
    }
    // Within along X 12 of 16 points, along Y 6 of 8, along Z 9 of 12.
    CHECK(compared == 12 * 6 * 9 && same == compared, "the turbulence's curl's bits within");
    TestBakeSeam();
    TestBakeRefusals(&noise, size);
}

// A noise's bake of 8 points along each axis over a period of 4 cells.
static void Bake(const mvfxNoise* noise, float* out)
{
    const int32_t size[3] = {8, 8, 8};
    CHECK(mvfxBakeCurlNoise(noise, 4, size, out, 3 * 512) == mvfx_success, "baked");
}

static void TestOctavesAndSeeds(void)
{
    static float a[3 * 512];
    static float b[3 * 512];
    // A gain of 0 leaves the first octave alone.
    mvfxNoise one = s_noise;
    one.octaves = 1;
    mvfxNoise silent = s_noise;
    silent.gain = 0.0f;
    Bake(&one, a);
    Bake(&silent, b);
    CHECK(memcmp(a, b, sizeof a) == 0, "gain 0 is one octave");
    Bake(&s_noise, b);
    CHECK(memcmp(a, b, sizeof a) != 0, "more octaves add detail");
    float largest = 0.0f;
    for (int32_t i = 0; i < 3 * 512; ++i)
    {
        largest = fmaxf(largest, fabsf(b[i]));
    }
    CHECK(largest > 0.5f && isfinite(largest), "not flat");
    mvfxNoise seeded = s_noise;
    seeded.seed = 1u;
    Bake(&seeded, a);
    seeded.seed = 2u;
    Bake(&seeded, b);
    CHECK(memcmp(a, b, sizeof a) != 0, "seeds differ");
}

static bool Near(const float* a, const float* b, float tolerance)
{
    return fabsf(a[0] - b[0]) <= tolerance && fabsf(a[1] - b[1]) <= tolerance &&
           fabsf(a[2] - b[2]) <= tolerance;
}

static void TestScroll(void)
{
    // The field scrolls: at time t it is at p what it was at p + scroll t.
    mvfxNoise scrolled = s_noise;
    scrolled.scroll[0] = 2.0f;
    scrolled.scroll[2] = -0.5f;
    static const double p[3] = {1.25, -3.5, 0.375};
    static const double moved[3] = {1.25 + 2.0 * 1.5, -3.5, 0.375 - 0.5 * 1.5};
    float a[3];
    float b[3];
    Curls(&scrolled, s_zero, 1.5, p, 1, a);
    Curls(&s_noise, s_zero, 0.0, moved, 1, b);
    CHECK(Near(a, b, 1e-6f), "scrolled over time");
}

static void TestWrapAndFar(void)
{
    // The lattice wraps every 2^32 cells: at frequency 1, places 2^32 apart
    // along X and Y and 2^33 along Z give the same bits, each in an
    // instance at its whole cell, so the fraction is held exactly.
    mvfxNoise unit = s_noise;
    unit.frequency = 1.0f;
    static const double far[3] = {4294967296.0, -4294967296.0, 8589934592.0};
    static const double near[3] = {0.25, 0.75, 0.125};
    const double out[3] = {far[0] + near[0], far[1] + near[1], far[2] + near[2]};
    float a[3];
    float b[3];
    Curls(&unit, s_zero, 0.0, near, 1, a);
    Curls(&unit, far, 0.0, out, 1, b);
    CHECK(memcmp(a, b, sizeof a) == 0, "periodic over 2^32 cells");
    // A place 2^52 out at the highest frequency, past 2^64 cells.
    mvfxNoise fast = s_noise;
    fast.frequency = MVFX_MAX_FREQUENCY;
    static const double huge[3] = {0x1p52, -0x1p52, 0x1p51};
    Curls(&fast, huge, 0.0, huge, 1, a);
    CHECK(isfinite(a[0]) && isfinite(a[1]) && isfinite(a[2]) &&
              (a[0] != 0.0f || a[1] != 0.0f || a[2] != 0.0f),
          "finite far out");
}

static void TestContinuousAndDivergenceFree(void)
{
    // Across a cell's face at frequency 1: the curl agrees on both sides.
    mvfxNoise unit = s_noise;
    unit.frequency = 1.0f;
    unit.octaves = 1;
    static const double face[6] = {2.0 - 0x1p-22, 0.3125, 0.625, 2.0, 0.3125, 0.625};
    float sides[6];
    Curls(&unit, s_zero, 0.0, face, 2, sides);
    CHECK(Near(sides, sides + 3, 1e-4f), "continuous across a cell's face");

    // Central differences of the curl, a step of 2^-10 each way along each
    // axis, at 200 places on a grid of 2^-8 in [-8, 8)^3: the divergence
    // near 0, against a curl not flat.
    enum
    {
        PLACES = 200
    };
    const double h = 0x1p-10;
    static double around[PLACES * 6 * 3];
    static float curls[PLACES * 6 * 3];
    for (int32_t i = 0; i < PLACES; ++i)
    {
        double p[3] = {
            floor((fmod(0.7548776662466927 * (i + 1), 1.0) * 16.0 - 8.0) * 256.0) / 256.0,
            floor((fmod(0.5698402909980532 * (i + 1), 1.0) * 16.0 - 8.0) * 256.0) / 256.0,
            floor((fmod(0.3141592653589793 * (i + 1) + 0.25, 1.0) * 16.0 - 8.0) * 256.0) / 256.0};
        for (int32_t a = 0; a < 3; ++a)
        {
            for (int32_t side = 0; side < 2; ++side)
            {
                double* q = &around[3 * (6 * i + 2 * a + side)];
                memcpy(q, p, sizeof p);
                q[a] += side == 0 ? h : -h;
            }
        }
    }
    Curls(&s_noise, s_zero, 0.0, around, PLACES * 6, curls);
    double worst = 0.0;
    double largest = 0.0;
    for (int32_t i = 0; i < PLACES; ++i)
    {
        double divergence = 0.0;
        for (int32_t a = 0; a < 3; ++a)
        {
            const float* plus = &curls[3 * (6 * i + 2 * a)];
            const float* minus = plus + 3;
            divergence += ((double)plus[a] - (double)minus[a]) / (2.0 * h);
            largest = fmax(largest, fabs((double)plus[a]));
        }
        worst = fmax(worst, fabs(divergence));
    }
    CHECK(worst <= 2e-2 && largest > 0.5, "without divergence, and not flat");
}

int main(void)
{
    TestBake();
    TestOctavesAndSeeds();
    TestScroll();
    TestWrapAndFar();
    TestContinuousAndDivergenceFree();
    return s_failures == 0 ? 0 : 1;
}
