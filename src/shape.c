// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Spawn shapes: validation, compilation, sampling.

#include "shape.h"

#include "direction.h"
#include "elementary.h"
#include "mesh.h"
#include "refusal.h"

#include <math.h>

#define TWO_PI 6.28318530717958647692f

// Whether low <= x <= high; false for NaN.
static bool IsWithin(float x, float low, float high)
{
    return x >= low && x <= high;
}

// A size's status: from 0 up to MVFX_MAX_EXTENT.
static mvfxResult CheckSize(float x)
{
    return mvfxUpToBound(x, 0.0f, MVFX_MAX_EXTENT);
}

mvfxResult mvfxCheckShape(const mvfxShape* shape)
{
    double square = 0.0;
    for (int32_t i = 0; i < 4; ++i)
    {
        if (!IsWithin(shape->rotation[i], -1.001f, 1.001f))
        {
            return mvfx_errorInvalid;
        }
        square += (double)shape->rotation[i] * (double)shape->rotation[i];
    }
    bool arcValid = shape->arc > 0.0f && shape->arc <= TWO_PI;
    if (!(fabs(sqrt(square) - 1.0) <= 0.001) || shape->arcMode > mvfx_arcBurstSpread ||
        !IsWithin(shape->arcSpeed, 0.0f, 1000.0f) || !IsWithin(shape->thickness, 0.0f, 1.0f) ||
        ((shape->kind == mvfx_shapeCircle || shape->kind == mvfx_shapeCone) && !arcValid))
    {
        return mvfx_errorInvalid;
    }
    for (int32_t i = 0; i < 3; ++i)
    {
        mvfxResult offset = mvfxBounded(shape->offset[i], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT);
        if (offset != mvfx_success)
        {
            return offset;
        }
    }
    switch (shape->kind)
    {
    case mvfx_shapePoint:
        return mvfx_success;
    case mvfx_shapeLine:
        return CheckSize(shape->length);
    case mvfx_shapeCircle:
    case mvfx_shapeSphere:
    case mvfx_shapeHemisphere:
        return CheckSize(shape->radius);
    case mvfx_shapeBox:
        return mvfxFirst(CheckSize(shape->halfExtents[0]),
                         mvfxFirst(CheckSize(shape->halfExtents[1]), CheckSize(shape->halfExtents[2])));
    case mvfx_shapeMesh:
        return mvfxRequire(shape->meshMode <= mvfx_meshVolume);
    case mvfx_shapeCone:
    {
        mvfxResult sizes =
            mvfxFirst(mvfxUpToBound(shape->angle, 0.0f, MVFX_MAX_CONE_ANGLE),
                      mvfxFirst(CheckSize(shape->radius), CheckSize(shape->length)));
        if (sizes != mvfx_success)
        {
            return sizes;
        }
        float sine = 0.0f;
        float cosine = 0.0f;
        mvfxSinCos(shape->angle, &sine, &cosine);
        return mvfxWithinBound((double)shape->radius +
                                   (double)shape->length * (double)(sine / cosine) <=
                               (double)MVFX_MAX_EXTENT);
    }
    default:
        return mvfx_errorInvalid;
    }
}

void mvfxCompileShape(const mvfxShape* shape, struct mvfxMesh* mesh, mvfxCompiledShape* out)
{
    // The quaternion normalized in binary64, then its matrix.
    double q[4];
    double square = 0.0;
    for (int32_t i = 0; i < 4; ++i)
    {
        q[i] = (double)shape->rotation[i];
        square += q[i] * q[i];
    }
    double scale = 1.0 / sqrt(square);
    double x = q[0] * scale;
    double y = q[1] * scale;
    double z = q[2] * scale;
    double w = q[3] * scale;
    double m[9] = {
        1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w),       2.0 * (x * z + y * w),
        2.0 * (x * y + z * w),       1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w),
        2.0 * (x * z - y * w),       2.0 * (y * z + x * w),       1.0 - 2.0 * (x * x + y * y),
    };
    *out = (mvfxCompiledShape){
        .kind = shape->kind,
        .arcMode = shape->arcMode,
        .coneVolume = shape->coneVolume,
        .radius = shape->radius,
        .length = shape->length,
        .halfExtents = {shape->halfExtents[0], shape->halfExtents[1], shape->halfExtents[2]},
        .thickness = shape->thickness,
        .arc = shape->arc,
        .arcSpeed = shape->arcSpeed,
        .meshMode = shape->meshMode,
        .mesh = mesh,
    };
    for (int32_t i = 0; i < 9; ++i)
    {
        out->matrix[i] = (float)m[i];
    }
    for (int32_t i = 0; i < 3; ++i)
    {
        out->offset[i] = shape->offset[i];
    }
    if (shape->kind == mvfx_shapeCone)
    {
        float sine = 0.0f;
        float cosine = 0.0f;
        mvfxSinCos(shape->angle, &sine, &cosine);
        out->tangent = sine / cosine;
    }
}

