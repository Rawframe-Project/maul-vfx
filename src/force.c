// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "force.h"

#include "direction.h"
#include "elementary.h"
#include "field_path.h"
#include "grid.h"
#include "noise.h"
#include "random.h"
#include "refusal.h"

#include <math.h>

// The salt of a force's draw.
#define FORCE_SALT 0x46524345u

// An axis's status: within MVFX_MAX_EXTENT, and of a length to normalize;
// naming the field of a tag below at.
static mvfxResult CheckAxis(const mvfxFieldPath* at, uint32_t tag, const float v[3])
{
    float copy[3] = {v[0], v[1], v[2]};
    mvfxResult within = mvfxRefuseVector(at, tag, v, MVFX_MAX_EXTENT);
    return within != mvfx_success ? within
                                  : mvfxRefuseField(at, tag, mvfxRequire(mvfxNormalize(copy)));
}

static mvfxResult CheckFalloff(const mvfxFieldPath* at, const mvfxFalloff* falloff)
{
    if (falloff->kind == mvfx_falloffNone)
    {
        return mvfx_success;
    }
    if (falloff->kind > mvfx_falloffCone)
    {
        return mvfxRefuseField(at, MVFX_TAG_FALLOFF_KIND, mvfx_errorInvalid);
    }
    mvfxResult checked =
        mvfxRefuseVector(at, MVFX_TAG_FALLOFF_CENTER, falloff->center, MVFX_MAX_EXTENT);
    checked = checked != mvfx_success
                  ? checked
                  : mvfxRefuseField(at, MVFX_TAG_FALLOFF_RADIUS,
                                    mvfxPositiveUpTo(falloff->radius, MVFX_MAX_EXTENT));
    checked =
        checked != mvfx_success
            ? checked
            : mvfxRefuseField(at, MVFX_TAG_FALLOFF_EXPONENT,
                              mvfxUpToBound(falloff->exponent, 0.0f, MVFX_MAX_FALLOFF_EXPONENT));
    checked = checked != mvfx_success || falloff->kind == mvfx_falloffSphere
                  ? checked
                  : CheckAxis(at, MVFX_TAG_FALLOFF_AXIS, falloff->axis);
    return checked != mvfx_success || falloff->kind != mvfx_falloffCone
               ? checked
               : mvfxRefuseField(at, MVFX_TAG_FALLOFF_ANGLE,
                                 mvfxPositiveUpTo(falloff->angle, MVFX_MAX_CONE_ANGLE));
}

// Whether a unit quaternion within 0.001, finite.
static bool IsRotation(const float q[4])
{
    double square = 0.0;
    for (int32_t k = 0; k < 4; ++k)
    {
        if (!(fabsf(q[k]) <= 1.001f))
        {
            return false;
        }
        square += (double)q[k] * (double)q[k];
    }
    return fabs(sqrt(square) - 1.0) <= 0.001;
}

static mvfxResult CheckBox(const mvfxFieldPath* at, const mvfxForce* force)
{
    if (!IsRotation(force->boxRotation))
    {
        return mvfxRefuseField(at, MVFX_TAG_FORCE_BOX_ROTATION, mvfx_errorInvalid);
    }
    if (force->border > mvfx_borderRepeat)
    {
        return mvfxRefuseField(at, MVFX_TAG_FORCE_BORDER, mvfx_errorInvalid);
    }
    return mvfxRefuseSizes(at, MVFX_TAG_FORCE_BOX_SIZE, force->boxSize, MVFX_MAX_EXTENT);
}

// The field of a force whose kind, space, mode or gas is out of range,
// 0 for none.
static uint32_t RefusedChoice(const mvfxForce* force)
{
    // A gas past MVFX_MAX_GASES is an index naming no gas, not a count.
    if (force->kind > mvfx_forceGas)
    {
        return MVFX_TAG_FORCE_KIND;
    }
    if (force->space > mvfx_spaceLocal)
    {
        return MVFX_TAG_FORCE_SPACE;
    }
    if (force->fieldMode > mvfx_fieldVelocity)
    {
        return MVFX_TAG_FORCE_FIELD_MODE;
    }
    return force->kind == mvfx_forceGas && (force->gas < 0 || force->gas >= MVFX_MAX_GASES)
               ? MVFX_TAG_FORCE_GAS
               : 0;
}

