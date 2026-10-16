// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A surface gas's step. Zhu and Bridson ("Animating Sand as a Fluid",
// SIGGRAPH 2005, section 5) take a point's distance to a liquid's surface
// as |x - m| - r, m the mean of the particles near x weighed by a kernel;
// Solenthaler, Schlafli and Pajarola ("A Unified Particle Model for
// Fluid-Solid Interactions", CAVW 2007) draw the surface in by a factor
// f where m moves faster than x, as it does in concavities and between
// drops, judged by the largest eigenvalue of m's Jacobian. Here the
// Jacobian is gathered exactly with the mean, from the kernel's gradient,
// and its symmetric part's largest eigenvalue taken (always real, and at
// least every eigenvalue's real part): in 2D in closed form, in 3D by
// cyclic Jacobi rotations (Kopp, "Efficient numerical diagonalization of
// hermitian 3x3 matrices", 2008: the most accurate where eigenvalues
// repeat), skipped when Gershgorin's bound is below the lower threshold.
// The particles are mapped into the box and
// sorted into buckets of the reach, so each cell gathers from the buckets
// about it in a fixed order; then the smoothing passes, each a gather
// into the other buffer.

#include "gas_surface.h"

#include "gas.h"
#include "gas_api.h"

#include <math.h>
#include <string.h>

// The least particles or rows a task's range takes.
#define PLACE_RANGE 64
#define ROW_RANGE   4

// Cyclic Jacobi's sweeps toward the largest eigenvalue: three leave a
// relative error below 1e-6 at random and repeated eigenvalues alike,
// two leave 1%.
#define JACOBI_SWEEPS 3

// A surface gas's step: the gas and its room, its emitter's places and
// the map from their space into the box, its sizes in cells, and the
// buffers a pass reads and writes.
typedef struct Surface
{
    const mvfxCompiledGas* gas;
    mvfxGasState* state;
    mvfxLiquidGasRoom* room;
    const float* x[3];
    int32_t count;
    mvfxSpaceMap map;
    const int32_t* n;
    // 3, or 2 for a gas in the XY plane.
    int32_t axes;
    float reach;
    float radius;
    const float* from;
    float* to;
} Surface;

// The sums a cell gathers: the kernel's weights, the weighted offsets to
// the particles, the kernel's gradients, and their outer products with
// the offsets (XX, YY, ZZ, XY, XZ, YZ).
typedef struct Sums
{
    float weight;
    float offset[3];
    float gradient[3];
    float outer[6];
} Sums;

// A room array of the particles' places in cells (0 to 2).
static float* Local(const Surface* s, int32_t k)
{
    return s->room->local + (size_t)k * (size_t)s->room->capacity;
}

static void MapRange(int32_t start, int32_t end, void* context)
{
    const Surface* s = context;
    for (int32_t i = start; i < end; ++i)
    {
        const float x[3] = {s->x[0][i], s->x[1][i], s->x[2][i]};
        float local[3];
        mvfxMapPoint(&s->map, x, local);
        for (int32_t k = 0; k < 3; ++k)
        {
            // A 2D gas's particles all at its cells' Z.
            float p = local[k] / s->gas->def.cellSize + 0.5f * (float)s->n[k];
            Local(s, k)[i] = k < s->axes ? p : 0.5f;
        }
    }
}

// Particle i's bucket, -1 beyond the buckets.
static int64_t BucketOf(const Surface* s, int32_t i)
{
    const int32_t* m = s->gas->buckets;
    int64_t bucket = 0;
    for (int32_t k = 2; k >= 0; --k)
    {
        // The first bucket lies before the box's first cell.
        float b = k < s->axes ? floorf(Local(s, k)[i] / (float)s->gas->bucket) + 1.0f : 0.0f;
        if (!(b >= 0.0f && b < (float)m[k]))
        {
            return -1;
        }
        bucket = bucket * m[k] + (int32_t)b;
    }
    return bucket;
}

// The particles by bucket, in their order within a bucket.
static void Sort(const Surface* s)
{
    int64_t buckets = s->gas->bucketCount;
    int32_t* start = s->room->cellStart;
    memset(start, 0, (size_t)(buckets + 1) * sizeof(int32_t));
    for (int32_t i = 0; i < s->count; ++i)
    {
        int64_t b = BucketOf(s, i);
        start[b >= 0 ? b : buckets] += b >= 0 ? 1 : 0;
    }
    int32_t total = 0;
    for (int64_t b = 0; b < buckets; ++b)
    {
        total += start[b];
        start[b] = total;
    }
    start[buckets] = total;
    for (int32_t i = s->count - 1; i >= 0; --i)
    {
        int64_t b = BucketOf(s, i);
        if (b >= 0)
        {
            s->room->order[--start[b]] = i;
        }
    }
}

