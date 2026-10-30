// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The checks and compiled forms of an emitter's parts beside its values:
// neighbors, steering, contacts, its pair force, plexus links and units.

#include "effect_parts.h"

#include "direction.h"
#include "life.h"
#include "refusal.h"

#include <math.h>

// Whether low <= x <= high; false for NaN.
static bool IsWithin(float x, float low, float high)
{
    return x >= low && x <= high;
}

// Whether a steer point is valid: a weight, a point or parameter, a
// space and a radius in range.
static mvfxResult CheckSteerPoint(const mvfxFieldPath* at, const mvfxSteerPoint* point,
                                  int32_t parameterCount)
{
    if (point->space > mvfx_spaceLocal)
    {
        return mvfxRefuseField(at, MVFX_TAG_STEER_POINT_SPACE, mvfx_errorInvalid);
    }
    if (point->fromParameter && (point->parameter < 0 || point->parameter >= parameterCount))
    {
        return mvfxRefuseField(at, MVFX_TAG_STEER_POINT_PARAMETER, mvfx_errorInvalid);
    }
    mvfxResult valid = mvfxRefuseField(at, MVFX_TAG_STEER_POINT_WEIGHT,
                                       mvfxUpToBound(point->weight, 0.0f, MVFX_MAX_EXTENT));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_STEER_POINT_RADIUS,
                                  mvfxUpToBound(point->radius, 0.0f, MVFX_MAX_EXTENT));
    return valid != mvfx_success
               ? valid
               : mvfxRefuseVector(at, MVFX_TAG_STEER_POINT_POINT, point->point, MVFX_MAX_EXTENT);
}

// Whether a steering path is valid: none (weight 0), or its points, space,
// radius and lookahead in range.
static mvfxResult CheckSteerPath(const mvfxFieldPath* at, const mvfxSteerPath* path)
{
    mvfxResult weight = mvfxRefuseField(at, MVFX_TAG_STEER_PATH_WEIGHT,
                                        mvfxUpToBound(path->weight, 0.0f, MVFX_MAX_EXTENT));
    if (weight != mvfx_success || path->weight == 0.0f)
    {
        return weight;
    }
    if (path->points == nullptr || path->pointCount < 2)
    {
        return mvfxRefuseField(at, MVFX_TAG_STEER_PATH_POINTS, mvfx_errorInvalid);
    }
    if (path->space > mvfx_spaceLocal)
    {
        return mvfxRefuseField(at, MVFX_TAG_STEER_PATH_SPACE, mvfx_errorInvalid);
    }
    if (!IsWithin(path->lookahead, 0.0f, 10.0f))
    {
        return mvfxRefuseField(at, MVFX_TAG_STEER_PATH_LOOKAHEAD, mvfx_errorInvalid);
    }
    // The count bounds the points read below.
    mvfxResult fields = mvfxRefuseField(at, MVFX_TAG_STEER_PATH_POINTS,
                                        mvfxWithinBound(path->pointCount <= MVFX_MAX_PATH_POINTS));
    fields = fields != mvfx_success
                 ? fields
                 : mvfxRefuseField(at, MVFX_TAG_STEER_PATH_RADIUS,
                                   mvfxUpToBound(path->radius, 0.0f, MVFX_MAX_EXTENT));
    if (fields != mvfx_success)
    {
        return fields;
    }
    for (int32_t i = 0; i < 3 * path->pointCount; ++i)
    {
        mvfxResult point = mvfxBounded(path->points[i], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT);
        if (point != mvfx_success)
        {
            return mvfxRefuseItem(at, MVFX_TAG_STEER_PATH_POINTS, i / 3, point);
        }
    }
    return mvfx_success;
}

// Whether a flocking rule is valid: no weight, or a weight with neighbors
// and a radius within theirs; naming its weight or its radius below the
// steering's path at.
static mvfxResult CheckFlockRule(const mvfxFieldPath* at, uint32_t weightTag, uint32_t radiusTag,
                                 const mvfxEmitterDef* def, float weight, float radius)
{
    mvfxResult bounded =
        mvfxRefuseField(at, weightTag, mvfxUpToBound(weight, 0.0f, MVFX_MAX_EXTENT));
    if (bounded != mvfx_success || weight == 0.0f)
    {
        return bounded;
    }
    return mvfxRefuseField(
        at, radiusTag,
        mvfxRequire(def->neighbors.enable && radius > 0.0f && radius <= def->neighbors.radius));
}

