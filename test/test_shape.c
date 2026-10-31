// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Spawn shapes and initial velocities against their geometry, through the
// particles a burst gives: every point on or in its shape, uniformity by
// moments and bin counts within five standard deviations at fixed seeds,
// normals (as the velocity along them), turns and offsets, arc modes from
// the particles' birth times, and velocities by direction mode,
// randomization and speed.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

#define SAMPLES 40000
#define PI_F    3.14159265358979323846f
#define PI_D    3.14159265358979323846
// Every particle's life, longer than any run.
#define LIFETIME 100.0f

static TestParticle s_read[SAMPLES];
static float s_positions[SAMPLES * 3];
static float s_normals[SAMPLES * 3];
static float s_velocities[SAMPLES * 3];

static mvfxShape BaseShape(mvfxShapeKind kind)
{
    return (mvfxShape){
        .kind = kind,
        .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
        .arc = 2.0f * PI_F,
        .thickness = 1.0f,
    };
}

// An emitter of shape and velocity, with a burst or a rate and room for
// capacity particles, stepped ticks; copies its particles' positions (from
// the instance's origin) and velocities, in the pool's order, into either
// array not NULL, and their ages over their LIFETIME second lives into ages
// when not NULL, and returns how many there are.
static int32_t Run(const mvfxShape* shape, const mvfxVelocity* velocity, const mvfxBurst* burst,
                   float rate, int32_t capacity, int64_t ticks, float* positions, float* velocities,
                   float* ages)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = rate;
    emitter.capacity = capacity;
    emitter.lifetime = mvfxMakeRangeSource(LIFETIME, LIFETIME);
    emitter.bursts = burst;
    emitter.burstCount = burst != NULL ? 1 : 0;
    emitter.shape = *shape;
    emitter.velocity = *velocity;
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
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    // In steps of at most a second, the longest a step takes.
    bool stepped = true;
    for (int64_t done = 0; done < ticks; done += MVFX_TICKS_PER_SECOND)
    {
        int64_t part = ticks - done < MVFX_TICKS_PER_SECOND ? ticks - done : MVFX_TICKS_PER_SECOND;
        stepped = stepped && mvfxContext_StepTicks(context, part) == mvfx_success;
    }
    CHECK(stepped, "step");
    int32_t count = ReadParticles(context, instance, 0, s_read, capacity);
    CHECK(count >= 0, "read");
    for (int32_t i = 0; i < count; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            if (positions != NULL)
            {
                positions[3 * i + k] = s_read[i].position[k];
            }
            if (velocities != NULL)
            {
                velocities[3 * i + k] = s_read[i].velocity[k];
            }
        }
        if (ages != NULL)
        {
            ages[i] = s_read[i].age;
        }
    }
    mvfxDestroyContext(context);
    return count;
}

// A burst of count particles of shape with velocity, after a step of a
// tick.
static void Burst(const mvfxShape* shape, const mvfxVelocity* velocity, int32_t count,
                  float* positions, float* velocities)
{
    const mvfxBurst burst = {
        .time = 0.0f, .minCount = count, .maxCount = count, .cycles = 1, .probability = 1.0f};
    CHECK(Run(shape, velocity, &burst, 0.0f, count, 1, positions, velocities, NULL) == count,
          "every particle born");
}