// Adds the particles of bucket b within the reach of point x (in cells).
static void GatherBucket(const Surface* s, int64_t b, const float x[3], Sums* sums)
{
    const int32_t* start = s->room->cellStart;
    float inverse = 1.0f / (s->reach * s->reach);
    for (int32_t e = start[b]; e < start[b + 1]; ++e)
    {
        int32_t i = s->room->order[e];
        const float d[3] = {Local(s, 0)[i] - x[0], Local(s, 1)[i] - x[1], Local(s, 2)[i] - x[2]};
        float a = 1.0f - (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * inverse;
        if (!(a > 0.0f))
        {
            continue;
        }
        // The kernel (1 - s^2)^3 and its gradient's factor 6 (1 - s^2)^2 /
        // R^2 along the offset.
        float k = a * a * a;
        float c = 6.0f * a * a * inverse;
        sums->weight += k;
        for (int32_t j = 0; j < 3; ++j)
        {
            sums->offset[j] += k * d[j];
            sums->gradient[j] += c * d[j];
        }
        sums->outer[0] += c * d[0] * d[0];
        sums->outer[1] += c * d[1] * d[1];
        sums->outer[2] += c * d[2] * d[2];
        sums->outer[3] += c * d[0] * d[1];
        sums->outer[4] += c * d[0] * d[2];
        sums->outer[5] += c * d[1] * d[2];
    }
}

// Zeroes a's (p, q) entry by a rotation in its plane, a turned on both
// sides (Golub and Van Loan, section 8.5): t the smaller root of
// t^2 + 2 theta t - 1, for accuracy.
static void JacobiRotate(float a[3][3], int32_t p, int32_t q)
{
    if (a[p][q] == 0.0f)
    {
        return;
    }
    float theta = (a[q][q] - a[p][p]) / (2.0f * a[p][q]);
    float t = (theta >= 0.0f ? 1.0f : -1.0f) / (fabsf(theta) + sqrtf(theta * theta + 1.0f));
    float c = 1.0f / sqrtf(t * t + 1.0f);
    float s = t * c;
    for (int32_t k = 0; k < 3; ++k)
    {
        float x = a[k][p];
        float y = a[k][q];
        a[k][p] = c * x - s * y;
        a[k][q] = s * x + c * y;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        float x = a[p][k];
        float y = a[q][k];
        a[p][k] = c * x - s * y;
        a[q][k] = s * x + c * y;
    }
}

float mvfxSurfaceEigenvalue(const float m[6], int32_t axes, float low)
{
    if (axes == 2)
    {
        float half = 0.5f * (m[0] - m[1]);
        return 0.5f * (m[0] + m[1]) + sqrtf(half * half + m[3] * m[3]);
    }
    float bound = fmaxf(fmaxf(m[0] + fabsf(m[3]) + fabsf(m[4]), m[1] + fabsf(m[3]) + fabsf(m[5])),
                        m[2] + fabsf(m[4]) + fabsf(m[5]));
    if (bound < low)
    {
        return bound;
    }
    float a[3][3] = {{m[0], m[3], m[4]}, {m[3], m[1], m[5]}, {m[4], m[5], m[2]}};
    for (int32_t sweep = 0; sweep < JACOBI_SWEEPS; ++sweep)
    {
        for (int32_t r = 0; r < 3; ++r)
        {
            JacobiRotate(a, r == 2 ? 1 : 0, r == 0 ? 1 : 2);
        }
    }
    return fmaxf(fmaxf(a[0][0], a[1][1]), a[2][2]);
}

// The distance at a point from its sums, a particle within its reach, in
// cells.
static float DistanceOf(const Surface* s, const Sums* sums)
{
    float mean[3];
    for (int32_t j = 0; j < 3; ++j)
    {
        mean[j] = sums->offset[j] / sums->weight;
    }
    const mvfxGasSurface* def = &s->gas->def.surface;
    float f = 1.0f;
    if (def->low < def->high)
    {
        // The Jacobian's symmetric part: (outer - (mean G^T + G mean^T) / 2)
        // over the weight.
        const int32_t i[6] = {0, 1, 2, 0, 0, 1};
        const int32_t j[6] = {0, 1, 2, 1, 2, 2};
        float m[6];
        for (int32_t e = 0; e < 6; ++e)
        {
            float cross = mean[i[e]] * sums->gradient[j[e]] + sums->gradient[i[e]] * mean[j[e]];
            m[e] = (sums->outer[e] - 0.5f * cross) / sums->weight;
        }
        float largest = mvfxSurfaceEigenvalue(m, s->axes, def->low);
        float g = (def->high - largest) / (def->high - def->low);
        float rest = 1.0f - g;
        f = largest < def->low ? 1.0f : fminf(fmaxf(1.0f - rest * rest * rest, 0.0f), 1.0f);
    }
    // Within the reach: the mean of particles within it is.
    return sqrtf(mean[0] * mean[0] + mean[1] * mean[1] + mean[2] * mean[2]) - s->radius * f;
}

// The distance at the center of cell (x, y, z), in the box's lengths.
static float FieldAt(const Surface* s, int32_t x, int32_t y, int32_t z)
{
    const float p[3] = {(float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f};
    const int32_t at[3] = {x, y, z};
    const int32_t* m = s->gas->buckets;
    int32_t low[3];
    int32_t high[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        // The cell's bucket and those about it, all within the buckets.
        int32_t b = k < s->axes ? at[k] / s->gas->bucket + 1 : 0;
        low[k] = k < s->axes ? b - 1 : 0;
        high[k] = k < s->axes ? b + 1 : 0;
        high[k] = high[k] < m[k] ? high[k] : m[k] - 1;
    }
    Sums sums = {0};
    for (int32_t bz = low[2]; bz <= high[2]; ++bz)
    {
        for (int32_t by = low[1]; by <= high[1]; ++by)
        {
            for (int32_t bx = low[0]; bx <= high[0]; ++bx)
            {
                GatherBucket(s, ((int64_t)bz * m[1] + by) * m[0] + bx, p, &sums);
            }
        }
    }
    // Without a particle within it, the reach, in lengths (in cells it may
    // round to 0).
    return sums.weight > 0.0f ? DistanceOf(s, &sums) * s->gas->def.cellSize
                              : mvfxSurfaceReach(s->gas);
}

static void FieldRows(int32_t start, int32_t end, void* context)
{
    const Surface* s = context;
    for (int32_t row = start; row < end; ++row)
    {
        int64_t i = (int64_t)row * s->n[0];
        for (int32_t x = 0; x < s->n[0]; ++x, ++i)
        {
            s->to[i] = FieldAt(s, x, row % s->n[1], row / s->n[1]);
        }
    }
}

// The mean of cell i at (x, y, z) and its neighbors along the gas's axes
// in from, a cell at the box's edge standing in for the one beyond.
static float MeanAt(const Surface* s, int64_t i, const int32_t at[3])
{
    const int64_t step[3] = {1, s->n[0], (int64_t)s->n[0] * s->n[1]};
    float sum = s->from[i];
    for (int32_t k = 0; k < s->axes; ++k)
    {
        sum += s->from[at[k] > 0 ? i - step[k] : i];
        sum += s->from[at[k] + 1 < s->n[k] ? i + step[k] : i];
    }
    return sum / (float)(1 + 2 * s->axes);
}

// A smoothing pass's rows: the mean, or with lower only where it lowers
// the cell (Zhu and Bridson's restriction, which keeps thin features).
static void SmoothRows(int32_t start, int32_t end, void* context, bool lower)
{
    const Surface* s = context;
    for (int32_t row = start; row < end; ++row)
    {
        int64_t i = (int64_t)row * s->n[0];
        for (int32_t x = 0; x < s->n[0]; ++x, ++i)
        {
            const int32_t at[3] = {x, row % s->n[1], row / s->n[1]};
            float mean = MeanAt(s, i, at);
            s->to[i] = lower ? fminf(mean, s->from[i]) : mean;
        }
    }
}

static void MeanRows(int32_t start, int32_t end, void* context)
{
    SmoothRows(start, end, context, false);
}

static void LowerRows(int32_t start, int32_t end, void* context)
{
    SmoothRows(start, end, context, true);
}

// The step's view of surface gas g and its emitter's particles.
static Surface SurfaceOf(mvfxInstance* instance, int32_t g)
{
    const mvfxCompiledGas* gas = &instance->effect->gases[g];
    int32_t e = gas->emitter;
    mvfxGasState* state = &instance->gases[g];
    float cell = gas->def.cellSize;
    Surface s = {.gas = gas,
                 .state = state,
                 .room = &state->room,
                 .count = instance->emitters[e].count,
                 .map = mvfxGasParticleMap(instance, g, e),
                 .n = gas->def.size,
                 .axes = gas->def.size[2] > 1 ? 3 : 2,
                 .reach = mvfxSurfaceReach(gas) / cell,
                 .radius = gas->def.surface.radius / cell};
    for (int32_t k = 0; k < 3; ++k)
    {
        s.x[k] = mvfxAttributeArray(instance, e, (mvfxAttribute)(mvfx_attributePositionX + k));
    }
    return s;
}

void mvfxStepSurfaceGas(mvfxInstance* instance, int32_t g, const mvfxTasks* tasks)
{
    Surface s = SurfaceOf(instance, g);
    int32_t rows = s.n[1] * s.n[2];
    mvfxRunTasks(tasks, MapRange, s.count, PLACE_RANGE, &s);
    Sort(&s);
    float* const buffers[2] = {s.state->scalars[0][0], s.state->scalars[0][1]};
    s.to = buffers[0];
    mvfxRunTasks(tasks, FieldRows, rows, ROW_RANGE, &s);
    for (int32_t pass = 0; pass < s.gas->def.surface.smoothing; ++pass)
    {
        s.from = buffers[0];
        s.to = buffers[1];
        mvfxRunTasks(tasks, MeanRows, rows, ROW_RANGE, &s);
        s.from = buffers[1];
        s.to = buffers[0];
        mvfxRunTasks(tasks, LowerRows, rows, ROW_RANGE, &s);
    }
    s.state->current = 0;
    s.state->holds = s.count > 0;
}
