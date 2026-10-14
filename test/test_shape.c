// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Spawn shapes and initial velocities against their geometry: every point
// on or in its shape, uniformity by moments and bin counts within five
// standard deviations at fixed seeds, normals, turns and offsets, arc
// modes, and velocities by direction mode, randomization and speed.

#include "shape.h"
#include "test_harness.h"
#include "velocity.h"

#include <math.h>

#define SAMPLES 40000
#define PI_F    3.14159265358979323846f
#define PI_D    3.14159265358979323846

static mvfxWords4 Words(uint32_t i, uint32_t salt)
{
    return mvfxPcg4d((mvfxWords4){i, salt, 0x1234u, 0u});
}

static mvfxShape BaseShape(mvfxShapeKind kind)
{
    return (mvfxShape){
        .kind = kind,
        .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
        .arc = 2.0f * PI_F,
        .thickness = 1.0f,
    };
}

static mvfxCompiledShape Compile(const mvfxShape* shape)
{
    mvfxCompiledShape compiled;
    CHECK(mvfxIsShapeValid(shape), "a valid shape");
    mvfxCompileShape(shape, &compiled);
    return compiled;
}

static mvfxShapeSample Sample(const mvfxCompiledShape* shape, uint32_t i)
{
    mvfxWords4 words = Words(i, 7u);
    mvfxShapeSample sample;
    mvfxSampleShape(shape, words, mvfxArcFraction(shape, words, 0.0, 0, 1), &sample);
    return sample;
}

static double Length(const float v[3])
{
    return sqrt((double)v[0] * (double)v[0] + (double)v[1] * (double)v[1] +
                (double)v[2] * (double)v[2]);
}

// Whether a mean of SAMPLES draws is within five standard errors.
static bool NearMean(double mean, double expected, double deviation)
{
    return fabs(mean - expected) <= 5.0 * deviation / sqrt((double)SAMPLES);
}

// Whether every one of count bins holds its share within five standard
// deviations of a binomial.
static bool EvenBins(const int64_t* bins, int32_t count)
{
    double share = 1.0 / count;
    double expected = SAMPLES * share;
    double deviation = sqrt(SAMPLES * share * (1.0 - share));
    for (int32_t i = 0; i < count; ++i)
    {
        if (fabs((double)bins[i] - expected) > 5.0 * deviation)
        {
            return false;
        }
    }
    return true;
}

static void TestSphere(void)
{
    static const float thicknesses[] = {1.0f, 0.0f, 0.5f};
    for (int32_t t = 0; t < 3; ++t)
    {
        mvfxShape shape = BaseShape(mvfx_shapeSphere);
        shape.radius = 2.0f;
        shape.thickness = thicknesses[t];
        mvfxCompiledShape compiled = Compile(&shape);
        double inner = 2.0 * (1.0 - (double)thicknesses[t]);
        double sumRadius = 0.0;
        double sumZ = 0.0;
        int64_t octants[8] = {0};
        bool inside = true;
        bool normals = true;
        for (uint32_t i = 0; i < SAMPLES; ++i)
        {
            mvfxShapeSample s = Sample(&compiled, i);
            double r = Length(s.position);
            inside = inside && r <= 2.0 * (1.0 + 1e-6) && r >= inner * (1.0 - 1e-6);
            for (int32_t k = 0; k < 3; ++k)
            {
                normals = normals && fabs((double)s.normal[k] - (double)s.position[k] / r) <= 1e-5;
            }
            sumRadius += r;
            sumZ += (double)s.position[2];
            octants[(s.position[0] < 0.0f ? 1 : 0) + (s.position[1] < 0.0f ? 2 : 0) +
                    (s.position[2] < 0.0f ? 4 : 0)] += 1;
        }
        CHECK(inside, "within the shell");
        CHECK(normals, "the normal points outward");
        CHECK(EvenBins(octants, 8), "every octant alike");
        // The mean radius of a uniform shell from a to b: 3/4 (b^4 - a^4) /
        // (b^3 - a^3); on the surface, b.
        double a = inner;
        double expected = t == 1 ? 2.0 : 0.75 * (16.0 - a * a * a * a) / (8.0 - a * a * a);
        CHECK(fabs(sumRadius / SAMPLES - expected) <= (t == 1 ? 1e-6 : 0.01), "the mean radius");
        CHECK(NearMean(sumZ / SAMPLES, 0.0, 2.0), "centered");
    }

    mvfxShape hemi = BaseShape(mvfx_shapeHemisphere);
    hemi.radius = 1.0f;
    hemi.thickness = 0.0f;
    mvfxCompiledShape compiled = Compile(&hemi);
    double sumZ = 0.0;
    bool upper = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        mvfxShapeSample s = Sample(&compiled, i);
        upper = upper && s.position[2] >= 0.0f;
        sumZ += (double)s.position[2];
    }
    CHECK(upper, "a hemisphere above its plane");
    // Z is uniform over [0, 1] on a hemisphere's surface.
    CHECK(NearMean(sumZ / SAMPLES, 0.5, sqrt(1.0 / 12.0)), "a hemisphere's mean height");
}