// The births of a burst of count particles: their places, still, into
// s_positions, and their shape's normals there, as their velocities at
// speed 1 along them, into s_normals; the same particles, drawn alike.
static void Births(const mvfxShape* shape, int32_t count)
{
    const mvfxVelocity still = {.mode = mvfx_directionShapeNormal, .speed = {0.0f, 0.0f}};
    const mvfxVelocity normal = {.mode = mvfx_directionShapeNormal, .speed = {1.0f, 1.0f}};
    Burst(shape, &still, count, s_positions, NULL);
    Burst(shape, &normal, count, NULL, s_normals);
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
        Births(&shape, SAMPLES);
        double inner = 2.0 * (1.0 - (double)thicknesses[t]);
        double sumRadius = 0.0;
        double sumZ = 0.0;
        int64_t octants[8] = {0};
        bool inside = true;
        bool normals = true;
        for (int32_t i = 0; i < SAMPLES; ++i)
        {
            const float* p = &s_positions[3 * i];
            const float* n = &s_normals[3 * i];
            double r = Length(p);
            inside = inside && r <= 2.0 * (1.0 + 1e-6) && r >= inner * (1.0 - 1e-6);
            for (int32_t k = 0; k < 3; ++k)
            {
                normals = normals && fabs((double)n[k] - (double)p[k] / r) <= 1e-5;
            }
            for (int32_t k = 0; k < 3 && thicknesses[t] == 0.0f; ++k)
            {
                inside = inside && p[k] == 2.0f * n[k];
            }
            sumRadius += r;
            sumZ += (double)p[2];
            octants[(p[0] < 0.0f ? 1 : 0) + (p[1] < 0.0f ? 2 : 0) + (p[2] < 0.0f ? 4 : 0)] += 1;
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

    // On the surface the radius is exact, not a cube root's rounding of it
    // (the cube root of 0.1 cubed comes back an ulp short).
    mvfxShape surface = BaseShape(mvfx_shapeSphere);
    surface.radius = 0.1f;
    surface.thickness = 0.0f;
    Births(&surface, 2000);
    bool exact = true;
    for (int32_t i = 0; i < 3 * 2000; ++i)
    {
        exact = exact && s_positions[i] == 0.1f * s_normals[i];
    }
    CHECK(exact, "a surface's radius exact");

    mvfxShape hemi = BaseShape(mvfx_shapeHemisphere);
    hemi.radius = 1.0f;
    hemi.thickness = 0.0f;
    Births(&hemi, SAMPLES);
    double sumZ = 0.0;
    bool upper = true;
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        float z = s_positions[3 * i + 2];
        upper = upper && z >= 0.0f;
        sumZ += (double)z;
    }
    CHECK(upper, "a hemisphere above its plane");
    // Z is uniform over [0, 1] on a hemisphere's surface.
    CHECK(NearMean(sumZ / SAMPLES, 0.5, sqrt(1.0 / 12.0)), "a hemisphere's mean height");
}

// The sector of eight about Z a place's angle falls in.
static int32_t Sector(const float* p)
{
    double angle = atan2((double)p[1], (double)p[0]);
    int32_t sector = (int32_t)floor((angle + PI_D) / (2.0 * PI_D) * 8.0);
    return sector < 0 ? 0 : (sector > 7 ? 7 : sector);
}

static void TestCircleAndLine(void)
{
    mvfxShape disc = BaseShape(mvfx_shapeCircle);
    disc.radius = 3.0f;
    Births(&disc, SAMPLES);
    double sumRadius = 0.0;
    int64_t sectors[8] = {0};
    bool flat = true;
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        const float* p = &s_positions[3 * i];
        flat = flat && p[2] == 0.0f && Length(p) <= 3.0 * (1.0 + 1e-6);
        sumRadius += Length(p);
        sectors[Sector(p)] += 1;
    }
    CHECK(flat, "a disc in its plane");
    CHECK(NearMean(sumRadius / SAMPLES, 2.0, 3.0 * sqrt(1.0 / 18.0)), "a disc's mean radius 2/3 r");
    CHECK(EvenBins(sectors, 8), "every sector alike");

    disc.thickness = 0.0f;
    disc.arc = 0.5f * PI_F;
    Births(&disc, SAMPLES);
    bool onEdge = true;
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        const float* p = &s_positions[3 * i];
        const float* n = &s_normals[3 * i];
        onEdge = onEdge && fabs(Length(p) - 3.0) <= 3e-6 && p[0] >= -1e-6f && p[1] >= -1e-6f &&
                 fabsf(n[0] * 3.0f - p[0]) <= 1e-5f;
    }
    CHECK(onEdge, "an arc's edge in its quarter, the normal outward");

    mvfxShape line = BaseShape(mvfx_shapeLine);
    line.length = 4.0f;
    Births(&line, SAMPLES);
    int64_t quarters[4] = {0};
    bool along = true;
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        const float* p = &s_positions[3 * i];
        along = along && fabsf(p[0]) <= 2.0f && p[1] == 0.0f && p[2] == 0.0f &&
                s_normals[3 * i + 1] == 1.0f;
        int32_t q = (int32_t)((p[0] + 2.0f) / 1.0f);
        quarters[q > 3 ? 3 : q] += 1;
    }
    CHECK(along, "a line along X, its normal +Y");
    CHECK(EvenBins(quarters, 4), "uniform along a line");
}

