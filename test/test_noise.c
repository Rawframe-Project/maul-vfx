// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Curl noise: zero at lattice points, analytic gradients against finite
// differences, continuous across cells, within its range, its curl the
// finite-difference curl of the potential and without divergence,
// octaves, gain and scroll, the 32-bit wrap of the lattice, huge
// coordinates, and seeds; and its bake into a periodic grid, the
// unbounded curl's bits within the period and no seam where it repeats.

#include "noise.h"
#include "test_harness.h"

#include <math.h>

// A point in [-8, 8)^3 from an index, spread by a fixed sequence.
static void PointOf(int32_t i, double p[3])
{
    p[0] = fmod(0.7548776662466927 * (i + 1), 1.0) * 16.0 - 8.0;
    p[1] = fmod(0.5698402909980532 * (i + 1), 1.0) * 16.0 - 8.0;
    p[2] = fmod(0.3141592653589793 * (i + 1) + 0.25, 1.0) * 16.0 - 8.0;
}

static void TestLatticeAndRange(void)
{
    float value[3];
    float gradient[3][3];
    bool zero = true;
    for (int32_t i = -3; i <= 3; ++i)
    {
        const double p[3] = {i, 2 * i + 1, -i};
        mvfxGradientNoise3(p, 7u, value, gradient);
        zero = zero && value[0] == 0.0f && value[1] == 0.0f && value[2] == 0.0f;
    }
    CHECK(zero, "zero at every lattice point");
    float largest = 0.0f;
    for (int32_t i = 0; i < 20000; ++i)
    {
        double p[3];
        PointOf(i, p);
        mvfxGradientNoise3(p, 7u, value, gradient);
        for (int32_t c = 0; c < 3; ++c)
        {
            largest = fmaxf(largest, fabsf(value[c]));
        }
    }
    CHECK(largest > 0.5f && largest <= 1.1f, "within its range, and not flat");
}

static void TestGradients(void)
{
    // Central differences of the value, in binary64 steps.
    const double h = 1e-3;
    double worst = 0.0;
    for (int32_t i = 0; i < 2000; ++i)
    {
        double p[3];
        PointOf(i, p);
        float value[3];
        float gradient[3][3];
        mvfxGradientNoise3(p, 11u, value, gradient);
        for (int32_t a = 0; a < 3; ++a)
        {
            double plus[3] = {p[0], p[1], p[2]};
            double minus[3] = {p[0], p[1], p[2]};
            plus[a] += h;
            minus[a] -= h;
            float vp[3];
            float vm[3];
            float unused[3][3];
            mvfxGradientNoise3(plus, 11u, vp, unused);
            mvfxGradientNoise3(minus, 11u, vm, unused);
            for (int32_t c = 0; c < 3; ++c)
            {
                double difference = ((double)vp[c] - (double)vm[c]) / (2.0 * h);
                worst = fmax(worst, fabs(difference - (double)gradient[c][a]));
            }
        }
    }
    CHECK(worst <= 2e-3, "analytic gradients match finite differences");

    // Across a cell's face: the value and the gradient agree on both sides.
    float below[3];
    float above[3];
    float gb[3][3];
    float ga[3][3];
    const double p1[3] = {2.0 - 1e-7, 0.3, 0.6};
    const double p2[3] = {2.0, 0.3, 0.6};
    mvfxGradientNoise3(p1, 3u, below, gb);
    mvfxGradientNoise3(p2, 3u, above, ga);
    bool continuous = true;
    for (int32_t c = 0; c < 3; ++c)
    {
        continuous = continuous && fabsf(below[c] - above[c]) <= 1e-5f;
        for (int32_t a = 0; a < 3; ++a)
        {
            continuous = continuous && fabsf(gb[c][a] - ga[c][a]) <= 1e-4f;
        }
    }
    CHECK(continuous, "continuous across a cell's face");
}

static const mvfxNoise s_noise = {
    .frequency = 0.75f, .octaves = 3, .lacunarity = 2.0f, .gain = 0.5f, .scroll = {0, 0, 0}};

// The potential the noise's curl is the curl of, in binary64 over float
// noises: each octave's noise over its frequency.
static void Potential(const mvfxNoise* noise, uint32_t seed, const double p[3], double out[3])
{
    out[0] = 0.0;
    out[1] = 0.0;
    out[2] = 0.0;
    double frequency = (double)noise->frequency;
    double weight = 1.0;
    for (int32_t o = 0; o < noise->octaves; ++o)
    {
        double q[3] = {p[0] * frequency, p[1] * frequency, p[2] * frequency};
        float value[3];
        float gradient[3][3];
        mvfxGradientNoise3(q, seed + (uint32_t)o * 0x9E3779B9u, value, gradient);
        for (int32_t c = 0; c < 3; ++c)
        {
            out[c] += weight * (double)value[c] / frequency;
        }
        frequency *= (double)noise->lacunarity;
        weight *= (double)noise->gain;
    }
}