// Whether an emitter's pair force is valid: a radius within its
// neighbors', a strength in range.
static mvfxResult CheckPairForce(const mvfxFieldPath* emitter, const mvfxEmitterDef* def)
{
    const mvfxPairForceDef* pair = &def->pairForce;
    if (!pair->enable)
    {
        return mvfx_success;
    }
    mvfxFieldPath at = mvfxFieldIn(emitter, MVFX_TAG_EMITTER_DEF_PAIR_FORCE, -1);
    if (!def->neighbors.enable || !(pair->radius > 0.0f) ||
        !(pair->radius <= def->neighbors.radius))
    {
        return mvfxRefuseField(&at, MVFX_TAG_PAIR_FORCE_DEF_RADIUS, mvfx_errorInvalid);
    }
    mvfxFieldPath strengthAt = mvfxFieldIn(&at, MVFX_TAG_PAIR_FORCE_DEF_STRENGTH, -1);
    mvfxResult strength = mvfxCheckLifeCurve(&strengthAt, &pair->strength);
    return strength != mvfx_success
               ? strength
               : mvfxRefuseField(
                     &strengthAt, MVFX_TAG_LIFE_CURVE_VALUE,
                     mvfxBounded(pair->strength.value, -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT));
}

// Whether a direction is in range and of a length that normalizes, naming
// the field of a tag below at; out the direction normalized.
static mvfxResult CheckDirection(const mvfxFieldPath* at, uint32_t tag, const float v[3],
                                 float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = v[k];
    }
    mvfxResult within = mvfxRefuseVector(at, tag, v, MVFX_MAX_EXTENT);
    return within != mvfx_success ? within
                                  : mvfxRefuseField(at, tag, mvfxRequire(mvfxNormalize(out)));
}

// Whether a unit's shape is valid: a chain's points, or a sheet's sides
// and two directions at least 3 degrees apart (the cosine between them
// at most cos 3 degrees either way).
static mvfxResult CheckUnitShape(const mvfxFieldPath* at, const mvfxUnitDef* unit)
{
    float across[3];
    mvfxResult direction = CheckDirection(at, MVFX_TAG_UNIT_DEF_DIRECTION, unit->direction, across);
    if (direction != mvfx_success)
    {
        return direction;
    }
    mvfxResult spacing = mvfxRefuseField(at, MVFX_TAG_UNIT_DEF_SPACING,
                                         mvfxPositiveUpTo(unit->spacing, MVFX_MAX_EXTENT));
    if (spacing != mvfx_success)
    {
        return spacing;
    }
    if (unit->kind == mvfx_unitChain)
    {
        return mvfxRefuseField(at, MVFX_TAG_UNIT_DEF_POINTS,
                               mvfxCountUpTo(unit->points, 2, MVFX_MAX_CHAIN_POINTS));
    }
    if (unit->kind != mvfx_unitSheet)
    {
        return mvfxRefuseField(at, MVFX_TAG_UNIT_DEF_KIND, mvfx_errorInvalid);
    }
    float down[3];
    mvfxResult sides = mvfxRefuseField(at, MVFX_TAG_UNIT_DEF_POINTS,
                                       mvfxCountUpTo(unit->points, 2, MVFX_MAX_SHEET_SIDE));
    sides = sides != mvfx_success
                ? sides
                : mvfxRefuseField(at, MVFX_TAG_UNIT_DEF_ROWS,
                                  mvfxCountUpTo(unit->rows, 2, MVFX_MAX_SHEET_SIDE));
    sides = sides != mvfx_success
                ? sides
                : CheckDirection(at, MVFX_TAG_UNIT_DEF_DOWN_DIRECTION, unit->downDirection, down);
    if (sides != mvfx_success)
    {
        return sides;
    }
    float cosine = across[0] * down[0] + across[1] * down[1] + across[2] * down[2];
    if (!(fabsf(cosine) <= 0.99863f))
    {
        return mvfxRefuseField(at, MVFX_TAG_UNIT_DEF_DOWN_DIRECTION, mvfx_errorInvalid);
    }
    const float drags[3] = {unit->shearCompliance, unit->normalDrag, unit->tangentDrag};
    const uint32_t tags[3] = {MVFX_TAG_UNIT_DEF_SHEAR_COMPLIANCE, MVFX_TAG_UNIT_DEF_NORMAL_DRAG,
                              MVFX_TAG_UNIT_DEF_TANGENT_DRAG};
    mvfxResult valid = mvfx_success;
    for (int32_t k = 0; valid == mvfx_success && k < 3; ++k)
    {
        valid = mvfxRefuseField(at, tags[k], mvfxUpToBound(drags[k], 0.0f, MVFX_MAX_EXTENT));
    }
    return valid;
}