// A force's strength: a source within its cap, and wind's, a rate, never
// below 0.
static mvfxResult CheckStrength(const mvfxFieldPath* at, const mvfxForce* force,
                                int32_t parameterCount, bool hasDuration)
{
    bool wind = force->kind == mvfx_forceWind;
    mvfxFieldPath strength = mvfxFieldIn(at, MVFX_TAG_FORCE_STRENGTH, -1);
    if (wind && force->strength.kind != mvfx_sourceNone && !(force->strength.range.min >= 0.0f))
    {
        mvfxFieldPath range = mvfxFieldIn(&strength, MVFX_TAG_SCALAR_SOURCE_RANGE, -1);
        return mvfxRefuseField(&range, MVFX_TAG_RANGE_MIN, mvfx_errorInvalid);
    }
    return mvfxCheckScalarSource(&strength, &force->strength, true, hasDuration, parameterCount,
                                 wind ? MVFX_MAX_DRAG : MVFX_MAX_EXTENT);
}

mvfxResult mvfxCheckForce(const mvfxFieldPath* at, const mvfxForce* force, int32_t parameterCount,
                          bool hasDuration)
{
    uint32_t choice = RefusedChoice(force);
    if (choice != 0)
    {
        return mvfxRefuseField(at, choice, mvfx_errorInvalid);
    }
    mvfxResult checked = mvfxRefuseField(at, MVFX_TAG_FORCE_TIGHTNESS,
                                         mvfxUpToBound(force->tightness, 0.0f, MVFX_MAX_DRAG));
    checked =
        checked != mvfx_success ? checked : CheckStrength(at, force, parameterCount, hasDuration);
    checked = checked != mvfx_success
                  ? checked
                  : mvfxRefuseVector(at, MVFX_TAG_FORCE_VECTOR, force->vector, MVFX_MAX_EXTENT);
    checked = checked != mvfx_success
                  ? checked
                  : mvfxRefuseVector(at, MVFX_TAG_FORCE_POINT, force->point, MVFX_MAX_EXTENT);
    if (checked != mvfx_success)
    {
        return checked;
    }
    mvfxFieldPath falloffAt = mvfxFieldIn(at, MVFX_TAG_FORCE_FALLOFF, -1);
    mvfxResult falloff = CheckFalloff(&falloffAt, &force->falloff);
    if (falloff != mvfx_success)
    {
        return falloff;
    }
    switch (force->kind)
    {
    case mvfx_forceAttractor:
        checked =
            mvfxRefuseField(at, MVFX_TAG_FORCE_SOFTENING,
                            mvfxBounded(force->softening, MVFX_MIN_SOFTENING, MVFX_MAX_EXTENT));
        return checked != mvfx_success
                   ? checked
                   : mvfxRefuseField(at, MVFX_TAG_FORCE_KILL_RADIUS,
                                     mvfxUpToBound(force->killRadius, 0.0f, MVFX_MAX_EXTENT));
    case mvfx_forceVortex:
        checked = CheckAxis(at, MVFX_TAG_FORCE_VECTOR, force->vector);
        return checked != mvfx_success
                   ? checked
                   : mvfxRefuseField(at, MVFX_TAG_FORCE_PULL,
                                     mvfxBounded(force->pull, -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT));
    case mvfx_forceTurbulence:
    {
        mvfxFieldPath noise = mvfxFieldIn(at, MVFX_TAG_FORCE_NOISE, -1);
        return mvfxCheckNoise(&noise, &force->noise);
    }
    case mvfx_forceVectorField:
        return CheckBox(at, force);
    default:
        return mvfx_success;
    }
}