float mvfxArcFraction(const mvfxCompiledShape* shape, mvfxWords4 words, double age,
                      int64_t batchIndex, int64_t batchCount)
{
    switch (shape->arcMode)
    {
    case mvfx_arcLoop:
    {
        double turns = age * (double)shape->arcSpeed;
        return (float)(turns - floor(turns));
    }
    case mvfx_arcPingPong:
    {
        double half = age * (double)shape->arcSpeed * 0.5;
        double there = 2.0 * (half - floor(half));
        return (float)(there <= 1.0 ? there : 2.0 - there);
    }
    case mvfx_arcBurstSpread:
        return batchCount > 1 ? (float)((double)batchIndex / (double)batchCount) : 0.0f;
    default:
        return mvfxUnitFloat(words.w);
    }
}

// The radius of a point uniform over the ring between (1 - thickness)
// radius and radius, by area.
static float RingRadius(float radius, float thickness, float u)
{
    float inner = radius * (1.0f - thickness);
    float innerSquare = inner * inner;
    return sqrtf(innerSquare + u * (radius * radius - innerSquare));
}

// The cube root of x >= 0.
static float CubeRoot(float x)
{
    return mvfxPow(x, 1.0f / 3.0f);
}

static void SampleSphere(const mvfxCompiledShape* shape, mvfxWords4 words, bool hemisphere,
                         float position[3], float normal[3])
{
    mvfxRandomDirection(words.x, words.y, normal);
    if (hemisphere)
    {
        normal[2] = fabsf(normal[2]);
    }
    float inner = shape->radius * (1.0f - shape->thickness);
    float innerCube = inner * inner * inner;
    float outerCube = shape->radius * shape->radius * shape->radius;
    float radius = CubeRoot(innerCube + mvfxUnitFloat(words.z) * (outerCube - innerCube));
    radius = shape->thickness == 0.0f ? shape->radius : radius;
    for (int32_t i = 0; i < 3; ++i)
    {
        position[i] = radius * normal[i];
    }
}

// A box's volume, uniformly, the normal along its nearest face.
static void SampleBoxVolume(const float h[3], mvfxWords4 words, float position[3], float normal[3])
{
    float best = -1.0f;
    int32_t axis = 2;
    uint32_t draws[3] = {words.x, words.y, words.z};
    for (int32_t i = 0; i < 3; ++i)
    {
        position[i] = (2.0f * mvfxUnitFloat(draws[i]) - 1.0f) * h[i];
        float reach = h[i] > 0.0f ? fabsf(position[i]) / h[i] : 0.0f;
        if (reach > best)
        {
            best = reach;
            axis = i;
        }
    }
    normal[axis] = position[axis] < 0.0f ? -1.0f : 1.0f;
}

// The axis of the slab a draw chooses, by the slabs' weights; should the
// product round onto the total, the heaviest axis rather than an empty one.
static int32_t ChooseSlab(const float weights[3], float total, uint32_t word)
{
    float pick = mvfxUnitFloat(word) * total;
    int32_t axis = pick < weights[0] ? 0 : (pick < weights[0] + weights[1] ? 1 : 2);
    if (weights[axis] > 0.0f)
    {
        return axis;
    }
    int32_t heaviest = weights[1] > weights[0] ? 1 : 0;
    return weights[2] > weights[heaviest] ? 2 : heaviest;
}

// A box's surface or shell: one of six slabs chosen by volume (by area on
// the surface), the normal along the slab's axis. The slabs along Z span
// the box's X and Y; those along Y its X and the inner Z; those along X
// the inner Y and Z: together they fill the shell once.
static void SampleBoxShell(const float h[3], float t, mvfxWords4 words, float position[3],
                           float normal[3])
{
    float inner[3] = {h[0] * (1.0f - t), h[1] * (1.0f - t), h[2] * (1.0f - t)};
    float depth[3] = {t > 0.0f ? h[0] - inner[0] : 1.0f, t > 0.0f ? h[1] - inner[1] : 1.0f,
                      t > 0.0f ? h[2] - inner[2] : 1.0f};
    float weights[3] = {
        inner[1] * inner[2] * depth[0],
        h[0] * inner[2] * depth[1],
        h[0] * h[1] * depth[2],
    };
    float total = weights[0] + weights[1] + weights[2];
    if (!(total > 0.0f))
    {
        position[0] = 0.0f;
        position[1] = 0.0f;
        position[2] = 0.0f;
        mvfxRandomDirection(words.x, words.y, normal);
        return;
    }
    int32_t axis = ChooseSlab(weights, total, words.x);
    // The spans of the slab's other two axes.
    float spans[3] = {
        axis == 0 ? inner[0] : h[0],
        axis == 2 ? h[1] : inner[1],
        inner[2],
    };
    float side = (words.x & 1u) != 0 ? -1.0f : 1.0f;
    uint32_t free[2] = {words.y, words.z};
    int32_t used = 0;
    for (int32_t i = 0; i < 3; ++i)
    {
        position[i] = i == axis ? side * (inner[i] + mvfxUnitFloat(words.w) * (h[i] - inner[i]))
                                : (2.0f * mvfxUnitFloat(free[used++]) - 1.0f) * spans[i];
    }
    normal[axis] = side;
}