static void TestCircleAndLine(void)
{
    mvfxShape disc = BaseShape(mvfx_shapeCircle);
    disc.radius = 3.0f;
    mvfxCompiledShape compiled = Compile(&disc);
    double sumRadius = 0.0;
    int64_t sectors[8] = {0};
    bool flat = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        mvfxShapeSample s = Sample(&compiled, i);
        flat = flat && s.position[2] == 0.0f && Length(s.position) <= 3.0 * (1.0 + 1e-6);
        sumRadius += Length(s.position);
        double angle = atan2((double)s.position[1], (double)s.position[0]);
        int32_t sector = (int32_t)floor((angle + PI_D) / (2.0 * PI_D) * 8.0);
        sectors[sector < 0 ? 0 : (sector > 7 ? 7 : sector)] += 1;
    }
    CHECK(flat, "a disc in its plane");
    CHECK(NearMean(sumRadius / SAMPLES, 2.0, 3.0 * sqrt(1.0 / 18.0)), "a disc's mean radius 2/3 r");
    CHECK(EvenBins(sectors, 8), "every sector alike");

    disc.thickness = 0.0f;
    disc.arc = 0.5f * PI_F;
    compiled = Compile(&disc);
    bool onEdge = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        mvfxShapeSample s = Sample(&compiled, i);
        onEdge = onEdge && fabs(Length(s.position) - 3.0) <= 3e-6 && s.position[0] >= -1e-6f &&
                 s.position[1] >= -1e-6f && fabsf(s.normal[0] * 3.0f - s.position[0]) <= 1e-5f;
    }
    CHECK(onEdge, "an arc's edge in its quarter, the normal outward");

    mvfxShape line = BaseShape(mvfx_shapeLine);
    line.length = 4.0f;
    compiled = Compile(&line);
    int64_t quarters[4] = {0};
    bool along = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        mvfxShapeSample s = Sample(&compiled, i);
        along = along && fabsf(s.position[0]) <= 2.0f && s.position[1] == 0.0f &&
                s.position[2] == 0.0f && s.normal[1] == 1.0f;
        int32_t q = (int32_t)((s.position[0] + 2.0f) / 1.0f);
        quarters[q > 3 ? 3 : q] += 1;
    }
    CHECK(along, "a line along X, its normal +Y");
    CHECK(EvenBins(quarters, 4), "uniform along a line");
}