mvfxForce mvfxCompileForce(const mvfxForce* force)
{
    mvfxForce out = *force;
    if (out.kind == mvfx_forceVortex)
    {
        (void)mvfxNormalize(out.vector);
    }
    if (out.falloff.kind == mvfx_falloffTube || out.falloff.kind == mvfx_falloffCone)
    {
        (void)mvfxNormalize(out.falloff.axis);
    }
    if (out.kind == mvfx_forceVectorField)
    {
        double square = 0.0;
        for (int32_t k = 0; k < 4; ++k)
        {
            square += (double)out.boxRotation[k] * (double)out.boxRotation[k];
        }
        double inverse = 1.0 / sqrt(square);
        for (int32_t k = 0; k < 4; ++k)
        {
            out.boxRotation[k] = (float)((double)out.boxRotation[k] * inverse);
        }
    }
    return out;
}

void mvfxMapPoint(const mvfxSpaceMap* map, const float p[3], float out[3])
{
    float local[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        local[k] = (float)((double)p[k] - map->before[k]) * map->sigma;
    }
    float turned[3];
    mvfxRotate(map->rotation, local, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (float)((double)turned[k] + map->after[k]);
    }
}

void mvfxMapVector(const mvfxSpaceMap* map, const float v[3], float sigma, float out[3])
{
    float scaled[3] = {v[0] * sigma, v[1] * sigma, v[2] * sigma};
    mvfxRotate(map->rotation, scaled, out);
}

// x held to [-bound, bound], or to [0, bound] when low is 0.
static float Hold(float x, float low, float bound)
{
    return x < low ? low : (x > bound ? bound : x);
}

mvfxWords4 mvfxForceDraw(uint32_t instanceSeed, int32_t owner, bool gas, int32_t index)
{
    return mvfxPcg4d((mvfxWords4){
        instanceSeed,
        ((uint32_t)owner * 0x9E3779B9u) ^ FORCE_SALT,
        (uint32_t)index,
        gas ? 1u : 0u,
    });
}

float mvfxForceStrength(const mvfxForce* force, const mvfxCompiledScalar* strength, float age,
                        const float (*parameters)[4], mvfxWords4 draw)
{
    bool wind = force->kind == mvfx_forceWind;
    return Hold(mvfxDrawScalar(strength, age, parameters, draw), wind ? 0.0f : -MVFX_MAX_EXTENT,
                wind ? MVFX_MAX_DRAG : MVFX_MAX_EXTENT);
}

mvfxResolvedForce mvfxResolveForce(const mvfxForce* force, float strength, const mvfxSpaceMap* map,
                                   uint32_t instanceSeed, const struct mvfxGrid* grid)
{
    float sigma = map->sigma;
    mvfxResolvedForce out = {
        .kind = force->kind,
        .falloff = force->falloff.kind,
        .strength = strength,
        .softening = force->softening * sigma,
        .killRadius = force->killRadius * sigma,
        .pull = force->pull * sigma,
        .radius = force->falloff.radius * sigma,
        .exponent = force->falloff.exponent,
        .fieldMode = force->fieldMode,
        .tightness = force->tightness,
        .map = *map,
        .source = force,
        .grid = grid,
        .unturned = force->boxRotation[0] == 0.0f && force->boxRotation[1] == 0.0f &&
                    force->boxRotation[2] == 0.0f,
        .mapUnturned =
            map->rotation[0] == 0.0f && map->rotation[1] == 0.0f && map->rotation[2] == 0.0f,
        .seed = force->noise.seedPerInstance ? force->noise.seed + instanceSeed * 0x85EBCA6Bu
                                             : force->noise.seed,
    };
    switch (force->kind)
    {
    case mvfx_forceAttractor:
        out.strength *= sigma;
        break;
    case mvfx_forceVortex:
        // The axis turns; strength and pull are accelerations.
        out.strength *= sigma;
        break;
    default:
        break;
    }
    mvfxMapVector(map, force->vector, force->kind == mvfx_forceVortex ? 1.0f : sigma, out.vector);
    mvfxMapPoint(map, force->point, out.point);
    mvfxMapPoint(map, force->falloff.center, out.center);
    mvfxMapVector(map, force->falloff.axis, 1.0f, out.axis);
    if (force->falloff.kind == mvfx_falloffCone)
    {
        float sine = 0.0f;
        mvfxSinCos(force->falloff.angle, &sine, &out.cosine);
    }
    return out;
}

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// The weight (1 - share)^exponent, 0 at or past the edge.
static float Weight(float share, float exponent)
{
    if (!(share < 1.0f))
    {
        return 0.0f;
    }
    return exponent == 0.0f ? 1.0f : mvfxPow(1.0f - share, exponent);
}