static void TestBoxVolume(void)
{
    // Its volume: uniform per axis, the normal along the nearest face.
    mvfxShape box = BaseShape(mvfx_shapeBox);
    box.halfExtents[0] = 1.0f;
    box.halfExtents[1] = 2.0f;
    box.halfExtents[2] = 3.0f;
    Births(&box, SAMPLES);
    double sums[3] = {0.0};
    bool inside = true;
    bool nearest = true;
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        const float* p = &s_positions[3 * i];
        int32_t axis = 0;
        double best = -1.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            double reach = fabs((double)p[k]) / (double)box.halfExtents[k];
            inside = inside && reach <= 1.0;
            sums[k] += reach;
            if (reach > best)
            {
                best = reach;
                axis = k;
            }
        }
        nearest = nearest && fabsf(s_normals[3 * i + axis]) == 1.0f;
    }
    CHECK(inside, "within the box");
    CHECK(nearest, "the normal along the nearest face");
    for (int32_t k = 0; k < 3; ++k)
    {
        CHECK(NearMean(sums[k] / SAMPLES, 0.5, sqrt(1.0 / 12.0)), "uniform along each axis");
    }
}

static void TestBoxSurfaceAndShell(void)
{
    // Its surface: faces by area (6 : 3 : 2 for the X, Y and Z faces).
    mvfxShape box = BaseShape(mvfx_shapeBox);
    box.halfExtents[0] = 1.0f;
    box.halfExtents[1] = 2.0f;
    box.halfExtents[2] = 3.0f;
    box.thickness = 0.0f;
    Births(&box, SAMPLES);
    int64_t faces[3] = {0};
    int64_t negative = 0;
    bool onFace = true;
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        const float* p = &s_positions[3 * i];
        const float* n = &s_normals[3 * i];
        int32_t face = -1;
        for (int32_t k = 0; k < 3; ++k)
        {
            if (n[k] != 0.0f)
            {
                face = k;
            }
            onFace = onFace && fabsf(p[k]) <= box.halfExtents[k];
        }
        onFace = onFace && face >= 0 && fabsf(p[face]) == box.halfExtents[face] &&
                 (p[face] > 0.0f) == (n[face] > 0.0f);
        faces[face < 0 ? 0 : face] += 1;
        negative += n[0] + n[1] + n[2] < 0.0f ? 1 : 0;
    }
    CHECK(onFace, "on the face its normal names");
    CHECK(fabs((double)negative - SAMPLES * 0.5) <= 5.0 * sqrt(SAMPLES * 0.25),
          "either side of the box alike");
    static const double areas[3] = {6.0 / 11.0, 3.0 / 11.0, 2.0 / 11.0};
    for (int32_t k = 0; k < 3; ++k)
    {
        double deviation = sqrt(SAMPLES * areas[k] * (1.0 - areas[k]));
        CHECK(fabs((double)faces[k] - SAMPLES * areas[k]) <= 5.0 * deviation, "faces by area");
    }

    // A shell half as deep: outside the inner box, and the share beyond the
    // inner box along each axis as its volume gives.
    box.thickness = 0.5f;
    Births(&box, SAMPLES);
    double inner[3] = {0.5, 1.0, 1.5};
    double outerVolume = 1.0 * 2.0 * 3.0;
    double shellVolume = outerVolume - inner[0] * inner[1] * inner[2];
    int64_t beyond[3] = {0};
    bool inShell = true;
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        const float* p = &s_positions[3 * i];
        bool out = false;
        for (int32_t k = 0; k < 3; ++k)
        {
            inShell = inShell && fabsf(p[k]) <= box.halfExtents[k];
            bool past = fabs((double)p[k]) >= inner[k];
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

static void TestConeBase(void)
{
    // From a base of radius 1 opening by 0.5 rad: on the rim, the direction
    // leans by the whole angle.
    mvfxShape cone = BaseShape(mvfx_shapeCone);
    cone.radius = 1.0f;
    cone.length = 2.0f;
    cone.angle = 0.5f;
    cone.thickness = 0.0f;
    Births(&cone, 2000);
    bool rim = true;
    for (int32_t i = 0; i < 2000; ++i)
    {
        const float* p = &s_positions[3 * i];
        rim = rim && p[2] == 0.0f && fabs(Length(p) - 1.0) <= 1e-6 &&
              fabs(acos((double)s_normals[3 * i + 2]) - 0.5) <= 1e-4;
    }
    CHECK(rim, "the rim leans by the angle");

    // Over the whole base: the lean in proportion to the distance from the
    // axis.
    cone.thickness = 1.0f;
    Births(&cone, 2000);
    bool leans = true;
    for (int32_t i = 0; i < 2000; ++i)
    {
        const float* n = &s_normals[3 * i];
        double ring = Length(&s_positions[3 * i]);
        leans = leans && fabs(atan2(sqrt((double)n[0] * (double)n[0] + (double)n[1] * (double)n[1]),
                                    (double)n[2]) -
                              atan(ring * tan(0.5))) <= 1e-4;
    }
    CHECK(leans, "the lean grows with the distance from the axis");

    // From an apex: within the angle, a uniform cosine over the cap.
    cone.thickness = 0.0f;
    cone.radius = 0.0f;
    Births(&cone, SAMPLES);
    double sumCosine = 0.0;
    bool within = true;
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        const float* n = &s_normals[3 * i];
        within = within && Length(&s_positions[3 * i]) == 0.0 && n[2] >= cosf(0.5f) - 1e-6f &&
                 fabs(Length(n) - 1.0) <= 1e-5;
        sumCosine += (double)n[2];
    }
    double capCosine = cos(0.5);
    CHECK(within, "from the apex within the angle");
    CHECK(NearMean(sumCosine / SAMPLES, 0.5 * (1.0 + capCosine), (1.0 - capCosine) / sqrt(12.0)),
          "a uniform cosine over the cap");
}