static void TestCurl(void)
{
    const double h = 1e-3;
    double worstCurl = 0.0;
    double worstDivergence = 0.0;
    double largest = 0.0;
    for (int32_t i = 0; i < 1000; ++i)
    {
        double p[3];
        PointOf(i, p);
        float curl[3];
        mvfxCurlNoise(&s_noise, 5u, p, 0.0, curl);
        // d[a][c]: the potential's component c along axis a, differenced.
        double d[3][3];
        double divergence = 0.0;
        for (int32_t a = 0; a < 3; ++a)
        {
            double plus[3] = {p[0], p[1], p[2]};
            double minus[3] = {p[0], p[1], p[2]};
            plus[a] += h;
            minus[a] -= h;
            double up[3];
            double down[3];
            Potential(&s_noise, 5u, plus, up);
            Potential(&s_noise, 5u, minus, down);
            for (int32_t c = 0; c < 3; ++c)
            {
                d[a][c] = (up[c] - down[c]) / (2.0 * h);
            }
            float cp[3];
            float cm[3];
            mvfxCurlNoise(&s_noise, 5u, plus, 0.0, cp);
            mvfxCurlNoise(&s_noise, 5u, minus, 0.0, cm);
            divergence += ((double)cp[a] - (double)cm[a]) / (2.0 * h);
        }
        const double reference[3] = {d[1][2] - d[2][1], d[2][0] - d[0][2], d[0][1] - d[1][0]};
        for (int32_t k = 0; k < 3; ++k)
        {
            worstCurl = fmax(worstCurl, fabs((double)curl[k] - reference[k]));
            largest = fmax(largest, fabs((double)curl[k]));
        }
        worstDivergence = fmax(worstDivergence, fabs(divergence));
    }
    CHECK(worstCurl <= 5e-3, "the curl of the potential");
    CHECK(worstDivergence <= 2e-2 && largest > 0.5, "without divergence, and not flat");
}

static void TestOctavesScrollAndWrap(void)
{
    const double p[3] = {1.25, -3.5, 0.375};
    float a[3];
    float b[3];
    // A gain of 0 leaves the first octave alone.
    mvfxNoise one = s_noise;
    one.octaves = 1;
    mvfxNoise silent = s_noise;
    silent.gain = 0.0f;
    mvfxCurlNoise(&one, 9u, p, 0.0, a);
    mvfxCurlNoise(&silent, 9u, p, 0.0, b);
    CHECK(a[0] == b[0] && a[1] == b[1] && a[2] == b[2], "gain 0 is one octave");
    mvfxCurlNoise(&s_noise, 9u, p, 0.0, b);
    CHECK(a[0] != b[0], "more octaves add detail");

    // The field scrolls: at time t it is at p what it was at p + scroll t.
    mvfxNoise scrolled = s_noise;
    scrolled.scroll[0] = 2.0f;
    scrolled.scroll[2] = -0.5f;
    const double moved[3] = {p[0] + 2.0 * 1.5, p[1], p[2] - 0.5 * 1.5};
    mvfxCurlNoise(&scrolled, 9u, p, 1.5, a);
    mvfxCurlNoise(&s_noise, 9u, moved, 0.0, b);
    CHECK(fabsf(a[0] - b[0]) <= 1e-6f && fabsf(a[1] - b[1]) <= 1e-6f && fabsf(a[2] - b[2]) <= 1e-6f,
          "scrolled over time");

    // The lattice wraps every 2^32 cells, and any finite place is sampled.
    float value[3];
    float wrapped[3];
    float gradient[3][3];
    // Fractions a binary64 holds exactly that far out.
    const double near[3] = {0.25, 0.75, 0.125};
    const double far[3] = {0.25 + 4294967296.0, 0.75 - 4294967296.0, 0.125 + 8589934592.0};
    mvfxGradientNoise3(near, 1u, value, gradient);
    mvfxGradientNoise3(far, 1u, wrapped, gradient);
    CHECK(value[0] == wrapped[0] && value[1] == wrapped[1] && value[2] == wrapped[2],
          "periodic over 2^32 cells");
    const double huge[3] = {0x1p70 + 0.0, -0x1p66, 1e300};
    mvfxGradientNoise3(huge, 1u, value, gradient);
    CHECK(isfinite(value[0]) && isfinite(gradient[2][2]), "finite at huge places");
    const double wild[3] = {(double)INFINITY, (double)NAN, -(double)INFINITY};
    mvfxGradientNoise3(wild, 1u, value, gradient);
    CHECK(isfinite(value[0]) && isfinite(value[1]) && isfinite(gradient[0][0]),
          "finite where a place is not");

    // Seeds differ.
    mvfxCurlNoise(&s_noise, 1u, p, 0.0, a);
    mvfxCurlNoise(&s_noise, 2u, p, 0.0, b);
    CHECK(a[0] != b[0] || a[1] != b[1], "seeds differ");
}