// The falloff's weight at x.
static float FalloffAt(const mvfxResolvedForce* force, const float x[3])
{
    if (force->falloff == mvfx_falloffNone)
    {
        return 1.0f;
    }
    float d[3] = {x[0] - force->center[0], x[1] - force->center[1], x[2] - force->center[2]};
    float distance = sqrtf(Dot(d, d));
    float along = Dot(d, force->axis);
    if (force->falloff == mvfx_falloffTube)
    {
        float across[3] = {d[0] - along * force->axis[0], d[1] - along * force->axis[1],
                           d[2] - along * force->axis[2]};
        return Weight(sqrtf(Dot(across, across)) / force->radius, force->exponent);
    }
    float weight = Weight(distance / force->radius, force->exponent);
    if (force->falloff == mvfx_falloffCone && distance > 0.0f)
    {
        // The angle's share by its cosine: 0 on the axis, 1 at the edge.
        float cosine = along / distance;
        float share = (1.0f - cosine) / (1.0f - force->cosine);
        weight *= Weight(share, force->exponent);
    }
    return weight;
}

// The attractor's acceleration at x.
static void Attract(const mvfxResolvedForce* force, const float x[3], float weight, float a[3])
{
    float d[3] = {force->point[0] - x[0], force->point[1] - x[1], force->point[2] - x[2]};
    float distance = sqrtf(Dot(d, d));
    if (!(distance > 0.0f))
    {
        return;
    }
    float ratio = force->softening / distance;
    float magnitude = distance < force->softening ? force->strength / force->softening * distance
                                                  : force->strength * ratio * ratio;
    float scale = weight * magnitude / distance;
    for (int32_t k = 0; k < 3; ++k)
    {
        a[k] += d[k] * scale;
    }
}

// The vortex's acceleration at x.
static void Swirl(const mvfxResolvedForce* force, const float x[3], float weight, float a[3])
{
    const float* u = force->vector;
    float r[3] = {x[0] - force->point[0], x[1] - force->point[1], x[2] - force->point[2]};
    float along = Dot(r, u);
    float across[3] = {r[0] - along * u[0], r[1] - along * u[1], r[2] - along * u[2]};
    float distance = sqrtf(Dot(across, across));
    if (!(distance > 0.0f))
    {
        return;
    }
    float inverse = 1.0f / distance;
    float turn[3] = {u[1] * across[2] - u[2] * across[1], u[2] * across[0] - u[0] * across[2],
                     u[0] * across[1] - u[1] * across[0]};
    for (int32_t k = 0; k < 3; ++k)
    {
        a[k] += weight * inverse * (force->strength * turn[k] - force->pull * across[k]);
    }
}

float mvfxForceDrag(const mvfxResolvedForce* force)
{
    if (force->kind == mvfx_forceWind)
    {
        return force->strength;
    }
    bool field = force->kind == mvfx_forceTurbulence || force->kind == mvfx_forceVectorField ||
                 force->kind == mvfx_forceGas;
    return field && force->fieldMode == mvfx_fieldVelocity ? force->tightness : 0.0f;
}

// An unturned box's vector: the same bits as the turns would give. They
// leave a finite offset as it is but a zero's sign, which u's + 0.5 drops;
// an offset not finite on one axis makes every axis NaN, as the turn's
// products do; and they leave the sample as it is, never -0, its sums
// starting from +0.
static bool SampleUnturned(const mvfxResolvedForce* force, const float d[3], float out[3])
{
    const mvfxForce* source = force->source;
    bool finite = isfinite(d[0]) && isfinite(d[1]) && isfinite(d[2]);
    float u[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        u[k] = finite ? d[k] / source->boxSize[k] + 0.5f : NAN;
    }
    float sample[4];
    if (!mvfxSampleGrid(force->grid, u, source->border, sample))
    {
        return false;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = sample[k];
    }
    return true;
}

