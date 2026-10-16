// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Position-based fluids (Macklin and Muller, "Position Based Fluids",
// SIGGRAPH 2013) in substeps, as units' XPBD is (unit.c): the integrator
// predicts each substep's places and velocities; a grid of the predicted
// places is built; each iteration computes every particle's density and
// multiplier in one gather, then its correction in a second that reads
// only the first's, and follows each correction's way with the
// colliders, sliding along them (Macklin et al. 2014 meet collisions
// inside the solver's loop); the velocity gains the correction over the
// substep; XSPH viscosity and vorticity confinement are gathers over the
// velocities, each into a buffer of its own. The density constraint only
// separates particles. Densities are over the rest
// density, so the kernels' constants cancel in the constraint but not in
// the curl, which keeps them.

#include "liquid.h"

#include "collide.h"
#include "integrate.h"
#include "neighbor.h"

#include <math.h>
#include <string.h>

float mvfxLiquidPoly6(const mvfxLiquidKernel* kernel, float square)
{
    float d = kernel->square - square;
    return d > 0.0f ? kernel->poly6 * d * d * d : 0.0f;
}

void mvfxLiquidSpiky(const mvfxLiquidKernel* kernel, const float offset[3], float length,
                     float out[3])
{
    float d = kernel->radius - length;
    float factor = d > 0.0f && length > 0.0f ? -kernel->spiky * d * d / length : 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = factor * offset[k];
    }
}

// The kernels' factors at a kernel radius: poly6's 315 / (64 pi h^9) and
// the spiky gradient's 45 / (pi h^6) in 3D, 4 / (pi h^8) and 30 / (pi h^5)
// in 2D.
static void Factors(float radius, bool planar, mvfxLiquidKernel* out)
{
    const double pi = 3.14159265358979323846;
    double h = (double)radius;
    double h2 = h * h;
    double h4 = h2 * h2;
    double h8 = h4 * h4;
    out->poly6 = planar ? (float)(4.0 / (pi * h8)) : (float)(315.0 / (64.0 * pi * h8 * h));
    out->spiky = planar ? (float)(30.0 / (pi * h4 * h)) : (float)(45.0 / (pi * h4 * h2));
}

mvfxLiquidKernel mvfxLiquidKernelOf(const mvfxLiquidDef* def, float scale)
{
    float spacing = def->spacing * scale;
    mvfxLiquidKernel kernel = {.radius = spacing * def->kernelScale};
    kernel.square = kernel.radius * kernel.radius;
    Factors(kernel.radius, def->planar, &kernel);
    // The ideal lattice about a particle, in a fixed order.
    int32_t reach = (int32_t)ceilf(def->kernelScale);
    int32_t layers = def->planar ? 0 : reach;
    float rest = 0.0f;
    float gradients = 0.0f;
    for (int32_t c = -layers; c <= layers; ++c)
    {
        for (int32_t b = -reach; b <= reach; ++b)
        {
            for (int32_t a = -reach; a <= reach; ++a)
            {
                const float offset[3] = {(float)a * spacing, (float)b * spacing,
                                         (float)c * spacing};
                float square =
                    offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2];
                float g[3];
                mvfxLiquidSpiky(&kernel, offset, sqrtf(square), g);
                rest += mvfxLiquidPoly6(&kernel, square);
                gradients += g[0] * g[0] + g[1] * g[1] + g[2] * g[2];
            }
        }
    }
    kernel.restInverse = 1.0f / rest;
    float full = gradients * kernel.restInverse * kernel.restInverse;
    kernel.softening = def->relaxation * full;
    kernel.tensileScale = def->tensile / full;
    float reference = def->tensileDistance * kernel.radius;
    kernel.tensileInverse = 1.0f / mvfxLiquidPoly6(&kernel, reference * reference);
    return kernel;
}

// A substep's view of a liquid: the grid and the places it was built from
// (each particle's cells), the places now and the velocities, the kernels
// and the def.
typedef struct Liquid
{
    const mvfxNeighborGrid* grid;
    float inverseCell;
    const float* built[3];
    float* x[3];
    float* v[3];
    mvfxLiquidKernel kernel;
    const mvfxLiquidDef* def;
    int32_t count;
} Liquid;