static void TestBox(void)
{
    // Its volume: uniform per axis, the normal along the nearest face.
    mvfxShape box = BaseShape(mvfx_shapeBox);
    box.halfExtents[0] = 1.0f;
    box.halfExtents[1] = 2.0f;
    box.halfExtents[2] = 3.0f;
    mvfxCompiledShape compiled = Compile(&box);
    double sums[3] = {0.0};
    bool inside = true;
    bool nearest = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        mvfxShapeSample s = Sample(&compiled, i);
        int32_t axis = 0;
        double best = -1.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            double reach = fabs((double)s.position[k]) / (double)box.halfExtents[k];
            inside = inside && reach <= 1.0;
            sums[k] += reach;
            if (reach > best)
            {
                best = reach;
                axis = k;
            }
        }
        nearest = nearest && fabsf(s.normal[axis]) == 1.0f;
    }
    CHECK(inside, "within the box");
    CHECK(nearest, "the normal along the nearest face");
    for (int32_t k = 0; k < 3; ++k)
    {
        CHECK(NearMean(sums[k] / SAMPLES, 0.5, sqrt(1.0 / 12.0)), "uniform along each axis");
    }

    // Its surface: faces by area (6 : 3 : 2 for the X, Y and Z faces).
    box.thickness = 0.0f;
    compiled = Compile(&box);
    int64_t faces[3] = {0};
    bool onFace = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        mvfxShapeSample s = Sample(&compiled, i);
        int32_t face = -1;
        for (int32_t k = 0; k < 3; ++k)
        {
            if (s.normal[k] != 0.0f)
            {
                face = k;
            }
            onFace = onFace && fabsf(s.position[k]) <= box.halfExtents[k];
        }
        onFace = onFace && face >= 0 && fabsf(s.position[face]) == box.halfExtents[face] &&
                 (s.position[face] > 0.0f) == (s.normal[face] > 0.0f);
        faces[face < 0 ? 0 : face] += 1;
    }
    CHECK(onFace, "on the face its normal names");
    static const double areas[3] = {6.0 / 11.0, 3.0 / 11.0, 2.0 / 11.0};
    for (int32_t k = 0; k < 3; ++k)
    {
        double deviation = sqrt(SAMPLES * areas[k] * (1.0 - areas[k]));
        CHECK(fabs((double)faces[k] - SAMPLES * areas[k]) <= 5.0 * deviation, "faces by area");
    }

    // A shell half as deep: outside the inner box, and the share beyond the
    // inner box along each axis as its volume gives.
    box.thickness = 0.5f;
    compiled = Compile(&box);
    double inner[3] = {0.5, 1.0, 1.5};
    double outerVolume = 1.0 * 2.0 * 3.0;
    double shellVolume = outerVolume - inner[0] * inner[1] * inner[2];
    int64_t beyond[3] = {0};
    bool inShell = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        mvfxShapeSample s = Sample(&compiled, i);
        bool out = false;
        for (int32_t k = 0; k < 3; ++k)
        {
            inShell = inShell && fabsf(s.position[k]) <= box.halfExtents[k];
            bool past = fabs((double)s.position[k]) >= inner[k];
            out = out || past;
            beyond[k] += past ? 1 : 0;
        }
        inShell = inShell && out;
    }
    CHECK(inShell, "within the shell");
    for (int32_t k = 0; k < 3; ++k)
    {
        double share = ((double)box.halfExtents[k] - inner[k]) * outerVolume /
                       (double)box.halfExtents[k] / shellVolume;
        double deviation = sqrt(SAMPLES * share * (1.0 - share));
        CHECK(fabs((double)beyond[k] - SAMPLES * share) <= 5.0 * deviation,
              "each axis's share of the shell by volume");
    }
}

static void TestCone(void)
{
    // From a base of radius 1 opening by 0.5 rad: on the rim, the direction
    // leans by the whole angle.
    mvfxShape cone = BaseShape(mvfx_shapeCone);
    cone.radius = 1.0f;
    cone.length = 2.0f;
    cone.angle = 0.5f;
    cone.thickness = 0.0f;
    mvfxCompiledShape compiled = Compile(&cone);
    bool rim = true;
    for (uint32_t i = 0; i < 2000; ++i)
    {
        mvfxShapeSample s = Sample(&compiled, i);
        rim = rim && s.position[2] == 0.0f && fabs(Length(s.position) - 1.0) <= 1e-6 &&
              fabs(acos((double)s.normal[2]) - 0.5) <= 1e-4;
    }
    CHECK(rim, "the rim leans by the angle");

    // From an apex: within the angle, a uniform cosine over the cap.
    cone.radius = 0.0f;
    compiled = Compile(&cone);
    double sumCosine = 0.0;
    bool within = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        mvfxShapeSample s = Sample(&compiled, i);
        within = within && Length(s.position) == 0.0 && s.normal[2] >= cosf(0.5f) - 1e-6f &&
                 fabs(Length(s.normal) - 1.0) <= 1e-5;
        sumCosine += (double)s.normal[2];
    }
    double capCosine = cos(0.5);
    CHECK(within, "from the apex within the angle");
    CHECK(NearMean(sumCosine / SAMPLES, 0.5 * (1.0 + capCosine), (1.0 - capCosine) / sqrt(12.0)),
          "a uniform cosine over the cap");

    // Its volume: within the frustum, heights by the area of the cross
    // sections, against the mean height integrated in binary64.
    cone.radius = 1.0f;
    cone.coneVolume = true;
    cone.thickness = 1.0f;
    compiled = Compile(&cone);
    double k = tan(0.5);
    double moment = 0.0;
    double mass = 0.0;
    double moment2 = 0.0;
    for (int32_t i = 0; i < 100000; ++i)
    {
        double z = 2.0 * (i + 0.5) / 100000.0;
        double area = (1.0 + k * z) * (1.0 + k * z);
        mass += area;
        moment += z * area;
        moment2 += z * z * area;
    }
    double meanHeight = moment / mass;
    double deviation = sqrt(moment2 / mass - meanHeight * meanHeight);
    double sumHeight = 0.0;
    bool inFrustum = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        mvfxShapeSample s = Sample(&compiled, i);
        double z = (double)s.position[2];
        double ring = sqrt((double)s.position[0] * (double)s.position[0] +
                           (double)s.position[1] * (double)s.position[1]);
        inFrustum = inFrustum && z >= 0.0 && z <= 2.0 && ring <= (1.0 + k * z) * (1.0 + 1e-5);
        sumHeight += z;
    }
    CHECK(inFrustum, "within the frustum");
    CHECK(NearMean(sumHeight / SAMPLES, meanHeight, deviation), "heights by cross section");
}