// The bake into a periodic grid: a noise of two octaves at lacunarity 2,
// frequency 1 so a lattice place is a place, period 4 cells over 16
// points an axis.
enum
{
    BAKE_POINTS = 16,
    BAKE_PERIOD = 4,
    BAKE_VALUES = 3 * BAKE_POINTS * BAKE_POINTS * BAKE_POINTS
};

static float s_baked[BAKE_VALUES];

static const float* BakedAt(int32_t x, int32_t y, int32_t z)
{
    return &s_baked[3 * ((z * BAKE_POINTS + y) * BAKE_POINTS + x)];
}

static void TestBakeSeam(void)
{
    // Along X, the last point to the first is a step like any other: no
    // seam where the tile repeats.
    float step = 0.0f;
    float seam = 0.0f;
    for (int32_t z = 0; z < BAKE_POINTS; ++z)
    {
        for (int32_t y = 0; y < BAKE_POINTS; ++y)
        {
            for (int32_t x = 0; x < BAKE_POINTS; ++x)
            {
                const float* a = BakedAt(x, y, z);
                const float* b = BakedAt((x + 1) % BAKE_POINTS, y, z);
                for (int32_t c = 0; c < 3; ++c)
                {
                    float d = fabsf(b[c] - a[c]);
                    step = x + 1 < BAKE_POINTS ? fmaxf(step, d) : step;
                    seam = x + 1 == BAKE_POINTS ? fmaxf(seam, d) : seam;
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
    const int32_t empty[3] = {BAKE_POINTS, 0, BAKE_POINTS};
    s_baked[0] = 12345.0f;
    bool refused =
        mvfxBakeCurlNoise(NULL, BAKE_PERIOD, size, s_baked, BAKE_VALUES) == mvfx_errorInvalid &&
        mvfxBakeCurlNoise(&fractional, BAKE_PERIOD, size, s_baked, BAKE_VALUES) ==
            mvfx_errorInvalid &&
        mvfxBakeCurlNoise(&noise, 0, size, s_baked, BAKE_VALUES) == mvfx_errorInvalid &&
        mvfxBakeCurlNoise(&noise, MVFX_MAX_NOISE_PERIOD + 1, size, s_baked, BAKE_VALUES) ==
            mvfx_errorInvalid &&
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
    const int32_t size[3] = {BAKE_POINTS, BAKE_POINTS, BAKE_POINTS};
    CHECK(mvfxBakeCurlNoise(&noise, BAKE_PERIOD, size, s_baked, BAKE_VALUES) == mvfx_success,
          "baked");
    // Where no octave's cell is its period's last, the unbounded curl's
    // bits.
    int32_t compared = 0;
    int32_t same = 0;
    for (int32_t z = 0; z < BAKE_POINTS; ++z)
    {
        for (int32_t y = 0; y < BAKE_POINTS; ++y)
        {
            for (int32_t x = 0; x < BAKE_POINTS; ++x)
            {
                const double place[3] = {(double)x * BAKE_PERIOD / BAKE_POINTS,
                                         (double)y * BAKE_PERIOD / BAKE_POINTS,
                                         (double)z * BAKE_PERIOD / BAKE_POINTS};
                if (!IsInner(place))
                {
                    continue;
                }
                float curl[3];
                mvfxCurlNoise(&noise, noise.seed, place, 0.0, curl);
                const float* baked = BakedAt(x, y, z);
                compared += 1;
                same += baked[0] == curl[0] && baked[1] == curl[1] && baked[2] == curl[2];
            }
        }
    }
    CHECK(compared == 12 * 12 * 12 && same == compared, "the unbounded curl's bits within");
    TestBakeSeam();
    TestBakeRefusals(&noise, size);
}

int main(void)
{
    TestLatticeAndRange();
    TestGradients();
    TestCurl();
    TestOctavesScrollAndWrap();
    TestBake();
    return s_failures == 0 ? 0 : 1;
}