// The neighbors of particle i within the kernel radius at their places
// now, at most the def's most, in the grid's order, into out; capped when
// it has more.
static int32_t Neighbors(const Liquid* l, int32_t i, int32_t out[MVFX_MAX_NEIGHBORS], bool* capped)
{
    const mvfxNeighborGrid* grid = l->grid;
    int32_t cell[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        cell[k] = mvfxGridCell(l->built[k][i], l->inverseCell);
    }
    uint32_t visited[27];
    int32_t visits = 0;
    int32_t count = 0;
    for (int32_t n = 0; n < 27; ++n)
    {
        int32_t around[3] = {cell[0] + n % 3 - 1, cell[1] + n / 3 % 3 - 1, cell[2] + n / 9 - 1};
        uint32_t key = mvfxCellKey(around, grid->mask);
        bool seen = false;
        for (int32_t v = 0; v < visits; ++v)
        {
            seen = seen || visited[v] == key;
        }
        if (seen)
        {
            continue;
        }
        visited[visits++] = key;
        for (int32_t e = grid->table[key]; e < grid->table[key + 1u]; ++e)
        {
            int32_t j = grid->index[e];
            float square = 0.0f;
            for (int32_t k = 0; k < 3; ++k)
            {
                float d = l->x[k][i] - l->x[k][j];
                square += d * d;
            }
            if (j == i || !(square < l->kernel.square))
            {
                continue;
            }
            if (count == l->def->maxNeighbors)
            {
                *capped = true;
                return count;
            }
            out[count++] = j;
        }
    }
    return count;
}