static void TestTurnAndOffset(void)
{
    // A quarter turn about Z and an offset: each sample is the unturned one
    // turned, then moved.
    mvfxShape plain = BaseShape(mvfx_shapeBox);
    plain.halfExtents[0] = 1.0f;
    plain.halfExtents[1] = 2.0f;
    plain.halfExtents[2] = 3.0f;
    plain.thickness = 0.0f;
    mvfxShape turned = plain;
    turned.rotation[2] = sinf(0.25f * PI_F);
    turned.rotation[3] = cosf(0.25f * PI_F);
    turned.offset[0] = 10.0f;
    turned.offset[1] = -5.0f;
    turned.offset[2] = 2.5f;
    mvfxCompiledShape a = Compile(&plain);
    mvfxCompiledShape b = Compile(&turned);
    bool agree = true;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        mvfxShapeSample p = Sample(&a, i);
        mvfxShapeSample q = Sample(&b, i);
        // A quarter turn about Z takes (x, y, z) to (-y, x, z).
        agree = agree && fabsf(q.position[0] - (10.0f - p.position[1])) <= 1e-5f &&
                fabsf(q.position[1] - (-5.0f + p.position[0])) <= 1e-5f &&
                fabsf(q.position[2] - (2.5f + p.position[2])) <= 1e-5f &&
                fabsf(q.normal[0] + p.normal[1]) <= 1e-6f &&
                fabsf(q.normal[1] - p.normal[0]) <= 1e-6f &&
                fabsf(q.local[0] + p.local[1]) <= 1e-5f;
    }
    CHECK(agree, "turned and moved");
}

static void TestArcModes(void)
{
    mvfxShape shape = BaseShape(mvfx_shapeCircle);
    shape.radius = 1.0f;
    shape.arcSpeed = 2.0f;
    mvfxWords4 words = Words(5u, 1u);
    shape.arcMode = mvfx_arcLoop;
    mvfxCompiledShape compiled = Compile(&shape);
    CHECK(mvfxArcFraction(&compiled, words, 0.25, 0, 1) == 0.5f, "a loop half way at 2 turns/s");
    CHECK(mvfxArcFraction(&compiled, words, 1.125, 0, 1) == 0.25f, "and around again");
    shape.arcMode = mvfx_arcPingPong;
    shape.arcSpeed = 1.0f;
    compiled = Compile(&shape);
    CHECK(mvfxArcFraction(&compiled, words, 0.5, 0, 1) == 0.5f, "ping-pong out");
    CHECK(mvfxArcFraction(&compiled, words, 1.5, 0, 1) == 0.5f, "ping-pong back");
    CHECK(mvfxArcFraction(&compiled, words, 1.0, 0, 1) == 1.0f, "ping-pong at the turn");
    shape.arcMode = mvfx_arcBurstSpread;
    compiled = Compile(&shape);
    CHECK(mvfxArcFraction(&compiled, words, 0.0, 3, 8) == 0.375f, "a burst spread evenly");
    CHECK(mvfxArcFraction(&compiled, words, 0.0, 0, 1) == 0.0f, "a single birth at the start");
    shape.arcMode = mvfx_arcRandom;
    compiled = Compile(&shape);
    CHECK(mvfxArcFraction(&compiled, words, 0.7, 2, 9) == mvfxUnitFloat(words.w), "random");
}

static mvfxCompiledVelocity CompileVelocity(const mvfxVelocity* velocity)
{
    mvfxCompiledVelocity compiled;
    CHECK(mvfxIsVelocityValid(velocity), "a valid velocity");
    mvfxCompileVelocity(velocity, &compiled);
    return compiled;
}