// Whether an emitter's units are valid: a particle, or a chain or sheet in
// range whose capacities hold whole units, not on a beam nor (a sheet)
// drawn as ribbons.
static mvfxResult CheckUnit(const mvfxFieldPath* emitter, const mvfxEmitterDef* def)
{
    const mvfxUnitDef* unit = &def->unit;
    if (unit->kind == mvfx_unitParticle)
    {
        return mvfx_success;
    }
    mvfxFieldPath at = mvfxFieldIn(emitter, MVFX_TAG_EMITTER_DEF_UNIT, -1);
    bool sheet = unit->kind == mvfx_unitSheet;
    if (def->beam.enable || (sheet && def->ribbon.ids != mvfx_ribbonNone))
    {
        return mvfxRefuseField(&at, MVFX_TAG_UNIT_DEF_KIND, mvfx_errorInvalid);
    }
    // The shape first: it bounds the product below.
    mvfxResult shape = CheckUnitShape(&at, unit);
    if (shape != mvfx_success)
    {
        return shape;
    }
    bool self = unit->selfThickness != 0.0f;
    uint32_t refused = unit->substeps < 1 ? MVFX_TAG_UNIT_DEF_SUBSTEPS
                       : self && !IsWithin(unit->selfFriction, 0.0f, 1.0f)
                           ? MVFX_TAG_UNIT_DEF_SELF_FRICTION
                       : self && unit->selfContacts < 1 ? MVFX_TAG_UNIT_DEF_SELF_CONTACTS
                                                        : 0;
    if (refused != 0)
    {
        return mvfxRefuseField(&at, refused, mvfx_errorInvalid);
    }
    mvfxResult solve = mvfxRefuseField(&at, MVFX_TAG_UNIT_DEF_SUBSTEPS,
                                       mvfxWithinBound(unit->substeps <= MVFX_MAX_SUBSTEPS));
    const float extents[5] = {unit->stretchCompliance, unit->bendCompliance, unit->damping,
                              unit->tearStrain, unit->selfThickness};
    const float bounds[5] = {MVFX_MAX_EXTENT, MVFX_MAX_EXTENT, MVFX_MAX_DRAG, MVFX_MAX_EXTENT,
                             MVFX_MAX_EXTENT};
    const uint32_t tags[5] = {MVFX_TAG_UNIT_DEF_STRETCH_COMPLIANCE,
                              MVFX_TAG_UNIT_DEF_BEND_COMPLIANCE, MVFX_TAG_UNIT_DEF_DAMPING,
                              MVFX_TAG_UNIT_DEF_TEAR_STRAIN, MVFX_TAG_UNIT_DEF_SELF_THICKNESS};
    for (int32_t k = 0; solve == mvfx_success && k < 5; ++k)
    {
        solve = mvfxRefuseField(&at, tags[k], mvfxUpToBound(extents[k], 0.0f, bounds[k]));
    }
    solve = solve != mvfx_success || !self
                ? solve
                : mvfxRefuseField(&at, MVFX_TAG_UNIT_DEF_SELF_CONTACTS,
                                  mvfxWithinBound(unit->selfContacts <= MVFX_MAX_NEIGHBORS));
    if (solve != mvfx_success)
    {
        return solve;
    }
    int32_t points = sheet ? unit->points * unit->rows : unit->points;
    int32_t units = def->capacity / points;
    if (def->capacity % points != 0)
    {
        return mvfxRefuseField(emitter, MVFX_TAG_EMITTER_DEF_CAPACITY, mvfx_errorInvalid);
    }
    if (def->hostSpawnCapacity > units)
    {
        return mvfxRefuseField(emitter, MVFX_TAG_EMITTER_DEF_HOST_SPAWN_CAPACITY,
                               mvfx_errorInvalid);
    }
    mvfxFieldPath spawn = mvfxFieldIn(emitter, MVFX_TAG_EMITTER_DEF_EVENT_SPAWN, -1);
    return mvfxRefuseField(
        &spawn, MVFX_TAG_EVENT_SPAWN_CAPACITY,
        mvfxRequire(!def->eventSpawn.enable || def->eventSpawn.capacity <= units));
}

// The length of a + sign b.
static float LengthOf(const float a[3], const float b[3], float sign)
{
    float square = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        float d = a[k] + sign * b[k];
        square += d * d;
    }
    return sqrtf(square);
}

// Compiles an emitter's units: the directions normalized and what its
// kind does not read cleared; its points, width and rest lengths.
static void CompileUnit(const mvfxUnitDef* unit, mvfxCompiledEmitter* out)
{
    out->unitPoints = 1;
    out->unitWidth = 1;
    if (unit->kind == mvfx_unitParticle)
    {
        // A particle reads nothing of it.
        out->unit = (mvfxUnitDef){0};
        return;
    }
    out->unit = *unit;
    mvfxUnitDef* u = &out->unit;
    (void)mvfxNormalize(u->direction);
    out->unitWidth = unit->points;
    out->unitPoints = unit->points;
    out->unitRests[0] = unit->spacing;
    if (unit->kind == mvfx_unitChain)
    {
        u->rows = 1;
        u->shearCompliance = 0.0f;
        u->normalDrag = 0.0f;
        u->tangentDrag = 0.0f;
        u->pinFirstRow = false;
        for (int32_t k = 0; k < 3; ++k)
        {
            u->downDirection[k] = 0.0f;
        }
        return;
    }
    (void)mvfxNormalize(u->downDirection);
    out->unitPoints = unit->points * unit->rows;
    out->unitRests[1] = unit->spacing;
    out->unitRests[2] = unit->spacing * LengthOf(u->direction, u->downDirection, 1.0f);
    out->unitRests[3] = unit->spacing * LengthOf(u->direction, u->downDirection, -1.0f);
}