static void TestConeVolume(void)
{
    // Its volume: within the frustum, heights by the area of the cross
    // sections, against the mean height integrated in binary64.
    mvfxShape cone = BaseShape(mvfx_shapeCone);
    cone.radius = 1.0f;
    cone.length = 2.0f;
    cone.angle = 0.5f;
    cone.coneVolume = true;
    Births(&cone, SAMPLES);
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
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        const float* p = &s_positions[3 * i];
        double z = (double)p[2];
        double ring = sqrt((double)p[0] * (double)p[0] + (double)p[1] * (double)p[1]);
        inFrustum = inFrustum && z >= 0.0 && z <= 2.0 && ring <= (1.0 + k * z) * (1.0 + 1e-5);
        sumHeight += z;
    }
    CHECK(inFrustum, "within the frustum");
    CHECK(NearMean(sumHeight / SAMPLES, meanHeight, deviation), "heights by cross section");

    // A cylinder (an angle of 0): heights uniform.
    cone.angle = 0.0f;
    Births(&cone, SAMPLES);
    sumHeight = 0.0;
    int64_t heights[4] = {0};
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        float z = s_positions[3 * i + 2];
        sumHeight += (double)z;
        int32_t bin = (int32_t)(z * 2.0f);
        heights[bin > 3 ? 3 : bin] += 1;
    }
    CHECK(NearMean(sumHeight / SAMPLES, 1.0, 2.0 / sqrt(12.0)), "a cylinder's heights uniform");
    CHECK(EvenBins(heights, 4), "a cylinder's heights in even bins");
}