static void TestVelocities(void)
{
    mvfxShape sphere = BaseShape(mvfx_shapeSphere);
    sphere.radius = 1.0f;
    sphere.offset[0] = 4.0f;
    mvfxCompiledShape shape = Compile(&sphere);

    // Fixed: along the normalized direction, at a speed in its range.
    mvfxVelocity fixed = {
        .mode = mvfx_directionFixed, .direction = {0.0f, 3.0f, 4.0f}, .speed = {2.0f, 3.0f}};
    mvfxCompiledVelocity compiled = CompileVelocity(&fixed);
    double sumSpeed = 0.0;
    int64_t speedBins[4] = {0};
    bool along = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        mvfxShapeSample s = Sample(&shape, i);
        float v[3];
        mvfxDrawVelocity(&compiled, &s, Words(i, 9u), v);
        double speed = Length(v);
        along = along && speed >= 2.0 - 1e-6 && speed <= 3.0 + 1e-6 &&
                fabs((double)v[1] / speed - 0.6) <= 1e-6 &&
                fabs((double)v[2] / speed - 0.8) <= 1e-6;
        sumSpeed += speed;
        int32_t bin = (int32_t)((speed - 2.0) * 4.0);
        speedBins[bin < 0 ? 0 : (bin > 3 ? 3 : bin)] += 1;
    }
    CHECK(along, "fixed: along the direction, within the speeds");
    CHECK(NearMean(sumSpeed / SAMPLES, 2.5, sqrt(1.0 / 12.0)), "speeds uniform");
    CHECK(EvenBins(speedBins, 4), "speeds in even bins");

    // A cone of 0.3 rad about -X: within it, a uniform cosine, no lean
    // aside.
    mvfxVelocity cone = {.mode = mvfx_directionCone,
                         .direction = {-1.0f, 0.0f, 0.0f},
                         .coneAngle = 0.3f,
                         .speed = {1.0f, 1.0f}};
    compiled = CompileVelocity(&cone);
    double sums[3] = {0.0};
    bool within = true;
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        mvfxShapeSample s = Sample(&shape, i);
        float v[3];
        mvfxDrawVelocity(&compiled, &s, Words(i, 9u), v);
        within = within && -v[0] >= cosf(0.3f) - 1e-5f && fabs(Length(v) - 1.0) <= 1e-5;
        for (int32_t k = 0; k < 3; ++k)
        {
            sums[k] += (double)v[k];
        }
    }
    double capCosine = cos(0.3);
    CHECK(within, "a cone's directions within its angle");
    CHECK(NearMean(-sums[0] / SAMPLES, 0.5 * (1.0 + capCosine), (1.0 - capCosine) / sqrt(12.0)),
          "a uniform cosine about the axis");
    CHECK(NearMean(sums[1] / SAMPLES, 0.0, 0.3) && NearMean(sums[2] / SAMPLES, 0.0, 0.3),
          "no lean aside");

    // From the center and along the normal: parallel to where the particle
    // is born on the sphere, the offset aside.
    static const mvfxDirectionMode radial[2] = {mvfx_directionFromCenter,
                                                mvfx_directionShapeNormal};
    for (int32_t m = 0; m < 2; ++m)
    {
        mvfxVelocity out = {.mode = radial[m], .speed = {5.0f, 5.0f}};
        compiled = CompileVelocity(&out);
        bool outward = true;
        for (uint32_t i = 0; i < 2000; ++i)
        {
            mvfxShapeSample s = Sample(&shape, i);
            float v[3];
            mvfxDrawVelocity(&compiled, &s, Words(i, 9u), v);
            double r = Length(s.local);
            for (int32_t k = 0; k < 3; ++k)
            {
                outward = outward && fabs((double)v[k] / 5.0 - (double)s.local[k] / r) <= 1e-5;
            }
        }
        CHECK(outward, "outward from the center");
    }

    // Randomized wholly: any way at all, whatever the mode.
    fixed.randomize = 1.0f;
    compiled = CompileVelocity(&fixed);
    double mean[3] = {0.0};
    for (uint32_t i = 0; i < SAMPLES; ++i)
    {
        mvfxShapeSample s = Sample(&shape, i);
        float v[3];
        mvfxDrawVelocity(&compiled, &s, Words(i, 9u), v);
        for (int32_t k = 0; k < 3; ++k)
        {
            mean[k] += (double)v[k] / Length(v);
        }
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        CHECK(NearMean(mean[k] / SAMPLES, 0.0, sqrt(1.0 / 3.0)), "randomized: no way favored");
    }
}

int main(void)
{
    TestSphere();
    TestCircleAndLine();
    TestBox();
    TestCone();
    TestTurnAndOffset();
    TestArcModes();
    TestVelocities();
    return s_failures == 0 ? 0 : 1;
}