// Whether emitter i's units' attachment is valid: none; or with a pinned
// point, the instance for a world-space emitter, the host's frames with
// room for some, or the particles of an event source in the same space.
static mvfxResult CheckAttach(const mvfxFieldPath* emitterAt, const mvfxEffectDef* def, int32_t i)
{
    const mvfxEmitterDef* emitter = &def->emitters[i];
    const mvfxUnitDef* unit = &emitter->unit;
    if (unit->kind == mvfx_unitParticle || unit->attach == mvfx_attachNone)
    {
        return mvfx_success;
    }
    mvfxFieldPath at = mvfxFieldIn(emitterAt, MVFX_TAG_EMITTER_DEF_UNIT, -1);
    bool pinned = unit->pinFirst || (unit->kind == mvfx_unitSheet && unit->pinFirstRow);
    switch (unit->attach)
    {
    case mvfx_attachInstance:
        return mvfxRefuseField(&at, MVFX_TAG_UNIT_DEF_ATTACH,
                               mvfxRequire(pinned && emitter->space == mvfx_spaceWorld));
    case mvfx_attachHost:
        return pinned ? mvfxRefuseField(
                            &at, MVFX_TAG_UNIT_DEF_ATTACH_FRAME_CAPACITY,
                            mvfxCountUpTo(unit->attachFrameCapacity, 1, MVFX_MAX_ATTACH_FRAMES))
                      : mvfxRefuseField(&at, MVFX_TAG_UNIT_DEF_ATTACH, mvfx_errorInvalid);
    case mvfx_attachParticles:
        // The source is in range: the event spawn's own check passed.
        return mvfxRefuseField(
            &at, MVFX_TAG_UNIT_DEF_ATTACH,
            mvfxRequire(pinned && emitter->eventSpawn.enable &&
                        def->emitters[emitter->eventSpawn.source].space == emitter->space));
    default:
        return mvfxRefuseField(&at, MVFX_TAG_UNIT_DEF_ATTACH, mvfx_errorInvalid);
    }
}

static bool IsSameCurve(const mvfxCurve* a, const mvfxCurve* b)
{
    bool same = a->keyCount == b->keyCount && a->tangents == b->tangents;
    for (int32_t i = 0; same && i < a->keyCount; ++i)
    {
        const mvfxCurveKey* x = &a->keys[i];
        const mvfxCurveKey* y = &b->keys[i];
        same = x->time == y->time && x->value == y->value && x->inTangent == y->inTangent &&
               x->outTangent == y->outTangent && x->interpolation == y->interpolation;
    }
    return same;
}

// Whether an emitter's sizes keep the axes its rigid shape does not take
// equal to X (mvfxRigidShape): its size source's ends on them X's (a
// birth takes X's draw for them, mvfxTieSize), and with size over life
// per axis, their curves X's. The emitter's field that ties none, size or
// overLife, 0 when all are tied.
static uint32_t UntiedSize(const mvfxEmitterDef* def)
{
    int32_t axes = mvfxSizeAxes(&def->rigid);
    const mvfxVectorSource* size = &def->size;
    bool drawn = size->kind != mvfx_sourceNone;
    const mvfxSizeOverLife* over = &def->overLife.size;
    for (int32_t k = axes; k < 3; ++k)
    {
        if (drawn && (size->min[k] != size->min[0] || size->max[k] != size->max[0]))
        {
            return MVFX_TAG_EMITTER_DEF_SIZE;
        }
        if (over->enable && over->perAxis &&
            (over->size[k].value != over->size[0].value ||
             !IsSameCurve(&over->size[k].curve, &over->size[0].curve)))
        {
            return MVFX_TAG_EMITTER_DEF_OVER_LIFE;
        }
    }
    return 0;
}