// The offset from neighbor j to particle i at their places now, and its
// length.
static float OffsetOf(const Liquid* l, int32_t i, int32_t j, float d[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = l->x[k][i] - l->x[k][j];
    }
    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

// The gradient of particle i's density with respect to neighbor j's
// place, negated: the spiky gradient over the rest density.
static void GradientOf(const Liquid* l, int32_t i, int32_t j, float g[3], float* square)
{
    float d[3];
    float length = OffsetOf(l, i, j, d);
    *square = length * length;
    mvfxLiquidSpiky(&l->kernel, d, length, g);
    for (int32_t k = 0; k < 3; ++k)
    {
        g[k] *= l->kernel.restInverse;
    }
}

// Each particle's multiplier into multipliers: minus its density's excess
// over the gradients' squared sizes and the softening; the particles
// capped counted into capped. The constraint is one-sided, as Macklin et
// al. 2014's fluids are: below its rest density a particle is not pulled
// in, which at a free surface would clump it (the paper's tensile
// instability), with nothing to hold the surface but that.
static void Multipliers(const Liquid* l, float* multipliers, uint64_t* capped)
{
    int32_t near[MVFX_MAX_NEIGHBORS];
    for (int32_t i = 0; i < l->count; ++i)
    {
        bool over = false;
        int32_t n = Neighbors(l, i, near, &over);
        *capped += over ? 1u : 0u;
        float density = mvfxLiquidPoly6(&l->kernel, 0.0f);
        float own[3] = {0.0f, 0.0f, 0.0f};
        float sizes = 0.0f;
        for (int32_t m = 0; m < n; ++m)
        {
            float g[3];
            float square = 0.0f;
            GradientOf(l, i, near[m], g, &square);
            density += mvfxLiquidPoly6(&l->kernel, square);
            for (int32_t k = 0; k < 3; ++k)
            {
                own[k] += g[k];
            }
            sizes += g[0] * g[0] + g[1] * g[1] + g[2] * g[2];
        }
        float error = fmaxf(density * l->kernel.restInverse - 1.0f, 0.0f);
        sizes += own[0] * own[0] + own[1] * own[1] + own[2] * own[2];
        multipliers[i] = -error / (sizes + l->kernel.softening);
    }
}

// The tensile correction between two particles a squared distance apart:
// -k (W(r) / W(dq))^n, over the lattice's denominator.
static float Tensile(const Liquid* l, float square)
{
    float ratio = mvfxLiquidPoly6(&l->kernel, square) * l->kernel.tensileInverse;
    float power = 1.0f;
    for (int32_t p = 0; p < l->def->tensilePower; ++p)
    {
        power *= ratio;
    }
    return -l->kernel.tensileScale * power;
}

// Each particle's corrected place into moved: its place plus the
// neighbors' gradients weighted by both multipliers and the tensile
// correction (Z held for a planar liquid).
static void Corrections(const Liquid* l, const float* multipliers, float* const moved[3])
{
    int32_t near[MVFX_MAX_NEIGHBORS];
    for (int32_t i = 0; i < l->count; ++i)
    {
        bool over = false;
        int32_t n = Neighbors(l, i, near, &over);
        float delta[3] = {0.0f, 0.0f, 0.0f};
        for (int32_t m = 0; m < n; ++m)
        {
            int32_t j = near[m];
            float g[3];
            float square = 0.0f;
            GradientOf(l, i, j, g, &square);
            float weight = multipliers[i] + multipliers[j] + Tensile(l, square);
            for (int32_t k = 0; k < 3; ++k)
            {
                delta[k] += weight * g[k];
            }
        }
        delta[2] = l->def->planar ? 0.0f : delta[2];
        for (int32_t k = 0; k < 3; ++k)
        {
            moved[k][i] = l->x[k][i] + delta[k];
        }
    }
}

// Each particle's way from its place to its corrected one followed with
// the colliders, sliding along them, into its place.
static void Slide(mvfxInstance* instance, int32_t emitter, const Liquid* l, float sigma,
                  float* const moved[3])
{
    const mvfxEmitterState* state = &instance->emitters[emitter];
    bool collides = instance->effect->emitters[emitter].collision.enabled;
    const mvfxCollision slide = {.enabled = true};
    for (int32_t i = 0; i < l->count; ++i)
    {
        const float start[3] = {l->x[0][i], l->x[1][i], l->x[2][i]};
        float place[3] = {moved[0][i], moved[1][i], moved[2][i]};
        if (collides)
        {
            mvfxResolvedCollider plane;
            mvfxOwnColliders own = mvfxOwnCollidersOf(instance, emitter, i, sigma, &plane);
            float way[3] = {place[0] - start[0], place[1] - start[1], place[2] - start[2]};
            mvfxContact first;
            (void)mvfxCollide(state->colliders, state->colliderCount, &own, &slide, 0.0f, start,
                              place, way, &first);
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            l->x[k][i] = place[k];
        }
    }
}

// XSPH viscosity: each velocity blended toward its neighbors' by the
// viscosity, weighted by poly6 over the rest density, into out.
static void Viscosity(const Liquid* l, float* const out[3])
{
    int32_t near[MVFX_MAX_NEIGHBORS];
    for (int32_t i = 0; i < l->count; ++i)
    {
        bool over = false;
        int32_t n = Neighbors(l, i, near, &over);
        float blend[3] = {0.0f, 0.0f, 0.0f};
        for (int32_t m = 0; m < n; ++m)
        {
            int32_t j = near[m];
            float d[3];
            float length = OffsetOf(l, i, j, d);
            float w = mvfxLiquidPoly6(&l->kernel, length * length) * l->kernel.restInverse;
            for (int32_t k = 0; k < 3; ++k)
            {
                blend[k] += (l->v[k][j] - l->v[k][i]) * w;
            }
        }
        blend[2] = l->def->planar ? 0.0f : blend[2];
        for (int32_t k = 0; k < 3; ++k)
        {
            out[k][i] = l->v[k][i] + l->def->viscosity * blend[k];
        }
    }
}

// Each particle's curl, the sum over its neighbors of the velocity's
// difference across the gradient of the kernel at the neighbor, into
// swirls.
static void Curls(const Liquid* l, float* const swirls[3])
{
    int32_t near[MVFX_MAX_NEIGHBORS];
    for (int32_t i = 0; i < l->count; ++i)
    {
        bool over = false;
        int32_t n = Neighbors(l, i, near, &over);
        float w[3] = {0.0f, 0.0f, 0.0f};
        for (int32_t m = 0; m < n; ++m)
        {
            int32_t j = near[m];
            float g[3];
            float square = 0.0f;
            GradientOf(l, i, j, g, &square);
            const float dv[3] = {l->v[0][j] - l->v[0][i], l->v[1][j] - l->v[1][i],
                                 l->v[2][j] - l->v[2][i]};
            // dv x (-g): the gradient at the neighbor is the negated one.
            w[0] += dv[2] * g[1] - dv[1] * g[2];
            w[1] += dv[0] * g[2] - dv[2] * g[0];
            w[2] += dv[1] * g[0] - dv[0] * g[1];
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            swirls[k][i] = w[k];
        }
    }
}

// A swirl's size.
static float SizeOf(const float* const swirls[3], int32_t i)
{
    return sqrtf(swirls[0][i] * swirls[0][i] + swirls[1][i] * swirls[1][i] +
                 swirls[2][i] * swirls[2][i]);
}

// Vorticity confinement over a substep of h: each velocity plus h times
// epsilon spacing (N x w), N the gradient of the curl's size over the
// larger of its own size and 1e-4 of the curl's over the kernel radius
// (a unit vector where the size changes, tapering to nothing where it is
// even, as a gas's), into out.
static void Confine(const Liquid* l, const float* const swirls[3], float spacing, float h,
                    float* const out[3])
{
    int32_t near[MVFX_MAX_NEIGHBORS];
    float strength = h * l->def->vorticity * spacing;
    for (int32_t i = 0; i < l->count; ++i)
    {
        bool over = false;
        int32_t n = Neighbors(l, i, near, &over);
        float size = SizeOf(swirls, i);
        float eta[3] = {0.0f, 0.0f, 0.0f};
        for (int32_t m = 0; m < n; ++m)
        {
            int32_t j = near[m];
            float g[3];
            float square = 0.0f;
            GradientOf(l, i, j, g, &square);
            float change = SizeOf(swirls, j) - size;
            for (int32_t k = 0; k < 3; ++k)
            {
                eta[k] += change * g[k];
            }
        }
        float length = sqrtf(eta[0] * eta[0] + eta[1] * eta[1] + eta[2] * eta[2]);
        float floor = 1e-4f * size / l->kernel.radius;
        float bound = length > floor ? length : floor;
        float scale = bound > 0.0f ? strength / bound : 0.0f;
        const float w[3] = {swirls[0][i], swirls[1][i], swirls[2][i]};
        float f[3] = {eta[1] * w[2] - eta[2] * w[1], eta[2] * w[0] - eta[0] * w[2],
                      eta[0] * w[1] - eta[1] * w[0]};
        f[2] = l->def->planar ? 0.0f : f[2];
        for (int32_t k = 0; k < 3; ++k)
        {
            out[k][i] = l->v[k][i] + scale * f[k];
        }
    }
}

// Copies three arrays of count values from from into to.
static void CopyThree(float* const to[3], const float* const from[3], int32_t count)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        memcpy(to[k], from[k], (size_t)count * sizeof(float));
    }
}