// A vector field's vector at a place in its force's space: the grid
// sampled in the box's own space, turned out with the box; false outside
// it under the none border.
static bool SampleBox(const mvfxResolvedForce* force, const double place[3], float out[3])
{
    const mvfxForce* source = force->source;
    const float* q = source->boxRotation;
    float d[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = (float)(place[k] - (double)source->point[k]);
    }
    if (force->unturned)
    {
        return SampleUnturned(force, d, out);
    }
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    float local[3];
    mvfxRotate(inverse, d, local);
    float u[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        u[k] = local[k] / source->boxSize[k] + 0.5f;
    }
    float sample[4];
    if (!mvfxSampleGrid(force->grid, u, source->border, sample))
    {
        return false;
    }
    mvfxRotate(q, sample, out);
    return true;
}

// A gas's velocity at a place in its box's frame, false outside the box
// (a 2D gas's reaches along its Z without end).
static bool SampleGas(const mvfxResolvedForce* force, const double place[3], float out[3])
{
    const mvfxGasLattice* l = &force->gasLattice;
    float p[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = (float)(place[k] / (double)l->cell) + 0.5f * (float)l->n[k];
        if (l->n[k] > 1 && !(p[k] >= 0.0f && p[k] <= (float)l->n[k]))
        {
            return false;
        }
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = l->n[k] > 1 ? mvfxGasSample(force->gasVelocity[k], l, k, p) : 0.0f;
    }
    return true;
}

// mvfxUnmapPoint for an unturned map, without the turn: the same place
// but a zero's sign, and but an axis not finite left alone where the turn
// makes every axis NaN; an unturned box's sampling, its one reader, sees
// neither (SampleUnturned).
static void UnmapUnturned(const mvfxSpaceMap* map, const float x[3], double out[3])
{
    bool unscaled = map->sigma == 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        double d = (double)x[k] - map->after[k];
        out[k] = (unscaled ? d : d / (double)map->sigma) + map->before[k];
    }
}

// A field's vector at x: its acceleration added to a, or, as a velocity,
// its pull and its drag.
static void Field(const mvfxResolvedForce* force, const float x[3], double time, float weight,
                  float a[3], float* drag)
{
    double place[3];
    if (force->kind == mvfx_forceVectorField && force->unturned && force->mapUnturned)
    {
        UnmapUnturned(&force->map, x, place);
    }
    else
    {
        mvfxUnmapPoint(&force->map, x, place);
    }
    float value[3];
    if (force->kind == mvfx_forceTurbulence)
    {
        mvfxCurlNoise(&force->source->noise, force->seed, place, time, value);
    }
    else if (force->kind == mvfx_forceGas ? !SampleGas(force, place, value)
                                          : !SampleBox(force, place, value))
    {
        return;
    }
    float field[3];
    mvfxMapVector(&force->map, value, force->map.sigma * force->strength, field);
    float scale = weight;
    if (force->fieldMode == mvfx_fieldVelocity)
    {
        *drag += weight * force->tightness;
        scale *= force->tightness;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        a[k] += scale * field[k];
    }
}

void mvfxForcesAt(const mvfxResolvedForce* forces, int32_t count, const float x[3], double time,
                  float a[3], float* drag)
{
    for (int32_t f = 0; f < count; ++f)
    {
        const mvfxResolvedForce* force = &forces[f];
        float weight = FalloffAt(force, x);
        if (!(weight > 0.0f))
        {
            continue;
        }
        switch (force->kind)
        {
        case mvfx_forceConstant:
            for (int32_t k = 0; k < 3; ++k)
            {
                a[k] += weight * force->strength * force->vector[k];
            }
            break;
        case mvfx_forceWind:
            *drag += weight * force->strength;
            for (int32_t k = 0; k < 3; ++k)
            {
                a[k] += weight * force->strength * force->vector[k];
            }
            break;
        case mvfx_forceAttractor:
            Attract(force, x, weight, a);
            break;
        case mvfx_forceTurbulence:
        case mvfx_forceVectorField:
        case mvfx_forceGas:
            Field(force, x, time, weight, a, drag);
            break;
        default:
            Swirl(force, x, weight, a);
            break;
        }
    }
}