static void TestTurnAndOffset(void)
{
    // A quarter turn about Z and an offset: each birth is the unturned
    // one's turned, then moved; its direction from the center turned
    // alike.
    enum
    {
        COUNT = 1000
    };
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
    static float p[COUNT * 3];
    static float pn[COUNT * 3];
    static float pc[COUNT * 3];
    static float qc[COUNT * 3];
    Births(&plain, COUNT);
    memcpy(p, s_positions, sizeof p);
    memcpy(pn, s_normals, sizeof pn);
    const mvfxVelocity center = {.mode = mvfx_directionFromCenter, .speed = {1.0f, 1.0f}};
    Burst(&plain, &center, COUNT, NULL, pc);
    Births(&turned, COUNT);
    Burst(&turned, &center, COUNT, NULL, qc);
    bool agree = true;
    for (int32_t i = 0; i < COUNT; ++i)
    {
        const float* a = &p[3 * i];
        const float* b = &s_positions[3 * i];
        const float* m = &pn[3 * i];
        const float* n = &s_normals[3 * i];
        // A quarter turn about Z takes (x, y, z) to (-y, x, z).
        agree = agree && fabsf(b[0] - (10.0f - a[1])) <= 1e-5f &&
                fabsf(b[1] - (-5.0f + a[0])) <= 1e-5f && fabsf(b[2] - (2.5f + a[2])) <= 1e-5f &&
                fabsf(n[0] + m[1]) <= 1e-6f && fabsf(n[1] - m[0]) <= 1e-6f &&
                fabsf(qc[3 * i] + pc[3 * i + 1]) <= 1e-5f &&
                fabsf(qc[3 * i + 1] - pc[3 * i]) <= 1e-5f;
    }
    CHECK(agree, "turned and moved");

    // A quaternion 0.09 % long turns as its unit one does.
    mvfxShape longer = turned;
    for (int32_t k = 0; k < 4; ++k)
    {
        longer.rotation[k] *= 1.0009f;
    }
    memcpy(p, s_positions, sizeof p);
    Births(&longer, COUNT);
    bool same = true;
    for (int32_t i = 0; i < 3 * COUNT; ++i)
    {
        same = same && fabsf(p[i] - s_positions[i]) <= 1e-5f;
    }
    CHECK(same, "the rotation normalized");
}

// The fraction of a unit circle's whole arc a place lies at, in [0, 1).
static double ArcFraction(const float* p)
{
    double angle = atan2((double)p[1], (double)p[0]);
    return (angle < 0.0 ? angle + 2.0 * PI_D : angle) / (2.0 * PI_D);
}

// Whether two fractions of a turn agree within tolerance, around it.
static bool SameTurn(double a, double b, double tolerance)
{
    double d = fabs(a - b);
    return fmin(d, 1.0 - d) <= tolerance;
}

static void TestArcModes(void)
{
    // A rate of 64 a second over 2 s: each particle's place on a unit
    // circle's edge is where the arc stood at its birth, the step's end less
    // its age.
    enum
    {
        RATE = 64,
        ROOM = 3 * RATE
    };
    static float ages[ROOM];
    mvfxShape shape = BaseShape(mvfx_shapeCircle);
    shape.radius = 1.0f;
    shape.thickness = 0.0f;
    const mvfxVelocity still = {.mode = mvfx_directionShapeNormal, .speed = {0.0f, 0.0f}};
    for (int32_t mode = 0; mode < 2; ++mode)
    {
        shape.arcMode = mode == 0 ? mvfx_arcLoop : mvfx_arcPingPong;
        shape.arcSpeed = mode == 0 ? 2.0f : 1.0f;
        int32_t born = Run(&shape, &still, NULL, (float)RATE, ROOM, 2 * MVFX_TICKS_PER_SECOND - 1,
                           s_positions, NULL, ages);
        bool placed = born >= 2 * RATE - 1;
        for (int32_t i = 0; i < born; ++i)
        {
            double at = 2.0 - 1.0 / MVFX_TICKS_PER_SECOND - (double)LIFETIME * (double)ages[i];
            double turns = at * (double)shape.arcSpeed;
            // Loop: around and around; ping-pong: out over one turn, back
            // over the next.
            double phase = fmod(turns, 2.0);
            double want = mode == 0 ? fmod(turns, 1.0) : 1.0 - fabs(1.0 - phase);
            placed = placed && SameTurn(ArcFraction(&s_positions[3 * i]), want, 1e-4);
        }
        CHECK(placed, mode == 0 ? "a loop at 2 turns a second" : "ping-pong out and back");
    }

    // A burst of 8 spread evenly from the arc's start; a burst of 1 at it.
    shape.arcMode = mvfx_arcBurstSpread;
    Burst(&shape, &still, 8, s_positions, NULL);
    bool spread = true;
    for (int32_t i = 0; i < 8; ++i)
    {
        spread = spread && SameTurn(ArcFraction(&s_positions[3 * i]), i / 8.0, 1e-6);
    }
    CHECK(spread, "a burst spread evenly");
    Burst(&shape, &still, 1, s_positions, NULL);
    CHECK(SameTurn(ArcFraction(s_positions), 0.0, 1e-6), "a single birth at the start");

    // Random: every sector alike.
    shape.arcMode = mvfx_arcRandom;
    Burst(&shape, &still, SAMPLES, s_positions, NULL);
    int64_t sectors[8] = {0};
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        sectors[Sector(&s_positions[3 * i])] += 1;
    }
    CHECK(EvenBins(sectors, 8), "random: every sector alike");
}