// The field of an emitter's rigid particles out of a range that no named
// bound covers, 0 for none: enable for rigid units or a rigid beam.
static uint32_t RefusedRigidField(const mvfxEmitterDef* def)
{
    const mvfxRigidDef* rigid = &def->rigid;
    if (rigid->shape > mvfx_rigidHull)
    {
        return MVFX_TAG_RIGID_DEF_SHAPE;
    }
    if (rigid->substeps < 1)
    {
        return MVFX_TAG_RIGID_DEF_SUBSTEPS;
    }
    if (!IsWithin(rigid->restitution, 0.0f, 1.0f))
    {
        return MVFX_TAG_RIGID_DEF_RESTITUTION;
    }
    if (!(rigid->dynamicFriction >= 0.0f))
    {
        return MVFX_TAG_RIGID_DEF_DYNAMIC_FRICTION;
    }
    if (!(rigid->staticFriction >= rigid->dynamicFriction))
    {
        return MVFX_TAG_RIGID_DEF_STATIC_FRICTION;
    }
    if (rigid->bodyContacts < 0)
    {
        return MVFX_TAG_RIGID_DEF_BODY_CONTACTS;
    }
    if (rigid->sleepSpeed != 0.0f && !(rigid->sleepTime > 0.0f))
    {
        return MVFX_TAG_RIGID_DEF_SLEEP_TIME;
    }
    return def->unit.kind != mvfx_unitParticle || def->beam.enable ? MVFX_TAG_RIGID_DEF_ENABLE : 0;
}

// Whether an emitter's rigid particles are valid: a shape, a density,
// substeps, restitution and friction in range, its sizes tied to its
// shape's axes; particles, not units nor a beam.
static mvfxResult CheckRigid(const mvfxFieldPath* emitter, const mvfxEmitterDef* def)
{
    const mvfxRigidDef* rigid = &def->rigid;
    if (!rigid->enable)
    {
        return mvfx_success;
    }
    mvfxFieldPath at = mvfxFieldIn(emitter, MVFX_TAG_EMITTER_DEF_RIGID, -1);
    uint32_t refused = RefusedRigidField(def);
    if (refused != 0)
    {
        return mvfxRefuseField(&at, refused, mvfx_errorInvalid);
    }
    // An axis its shape does not set tied to X: as the size's and the
    // size over life's comments say.
    uint32_t untied = UntiedSize(def);
    if (untied == MVFX_TAG_EMITTER_DEF_SIZE)
    {
        return mvfxRefuseField(emitter, untied, mvfx_errorInvalid);
    }
    if (untied != 0)
    {
        mvfxFieldPath overLife = mvfxFieldIn(emitter, MVFX_TAG_EMITTER_DEF_OVER_LIFE, -1);
        return mvfxRefuseField(&overLife, MVFX_TAG_OVER_LIFE_SIZE, mvfx_errorInvalid);
    }
    mvfxResult valid = mvfxRefuseField(&at, MVFX_TAG_RIGID_DEF_DENSITY,
                                       mvfxPositiveUpTo(rigid->density, MVFX_MAX_EXTENT));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(&at, MVFX_TAG_RIGID_DEF_SUBSTEPS,
                                  mvfxWithinBound(rigid->substeps <= MVFX_MAX_SUBSTEPS));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(&at, MVFX_TAG_RIGID_DEF_DYNAMIC_FRICTION,
                                  mvfxBounded(rigid->dynamicFriction, 0.0f, MVFX_MAX_DRAG));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(&at, MVFX_TAG_RIGID_DEF_STATIC_FRICTION,
                                  mvfxBounded(rigid->staticFriction, 0.0f, MVFX_MAX_DRAG));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(&at, MVFX_TAG_RIGID_DEF_BODY_CONTACTS,
                                  mvfxWithinBound(rigid->bodyContacts <= MVFX_MAX_BODY_CONTACTS));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(&at, MVFX_TAG_RIGID_DEF_SLEEP_SPEED,
                                  mvfxUpToBound(rigid->sleepSpeed, 0.0f, MVFX_MAX_EXTENT));
    return valid != mvfx_success || rigid->sleepSpeed == 0.0f
               ? valid
               : mvfxRefuseField(&at, MVFX_TAG_RIGID_DEF_SLEEP_TIME,
                                 mvfxBounded(rigid->sleepTime, 0.0f, MVFX_MAX_SLEEP_TIME));
}