void mvfxUnmapVector(const mvfxSpaceMap* map, const float v[3], float out[3])
{
    const float* q = map->rotation;
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    mvfxRotate(inverse, v, out);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] /= map->sigma;
    }
}

void mvfxMapPointDouble(const mvfxSpaceMap* map, const double p[3], float out[3])
{
    double d[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = (p[k] - map->before[k]) * (double)map->sigma;
    }
    double turned[3];
    mvfxRotateDouble(map->rotation, d, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (float)(turned[k] + map->after[k]);
    }
}

// A turbulence's noise for a plan starting at time: per octave, the
// lattice coordinate of the particles' space's origin, as mvfxCurlNoise
// takes a place's, split into its cell and fraction.
static void PlanNoise(const mvfxResolvedForce* force, double time, mvfxPlannedForce* out)
{
    const mvfxNoise* noise = &force->source->noise;
    static const float origin[3] = {0.0f, 0.0f, 0.0f};
    double place[3];
    mvfxUnmapPoint(&force->map, origin, place);
    double moved[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        moved[k] = place[k] + (double)noise->scroll[k] * time;
        out->scroll[k] = noise->scroll[k];
    }
    out->octaves = noise->octaves;
    out->seed = force->seed;
    out->gain = noise->gain;
    double frequency = (double)noise->frequency;
    for (int32_t o = 0; o < noise->octaves; ++o)
    {
        out->frequency[o] = (float)frequency;
        for (int32_t k = 0; k < 3; ++k)
        {
            out->cell[o][k] = mvfxNoiseCell(moved[k] * frequency, &out->fraction[o][k]);
        }
        frequency *= (double)noise->lacunarity;
    }
}

// A vector field's box for a plan: the particles' origin mapped back,
// less the box's center, as SampleBox takes a place's offset.
static void PlanBox(const mvfxResolvedForce* force, mvfxPlannedForce* out)
{
    const mvfxForce* source = force->source;
    static const float origin[3] = {0.0f, 0.0f, 0.0f};
    double place[3];
    mvfxUnmapPoint(&force->map, origin, place);
    for (int32_t k = 0; k < 3; ++k)
    {
        out->boxOrigin[k] = (float)(place[k] - (double)source->point[k]);
        out->boxSize[k] = source->boxSize[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        out->boxRotation[k] = source->boxRotation[k];
    }
    out->border = source->border;
}

// A gas force's box for a plan: the particles' origin in the gas's box's
// frame, and its cell's edge along each axis.
static void PlanGas(const mvfxResolvedForce* force, mvfxPlannedForce* out)
{
    static const float origin[3] = {0.0f, 0.0f, 0.0f};
    double place[3];
    mvfxUnmapPoint(&force->map, origin, place);
    for (int32_t k = 0; k < 3; ++k)
    {
        out->boxOrigin[k] = (float)place[k];
        out->boxSize[k] = force->gasLattice.cell;
    }
    out->boxRotation[3] = 1.0f;
}

mvfxPlannedForce mvfxPlannedForceOf(const mvfxResolvedForce* force, double time)
{
    mvfxPlannedForce out = {
        .kind = force->kind,
        .falloff = force->falloff,
        .fieldMode = force->fieldMode,
        .strength = force->strength,
        .softening = force->softening,
        .killRadius = force->killRadius,
        .pull = force->pull,
        .radius = force->radius,
        .cosine = force->cosine,
        .exponent = force->exponent,
        .tightness = force->tightness,
        .sigma = force->map.sigma,
    };
    for (int32_t k = 0; k < 3; ++k)
    {
        out.vector[k] = force->vector[k];
        out.point[k] = force->point[k];
        out.center[k] = force->center[k];
        out.axis[k] = force->axis[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        out.rotation[k] = force->map.rotation[k];
    }
    if (force->kind == mvfx_forceTurbulence)
    {
        PlanNoise(force, time, &out);
    }
    if (force->kind == mvfx_forceVectorField)
    {
        PlanBox(force, &out);
    }
    if (force->kind == mvfx_forceGas)
    {
        PlanGas(force, &out);
    }
    return out;
}