// A sphere of radius 1 four along X, the velocities' shape.
static mvfxShape VelocityShape(void)
{
    mvfxShape sphere = BaseShape(mvfx_shapeSphere);
    sphere.radius = 1.0f;
    sphere.offset[0] = 4.0f;
    return sphere;
}

static void TestFixedAndCone(void)
{
    mvfxShape shape = VelocityShape();

    // Fixed: along the normalized direction, at a speed in its range.
    mvfxVelocity fixed = {
        .mode = mvfx_directionFixed, .direction = {0.0f, 3.0f, 4.0f}, .speed = {2.0f, 3.0f}};
    Burst(&shape, &fixed, SAMPLES, NULL, s_velocities);
    double sumSpeed = 0.0;
    int64_t speedBins[4] = {0};
    bool along = true;
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        const float* v = &s_velocities[3 * i];
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
    Burst(&shape, &cone, SAMPLES, NULL, s_velocities);
    double sums[3] = {0.0};
    bool within = true;
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        const float* v = &s_velocities[3 * i];
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

    // About an axis leaning down, where the basis takes its other sign.
    mvfxVelocity down = {.mode = mvfx_directionCone,
                         .direction = {0.3f, 0.0f, -1.0f},
                         .coneAngle = 0.2f,
                         .speed = {1.0f, 1.0f}};
    Burst(&shape, &down, 2000, NULL, s_velocities);
    double axisLength = sqrt(0.09 + 1.0);
    bool aboutDown = true;
    for (int32_t i = 0; i < 2000; ++i)
    {
        const float* v = &s_velocities[3 * i];
        double reach = (0.3 * (double)v[0] - (double)v[2]) / axisLength;
        aboutDown = aboutDown && reach >= cos(0.2) - 1e-5 && fabs(Length(v) - 1.0) <= 1e-5;
    }
    CHECK(aboutDown, "a cone about a downward axis");
}

static void TestOutwardAndRandom(void)
{
    // From the center and along the normal: parallel to where the particle
    // is born on the sphere, the offset aside.
    mvfxShape shape = VelocityShape();
    const mvfxVelocity still = {.mode = mvfx_directionShapeNormal, .speed = {0.0f, 0.0f}};
    Burst(&shape, &still, 2000, s_positions, NULL);
    static const mvfxDirectionMode radial[2] = {mvfx_directionFromCenter,
                                                mvfx_directionShapeNormal};
    for (int32_t m = 0; m < 2; ++m)
    {
        mvfxVelocity out = {.mode = radial[m], .speed = {5.0f, 5.0f}};
        Burst(&shape, &out, 2000, NULL, s_velocities);
        bool outward = true;
        for (int32_t i = 0; i < 2000; ++i)
        {
            const float local[3] = {s_positions[3 * i] - 4.0f, s_positions[3 * i + 1],
                                    s_positions[3 * i + 2]};
            double r = Length(local);
            for (int32_t k = 0; k < 3; ++k)
            {
                outward = outward && fabs((double)s_velocities[3 * i + k] / 5.0 -
                                          (double)local[k] / r) <= 1e-5;
            }
        }
        CHECK(outward, "outward from the center");
    }

    // Randomized wholly: any way at all, whatever the mode.
    mvfxVelocity fixed = {.mode = mvfx_directionFixed,
                          .direction = {0.0f, 3.0f, 4.0f},
                          .randomize = 1.0f,
                          .speed = {2.0f, 3.0f}};
    Burst(&shape, &fixed, SAMPLES, NULL, s_velocities);
    double mean[3] = {0.0};
    for (int32_t i = 0; i < SAMPLES; ++i)
    {
        const float* v = &s_velocities[3 * i];
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
    TestBoxVolume();
    TestBoxSurfaceAndShell();
    TestConeBase();
    TestConeVolume();
    TestTurnAndOffset();
    TestArcModes();
    TestFixedAndCone();
    TestOutwardAndRandom();
    return s_failures == 0 ? 0 : 1;
}