// Whether an emitter's liquid is valid: each parameter in range, the
// kernel radius within the neighbor radii; particles, not units, rigid
// bodies nor a beam.
// The field of a liquid out of a range that no named bound covers, 0 for
// none: enable for a liquid of units, rigid bodies or a beam.
static uint32_t RefusedLiquidField(const mvfxEmitterDef* def)
{
    const mvfxLiquidDef* liquid = &def->liquid;
    if (liquid->substeps < 1)
    {
        return MVFX_TAG_LIQUID_DEF_SUBSTEPS;
    }
    if (liquid->iterations < 1)
    {
        return MVFX_TAG_LIQUID_DEF_ITERATIONS;
    }
    if (!(liquid->relaxation > 0.0f && liquid->relaxation <= 1.0f))
    {
        return MVFX_TAG_LIQUID_DEF_RELAXATION;
    }
    if (liquid->maxNeighbors < 1)
    {
        return MVFX_TAG_LIQUID_DEF_MAX_NEIGHBORS;
    }
    if (!IsWithin(liquid->tensile, 0.0f, 1.0f))
    {
        return MVFX_TAG_LIQUID_DEF_TENSILE;
    }
    if (!(liquid->tensileDistance > 0.0f && liquid->tensileDistance < 1.0f))
    {
        return MVFX_TAG_LIQUID_DEF_TENSILE_DISTANCE;
    }
    if (liquid->tensilePower < 1)
    {
        return MVFX_TAG_LIQUID_DEF_TENSILE_POWER;
    }
    if (!IsWithin(liquid->viscosity, 0.0f, 1.0f))
    {
        return MVFX_TAG_LIQUID_DEF_VISCOSITY;
    }
    if (liquid->solver > mvfx_liquidGrid)
    {
        return MVFX_TAG_LIQUID_DEF_SOLVER;
    }
    return def->unit.kind != mvfx_unitParticle || def->rigid.enable || def->beam.enable
               ? MVFX_TAG_LIQUID_DEF_ENABLE
               : 0;
}

static mvfxResult CheckLiquid(const mvfxFieldPath* emitter, const mvfxEmitterDef* def)
{
    const mvfxLiquidDef* liquid = &def->liquid;
    if (!liquid->enable)
    {
        return mvfx_success;
    }
    mvfxFieldPath at = mvfxFieldIn(emitter, MVFX_TAG_EMITTER_DEF_LIQUID, -1);
    uint32_t refused = RefusedLiquidField(def);
    if (refused != 0)
    {
        return mvfxRefuseField(&at, refused, mvfx_errorInvalid);
    }
    // The kernel's scale first: it divides the spacing's bound.
    mvfxResult valid = mvfxRefuseField(
        &at, MVFX_TAG_LIQUID_DEF_KERNEL_SCALE,
        mvfxBounded(liquid->kernelScale, MVFX_MIN_LIQUID_KERNEL, MVFX_MAX_LIQUID_KERNEL));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(&at, MVFX_TAG_LIQUID_DEF_SPACING,
                                  mvfxBounded(liquid->spacing, MVFX_MIN_NEIGHBOR_RADIUS,
                                              MVFX_MAX_NEIGHBOR_RADIUS / liquid->kernelScale));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(&at, MVFX_TAG_LIQUID_DEF_SUBSTEPS,
                                  mvfxWithinBound(liquid->substeps <= MVFX_MAX_SUBSTEPS));
    valid =
        valid != mvfx_success
            ? valid
            : mvfxRefuseField(&at, MVFX_TAG_LIQUID_DEF_ITERATIONS,
                              mvfxWithinBound(liquid->iterations <= MVFX_MAX_LIQUID_ITERATIONS));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(&at, MVFX_TAG_LIQUID_DEF_MAX_NEIGHBORS,
                                  mvfxWithinBound(liquid->maxNeighbors <= MVFX_MAX_NEIGHBORS));
    valid = valid != mvfx_success ? valid
                                  : mvfxRefuseField(&at, MVFX_TAG_LIQUID_DEF_TENSILE_POWER,
                                                    mvfxWithinBound(liquid->tensilePower <=
                                                                    MVFX_MAX_LIQUID_TENSILE_POWER));
    return valid != mvfx_success
               ? valid
               : mvfxRefuseField(&at, MVFX_TAG_LIQUID_DEF_VORTICITY,
                                 mvfxUpToBound(liquid->vorticity, 0.0f, MVFX_MAX_EXTENT));
}

// Whether an emitter's plexus links are valid.
static mvfxResult CheckPlexus(const mvfxFieldPath* at, const mvfxPlexusDef* plexus)
{
    if (!plexus->enable)
    {
        return mvfx_success;
    }
    mvfxResult valid = mvfxRefuseField(
        at, MVFX_TAG_PLEXUS_DEF_DISTANCE,
        mvfxBounded(plexus->distance, MVFX_MIN_NEIGHBOR_RADIUS, MVFX_MAX_NEIGHBOR_RADIUS));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_PLEXUS_DEF_MAX_LINKS,
                                  mvfxCountUpTo(plexus->maxLinks, 1, MVFX_MAX_PLEXUS_LINKS));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_PLEXUS_DEF_MAX_PER_PARTICLE,
                                  mvfxCountUpTo(plexus->maxPerParticle, 1, MVFX_MAX_NEIGHBORS));
    return valid != mvfx_success
               ? valid
               : mvfxRefuseField(at, MVFX_TAG_PLEXUS_DEF_WIDTH,
                                 mvfxUpToBound(plexus->width, 0.0f, MVFX_MAX_EXTENT));
}