// Three arrays of a room's stride from base.
static void ThreeOf(float* base, size_t stride, float* out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = base + (size_t)k * stride;
    }
}

// The density iterations on the places, the first's capped particles
// counted into capped.
static void Iterate(mvfxInstance* instance, int32_t emitter, Liquid* l, float sigma,
                    uint64_t* capped)
{
    mvfxLiquidRoom* room = &instance->emitters[emitter].liquid;
    float* moved[3];
    ThreeOf(room->moved, (size_t)room->grid.capacity, moved);
    for (int32_t n = 0; n < l->def->iterations; ++n)
    {
        uint64_t over = 0;
        Multipliers(l, room->multipliers, &over);
        *capped += n == 0 ? over : 0u;
        Corrections(l, room->multipliers, moved);
        Slide(instance, emitter, l, sigma, moved);
    }
}

// A substep of h after the integrator: the iterations, the velocities'
// gain, then viscosity and confinement.
static void Solve(mvfxInstance* instance, int32_t emitter, Liquid* l, float sigma, float spacing,
                  float h, uint64_t* capped)
{
    mvfxLiquidRoom* room = &instance->emitters[emitter].liquid;
    size_t stride = (size_t)room->grid.capacity;
    float* predicted[3];
    float* velocities[3];
    float* swirls[3];
    ThreeOf(room->predicted, stride, predicted);
    ThreeOf(room->velocities, stride, velocities);
    ThreeOf(room->swirls, stride, swirls);
    Iterate(instance, emitter, l, sigma, capped);
    for (int32_t k = 0; k < 3; ++k)
    {
        for (int32_t i = 0; i < l->count; ++i)
        {
            l->v[k][i] += (l->x[k][i] - predicted[k][i]) / h;
        }
    }
    if (l->def->viscosity > 0.0f)
    {
        Viscosity(l, velocities);
        CopyThree(l->v, (const float* const*)velocities, l->count);
    }
    if (l->def->vorticity > 0.0f)
    {
        Curls(l, swirls);
        Confine(l, (const float* const*)swirls, spacing, h, velocities);
        CopyThree(l->v, (const float* const*)velocities, l->count);
    }
}