static void SampleBox(const mvfxCompiledShape* shape, mvfxWords4 words, float position[3],
                      float normal[3])
{
    normal[0] = 0.0f;
    normal[1] = 0.0f;
    normal[2] = 0.0f;
    if (shape->thickness == 1.0f)
    {
        SampleBoxVolume(shape->halfExtents, words, position, normal);
    }
    else
    {
        SampleBoxShell(shape->halfExtents, shape->thickness, words, position, normal);
    }
}

// A cone: on its base, a ring of the base's radius; in its volume, a
// height drawn by the area of its cross sections (the cube of the radius
// at that height is uniform), then a ring there. The direction along its
// slant leans from +Z by the cone's angle in proportion to the distance
// from the axis; from an apex (a base of radius 0), uniformly within the
// cone's angle.
static void SampleCone(const mvfxCompiledShape* shape, mvfxWords4 words, float arcFraction,
                       float position[3], float normal[3])
{
    float sine = 0.0f;
    float cosine = 0.0f;
    mvfxSinCos(arcFraction * shape->arc, &sine, &cosine);
    float height = 0.0f;
    float radius = shape->radius;
    if (shape->coneVolume)
    {
        float k = shape->tangent;
        float u = mvfxUnitFloat(words.z);
        if (k > 0.0f && shape->length > 0.0f)
        {
            float top = shape->radius + k * shape->length;
            float baseCube = shape->radius * shape->radius * shape->radius;
            float topCube = top * top * top;
            float reached = CubeRoot(baseCube + u * (topCube - baseCube));
            height = fminf((reached - shape->radius) / k, shape->length);
            height = fmaxf(height, 0.0f);
        }
        else
        {
            height = u * shape->length;
        }
        radius = shape->radius + k * height;
    }
    float ring = RingRadius(radius, shape->thickness, mvfxUnitFloat(words.x));
    position[0] = ring * cosine;
    position[1] = ring * sine;
    position[2] = height;
    if (radius > 0.0f)
    {
        float lean = ring / radius * shape->tangent;
        normal[0] = lean * cosine;
        normal[1] = lean * sine;
        normal[2] = 1.0f;
        (void)mvfxNormalize(normal);
        return;
    }
    // From the apex: a uniform cosine over the cap.
    float capCosine = 1.0f / sqrtf(1.0f + shape->tangent * shape->tangent);
    float z = 1.0f - mvfxUnitFloat(words.y) * (1.0f - capCosine);
    float across = sqrtf(fmaxf(0.0f, 1.0f - z * z));
    normal[0] = across * cosine;
    normal[1] = across * sine;
    normal[2] = z;
}

// Turns v by the shape's rotation.
static void Turn(const mvfxCompiledShape* shape, const float v[3], float out[3])
{
    const float* m = shape->matrix;
    for (int32_t r = 0; r < 3; ++r)
    {
        out[r] = m[3 * r] * v[0] + m[3 * r + 1] * v[1] + m[3 * r + 2] * v[2];
    }
}

void mvfxSampleShape(const mvfxCompiledShape* shape, const struct mvfxMesh* mesh, mvfxWords4 words,
                     float arcFraction, mvfxShapeSample* out)
{
    float position[3] = {0.0f, 0.0f, 0.0f};
    float normal[3] = {0.0f, 0.0f, 1.0f};
    switch (shape->kind)
    {
    case mvfx_shapeLine:
        position[0] = (arcFraction - 0.5f) * shape->length;
        normal[2] = 0.0f;
        normal[1] = 1.0f;
        break;
    case mvfx_shapeCircle:
    {
        float sine = 0.0f;
        float cosine = 0.0f;
        mvfxSinCos(arcFraction * shape->arc, &sine, &cosine);
        float ring = RingRadius(shape->radius, shape->thickness, mvfxUnitFloat(words.x));
        position[0] = ring * cosine;
        position[1] = ring * sine;
        normal[0] = cosine;
        normal[1] = sine;
        normal[2] = 0.0f;
        break;
    }
    case mvfx_shapeSphere:
    case mvfx_shapeHemisphere:
        SampleSphere(shape, words, shape->kind == mvfx_shapeHemisphere, position, normal);
        break;
    case mvfx_shapeBox:
        SampleBox(shape, words, position, normal);
        break;
    case mvfx_shapeCone:
        SampleCone(shape, words, arcFraction, position, normal);
        break;
    case mvfx_shapeMesh:
        mvfxSampleMesh(mesh, shape->meshMode, words, position, normal);
        break;
    default:
        mvfxRandomDirection(words.x, words.y, normal);
        break;
    }
    Turn(shape, position, out->local);
    Turn(shape, normal, out->normal);
    for (int32_t i = 0; i < 3; ++i)
    {
        out->position[i] = out->local[i] + shape->offset[i];
    }
}