// Whether an emitter's particle contacts are valid.
static mvfxResult CheckContacts(const mvfxFieldPath* at, const mvfxContactDef* contacts)
{
    if (!contacts->enable)
    {
        return mvfx_success;
    }
    if (!IsWithin(contacts->restitution, 0.0f, 1.0f))
    {
        return mvfxRefuseField(at, MVFX_TAG_CONTACT_DEF_RESTITUTION, mvfx_errorInvalid);
    }
    if (!IsWithin(contacts->friction, 0.0f, 1.0f))
    {
        return mvfxRefuseField(at, MVFX_TAG_CONTACT_DEF_FRICTION, mvfx_errorInvalid);
    }
    mvfxResult valid = mvfxRefuseField(at, MVFX_TAG_CONTACT_DEF_RADIUS_SCALE,
                                       mvfxPositiveUpTo(contacts->radiusScale, MVFX_MAX_EXTENT));
    valid =
        valid != mvfx_success
            ? valid
            : mvfxRefuseField(at, MVFX_TAG_CONTACT_DEF_ITERATIONS,
                              mvfxCountUpTo(contacts->iterations, 1, MVFX_MAX_CONTACT_ITERATIONS));
    return valid != mvfx_success
               ? valid
               : mvfxRefuseField(at, MVFX_TAG_CONTACT_DEF_MAX_CONTACTS,
                                 mvfxCountUpTo(contacts->maxContacts, 1, MVFX_MAX_NEIGHBORS));
}

// Whether an emitter's steering is valid.
static mvfxResult CheckSteering(const mvfxFieldPath* emitter, const mvfxEmitterDef* def,
                                int32_t parameterCount)
{
    const mvfxSteeringDef* s = &def->steering;
    if (!s->enable)
    {
        return mvfx_success;
    }
    mvfxFieldPath at = mvfxFieldIn(emitter, MVFX_TAG_EMITTER_DEF_STEERING, -1);
    if (!IsWithin(s->viewAngle, 0.0f, 3.14159274f))
    {
        return mvfxRefuseField(&at, MVFX_TAG_STEERING_DEF_VIEW_ANGLE, mvfx_errorInvalid);
    }
    mvfxResult valid = CheckFlockRule(&at, MVFX_TAG_STEERING_DEF_SEPARATION,
                                      MVFX_TAG_STEERING_DEF_SEPARATION_RADIUS, def, s->separation,
                                      s->separationRadius);
    valid = valid != mvfx_success ? valid
                                  : CheckFlockRule(&at, MVFX_TAG_STEERING_DEF_ALIGNMENT,
                                                   MVFX_TAG_STEERING_DEF_ALIGNMENT_RADIUS, def,
                                                   s->alignment, s->alignmentRadius);
    valid = valid != mvfx_success ? valid
                                  : CheckFlockRule(&at, MVFX_TAG_STEERING_DEF_COHESION,
                                                   MVFX_TAG_STEERING_DEF_COHESION_RADIUS, def,
                                                   s->cohesion, s->cohesionRadius);
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(&at, MVFX_TAG_STEERING_DEF_CRUISE_SPEED,
                                  mvfxUpToBound(s->cruiseSpeed, 0.0f, MVFX_MAX_EXTENT));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(&at, MVFX_TAG_STEERING_DEF_MAX_FORCE,
                                  mvfxUpToBound(s->maxForce, 0.0f, MVFX_MAX_EXTENT));
    mvfxFieldPath seek = mvfxFieldIn(&at, MVFX_TAG_STEERING_DEF_SEEK, -1);
    valid = valid != mvfx_success ? valid : CheckSteerPoint(&seek, &s->seek, parameterCount);
    mvfxFieldPath flee = mvfxFieldIn(&at, MVFX_TAG_STEERING_DEF_FLEE, -1);
    valid = valid != mvfx_success ? valid : CheckSteerPoint(&flee, &s->flee, parameterCount);
    mvfxFieldPath path = mvfxFieldIn(&at, MVFX_TAG_STEERING_DEF_PATH, -1);
    return valid != mvfx_success ? valid : CheckSteerPath(&path, &s->path);
}