void mvfxStepLiquid(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    mvfxLiquidRoom* room = &state->liquid;
    // Lengths in the particles' space: a world-space emitter's scale with
    // its instance.
    float sigma = compiled->space == mvfx_spaceWorld ? instance->to.scale : 1.0f;
    Liquid l = {.grid = &room->grid,
                .kernel = mvfxLiquidKernelOf(&compiled->liquid, sigma),
                .def = &compiled->liquid,
                .count = state->count};
    l.inverseCell = 1.0f / l.kernel.radius;
    float* predicted[3];
    ThreeOf(room->predicted, (size_t)room->grid.capacity, predicted);
    for (int32_t k = 0; k < 3; ++k)
    {
        l.x[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        l.v[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
        l.built[k] = predicted[k];
    }
    int64_t ticks = instance->clock - instance->stepStart;
    int32_t substeps = compiled->liquid.substeps;
    uint64_t capped = 0;
    for (int32_t s = 0; s < substeps; ++s)
    {
        int64_t from = instance->stepStart + ticks * s / substeps;
        int64_t to = instance->stepStart + ticks * (s + 1) / substeps;
        if (to == from)
        {
            continue;
        }
        double seconds = (double)(to - from) / (double)MVFX_TICKS_PER_SECOND;
        mvfxIntegrateRange(instance, emitter, 0, l.count, from, seconds);
        CopyThree(predicted, (const float* const*)l.x, l.count);
        mvfxBuildNeighborGrid(&room->grid, (const float* const*)predicted, nullptr, l.count,
                              l.kernel.radius);
        uint64_t over = 0;
        Solve(instance, emitter, &l, sigma, compiled->liquid.spacing * sigma, (float)seconds,
              &over);
        // Counted once a step, in its first substep.
        capped += s == 0 ? over : 0u;
    }
    state->neighborsCapped += capped;
}

void mvfxMoveGridLiquid(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    mvfxLiquidRoom* room = &state->liquid;
    size_t stride = (size_t)room->grid.capacity;
    float sigma = compiled->space == mvfx_spaceWorld ? instance->to.scale : 1.0f;
    Liquid l = {.def = &compiled->liquid, .count = state->count};
    float* start[3];
    float* carried[3];
    float* moved[3];
    ThreeOf(room->predicted, stride, start);
    ThreeOf(room->velocities, stride, carried);
    ThreeOf(room->moved, stride, moved);
    for (int32_t k = 0; k < 3; ++k)
    {
        l.x[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        l.v[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    CopyThree(start, (const float* const*)l.x, l.count);
    CopyThree(carried, (const float* const*)l.v, l.count);
    // The integrator gives the velocity its forces, drag and bounces, and
    // ages, kills and events, as for any particle.
    mvfxIntegrateEmitter(instance, emitter);
    // The places move by the velocity the gas gave, followed along the
    // colliders.
    float h =
        (float)((double)(instance->clock - instance->stepStart) / (double)MVFX_TICKS_PER_SECOND);
    for (int32_t k = 0; k < 3; ++k)
    {
        for (int32_t i = 0; i < l.count; ++i)
        {
            moved[k][i] = start[k][i] + carried[k][i] * h;
        }
    }
    CopyThree(l.x, (const float* const*)start, l.count);
    Slide(instance, emitter, &l, sigma, moved);
}