// Whether an emitter's neighbor queries are valid against its effect: a
// source of its own or in its space, a radius and a cap in range.
static mvfxResult CheckNeighbors(const mvfxFieldPath* emitter, const mvfxEffectDef* def, int32_t i)
{
    const mvfxNeighborDef* neighbors = &def->emitters[i].neighbors;
    if (!neighbors->enable)
    {
        return mvfx_success;
    }
    mvfxFieldPath at = mvfxFieldIn(emitter, MVFX_TAG_EMITTER_DEF_NEIGHBORS, -1);
    int32_t source = neighbors->source;
    bool sourceValid = source == -1 || (source >= 0 && source < def->emitterCount &&
                                        def->emitters[source].space == def->emitters[i].space);
    if (!sourceValid)
    {
        return mvfxRefuseField(&at, MVFX_TAG_NEIGHBOR_DEF_SOURCE, mvfx_errorInvalid);
    }
    mvfxResult valid = mvfxRefuseField(
        &at, MVFX_TAG_NEIGHBOR_DEF_RADIUS,
        mvfxBounded(neighbors->radius, MVFX_MIN_NEIGHBOR_RADIUS, MVFX_MAX_NEIGHBOR_RADIUS));
    return valid != mvfx_success
               ? valid
               : mvfxRefuseField(&at, MVFX_TAG_NEIGHBOR_DEF_MAX_NEIGHBORS,
                                 mvfxCountUpTo(neighbors->maxNeighbors, 1, MVFX_MAX_NEIGHBORS));
}

static mvfxResult CheckEmitterParts(const mvfxFieldPath* root, const mvfxEffectDef* def, int32_t i)
{
    const mvfxEmitterDef* emitter = &def->emitters[i];
    mvfxFieldPath at = mvfxFieldIn(root, MVFX_TAG_EFFECT_DEF_EMITTERS, i);
    mvfxFieldPath contacts = mvfxFieldIn(&at, MVFX_TAG_EMITTER_DEF_CONTACTS, -1);
    mvfxFieldPath plexus = mvfxFieldIn(&at, MVFX_TAG_EMITTER_DEF_PLEXUS, -1);
    mvfxResult valid = CheckNeighbors(&at, def, i);
    valid = valid != mvfx_success ? valid : CheckSteering(&at, emitter, def->parameterCount);
    valid = valid != mvfx_success ? valid : CheckContacts(&contacts, &emitter->contacts);
    valid = valid != mvfx_success ? valid : CheckPairForce(&at, emitter);
    valid = valid != mvfx_success ? valid : CheckPlexus(&plexus, &emitter->plexus);
    valid = valid != mvfx_success ? valid : CheckUnit(&at, emitter);
    valid = valid != mvfx_success ? valid : CheckAttach(&at, def, i);
    valid = valid != mvfx_success ? valid : CheckRigid(&at, emitter);
    return valid != mvfx_success ? valid : CheckLiquid(&at, emitter);
}

// Copies an emitter's steering, its path's points into the compiled
// emitter so nothing of the host's is kept.
static void CopySteering(const mvfxEmitterDef* def, mvfxCompiledEmitter* out)
{
    out->steering = def->steering.enable ? def->steering : (mvfxSteeringDef){0};
    const mvfxSteerPath* path = &out->steering.path;
    for (int32_t i = 0; path->weight > 0.0f && i < path->pointCount; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            out->pathPoints[i][k] = def->steering.path.points[3 * i + k];
        }
    }
    out->steering.path.points = nullptr;
}

static void CompileEmitterParts(const mvfxEmitterDef* def, mvfxCompiledEmitter* out)
{
    CopySteering(def, out);
    out->rigid = def->rigid.enable ? def->rigid : (mvfxRigidDef){0};
    out->liquid = def->liquid.enable ? def->liquid : (mvfxLiquidDef){0};
    out->plexus = def->plexus.enable ? def->plexus : (mvfxPlexusDef){0};
    CompileUnit(&def->unit, out);
    out->contacts = def->contacts.enable ? def->contacts : (mvfxContactDef){0};
}

// Resolves each emitter's neighbor source to an index, and sizes each
// grid's cells by the largest radius that queries it.
static void ResolveNeighbors(const mvfxEffectDef* def, mvfxEffect* effect)
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        effect->emitters[i].gridCell = 0.0f;
        effect->emitters[i].neighbors = (mvfxNeighborDef){0};
    }
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        mvfxNeighborDef neighbors = def->emitters[i].neighbors;
        if (!neighbors.enable)
        {
            continue;
        }
        neighbors.source = neighbors.source < 0 ? i : neighbors.source;
        effect->emitters[i].neighbors = neighbors;
        float* cell = &effect->emitters[neighbors.source].gridCell;
        *cell = neighbors.radius > *cell ? neighbors.radius : *cell;
    }
}

mvfxResult mvfxCheckEffectParts(const mvfxFieldPath* root, const mvfxEffectDef* def)
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        mvfxResult parts = CheckEmitterParts(root, def, i);
        if (parts != mvfx_success)
        {
            return parts;
        }
    }
    return mvfx_success;
}

void mvfxCompileEffectParts(const mvfxEffectDef* def, mvfxEffect* effect)
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        CompileEmitterParts(&def->emitters[i], &effect->emitters[i]);
    }
    ResolveNeighbors(def, effect);
}
