// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effect defs compiled, once effect_check.c has checked them: each
// emitter's times in ticks, its rate curve baked with its integral, its
// bursts, and its attribute plan laid out.

#include "effect.h"

#include "allocator.h"
#include "collide.h"
#include "defaults.h"
#include "effect_gas.h"
#include "elementary.h"
#include "force.h"
#include "hull.h"
#include "kill.h"

#include <math.h>

mvfxEmitterDef mvfxDefaultEmitterDef(void)
{
    return (mvfxEmitterDef)MVFX_DEFAULT_EMITTER_DEF;
}

mvfxEffectDef mvfxDefaultEffectDef(void)
{
    return (mvfxEffectDef)MVFX_DEFAULT_EFFECT_DEF;
}

void mvfxBakeRateCurve(const mvfxCurve* curve, float rate, mvfxRateCurve* out)
{
    mvfxBakeCurve(curve, 0.0f, 1.0f, &out->table);
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        out->table.samples[i] *= rate;
    }
    mvfxIntegrateCurve(&out->table, 1.0, &out->integral);
}

// Compiles a ribbon's tessellation, texture and facing: for a bend angle,
// the cosine each count of pieces allows a segment's turn down to.
static void CompileRibbonShape(const mvfxRibbonDef* ribbon, mvfxCompiledEmitter* out)
{
    out->ribbonPieces = ribbon->pieces;
    // A ribbon's fields are checked only when it is drawn: an emitter
    // without one bends nothing, whatever its bend angle.
    bool bends = ribbon->ids != mvfx_ribbonNone && ribbon->bendAngle > 0.0f;
    for (int32_t k = 1; k <= MVFX_MAX_RIBBON_PIECES; ++k)
    {
        float turn = bends ? fminf(ribbon->bendAngle * (float)k, 3.14159265f) : 0.0f;
        float sine = 0.0f;
        float cosine = 1.0f;
        mvfxSinCos(turn, &sine, &cosine);
        out->ribbonCosLimits[k - 1] = bends ? cosine : -2.0f;
    }
    out->ribbonTexture = ribbon->texture;
    out->ribbonTileLength = ribbon->tileLength;
    out->ribbonScroll = ribbon->scroll;
    out->ribbonFacing = ribbon->facing;
    out->ribbonBreakDistance = ribbon->breakDistance;
    out->ribbonPinHead = ribbon->pinHead;
    for (int32_t k = 0; k < 3; ++k)
    {
        out->ribbonNormal[k] = ribbon->normal[k];
    }
}

// Whether a gradient has no keys, so is white.
static bool IsWhite(const mvfxGradient* gradient)
{
    return gradient->colorKeyCount == 0 && gradient->alphaKeyCount == 0;
}

void mvfxAddParameterReach(const mvfxEmitterDef* def, double reach[MVFX_MAX_PARAMETERS][4])
{
    mvfxSourceCaps caps;
    mvfxSourceCapsOf(&def->overLife, def->custom, def->customCount, &caps);
    const mvfxScalarSource* scalars[4] = {&def->lifetime, &def->angle, &def->spin,
                                          &def->motion.dragScale};
    const float scalarCaps[4] = {MVFX_MAX_DRAW, MVFX_MAX_DRAW, caps.stored.spin, MVFX_MAX_DRAW};
    const mvfxVectorSource* vectors[4] = {&def->size, &def->color, &def->orientation,
                                          &def->angularVelocity};
    const float* vectorCaps[4] = {caps.stored.size, caps.stored.color, caps.any, caps.spin};
    static const int32_t components[4] = {3, 4, 3, 3};
    for (int32_t s = 0; s < 4; ++s)
    {
        mvfxScalarParameterReach(scalars[s], scalarCaps[s], reach);
        mvfxVectorParameterReach(vectors[s], components[s], vectorCaps[s], reach);
    }
    for (int32_t i = 0; i < def->customCount; ++i)
    {
        mvfxVectorParameterReach(&def->custom[i].initial, def->custom[i].components, caps.custom[i],
                                 reach);
    }
}

// Whether a source is an emitter of the effect already placed: one out of
// range never is, so its def is refused here, before any check of it.
static bool IsPlaced(const bool placed[MVFX_MAX_EMITTERS], int32_t source, int32_t count)
{
    return source >= 0 && source < count && placed[source];
}

bool mvfxOrderEmitters(const mvfxEffectDef* def, int32_t order[MVFX_MAX_EMITTERS])
{
    bool placed[MVFX_MAX_EMITTERS] = {false};
    for (int32_t n = 0; n < def->emitterCount; ++n)
    {
        int32_t next = -1;
        for (int32_t i = 0; next < 0 && i < def->emitterCount; ++i)
        {
            const mvfxEventSpawn* spawn = &def->emitters[i].eventSpawn;
            const mvfxNeighborDef* neighbors = &def->emitters[i].neighbors;
            bool ready = (!spawn->enable || IsPlaced(placed, spawn->source, def->emitterCount)) &&
                         (!neighbors->enable || neighbors->source == -1 || neighbors->source == i ||
                          IsPlaced(placed, neighbors->source, def->emitterCount));
            next = !placed[i] && ready ? i : next;
        }
        if (next < 0)
        {
            return false;
        }
        placed[next] = true;
        order[n] = next;
    }
    return true;
}

// Adds count attributes from first to a plan when a condition holds.
static void Store(mvfxAttributePlan* plan, bool when, int32_t first, int32_t count)
{
    for (int32_t a = first; when && a < first + count; ++a)
    {
        plan->present[a >> 6] |= (uint64_t)1 << (a & 63);
    }
}

// The attributes an emitter stores: position, birth tick, inverse life,
// serial and velocity always; each optional value when its source is set;
// each custom attribute's components.
static void StoreAttributes(const mvfxEmitterDef* def, mvfxAttributePlan* plan)
{
    Store(plan, true, 0, mvfx_attributeVelocityZ + 1);
    Store(plan, def->size.kind != mvfx_sourceNone, mvfx_attributeSizeX, 3);
    Store(plan, def->color.kind != mvfx_sourceNone, mvfx_attributeColorR, 4);
    bool sprite = def->angle.kind != mvfx_sourceNone || def->spin.kind != mvfx_sourceNone;
    Store(plan, sprite, mvfx_attributeAngle, 2);
    bool mesh = def->orientationMode != mvfx_orientationNone ||
                def->angularVelocity.kind != mvfx_sourceNone || def->rigid.enable;
    Store(plan, mesh, mvfx_attributeOrientationX, 7);
    Store(plan, def->frame.mode != mvfx_frameNone, mvfx_attributeFrame, 1);
    Store(plan, def->meshIndex.mode != mvfx_frameNone, mvfx_attributeMesh, 1);
    Store(plan, def->ribbon.ids != mvfx_ribbonNone, mvfx_attributeRibbon, 1);
    Store(plan,
          def->ribbon.ids != mvfx_ribbonNone && def->ribbon.texture == mvfx_ribbonTileByDistance,
          mvfx_attributeRibbonDistance, 1);
    Store(plan, def->motion.dragScale.kind != mvfx_sourceNone, mvfx_attributeDrag, 1);
    Store(plan, def->collision.enable, mvfx_attributeCollisions, 1);
    Store(plan, def->neighbors.enable, mvfx_attributeNeighborCount, 7);
    bool pinned =
        def->unit.kind != mvfx_unitParticle &&
        (def->unit.pinFirst || (def->unit.kind == mvfx_unitSheet && def->unit.pinFirstRow));
    Store(plan, pinned, mvfx_attributeAnchorX, 3);
    mvfxAttachKind attach =
        def->unit.kind != mvfx_unitParticle ? def->unit.attach : mvfx_attachNone;
    bool framed = attach == mvfx_attachHost || attach == mvfx_attachParticles;
    Store(plan, framed, mvfx_attributeAttachX, 3);
    Store(plan, framed, mvfx_attributeAttachRotationX, 4);
    Store(plan, framed, mvfx_attributeAttachSource, 1);
    Store(plan, attach != mvfx_attachNone, mvfx_attributeAttachFlags, 1);
    Store(plan, def->unit.kind != mvfx_unitParticle && def->unit.tearStrain > 0.0f,
          mvfx_attributeTorn, 1);
    Store(plan, def->collision.enable && def->collision.enableSceneQueries,
          mvfx_attributeScenePointX, 6);
    Store(plan, def->rigid.enable && def->rigid.sleepSpeed > 0.0f, mvfx_attributeSleep, 4);
    Store(plan, def->liquid.enable && def->liquid.solver == mvfx_liquidGrid, mvfx_attributeAffine,
          9);
    for (int32_t i = 0; i < def->customCount; ++i)
    {
        Store(plan, true, mvfx_attributeCustom + 4 * i, def->custom[i].components);
    }
}

// The plan of an emitter's attributes: a slot for each it stores.
static mvfxAttributePlan PlanAttributes(const mvfxEmitterDef* def)
{
    mvfxAttributePlan plan = {0};
    StoreAttributes(def, &plan);
    for (int32_t a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
    {
        if (mvfxHasAttribute(&plan, a))
        {
            plan.slots[a] = (uint8_t)plan.arrayCount++;
        }
    }
    return plan;
}

// The curve and gradient tables an emitter's values need.
static void CountTables(const mvfxEmitterDef* def, int32_t* curves, int32_t* gradients)
{
    const mvfxVectorSource* vectors[4] = {&def->size, &def->color, &def->orientation,
                                          &def->angularVelocity};
    *curves += mvfxScalarCurves(&def->lifetime) + mvfxScalarCurves(&def->angle) +
               mvfxScalarCurves(&def->spin) + mvfxScalarCurves(&def->motion.dragScale) +
               mvfxVelocityModuleCurves(&def->velocityModules);
    for (int32_t v = 0; v < 4; ++v)
    {
        *curves += mvfxVectorCurves(vectors[v]);
        *gradients += mvfxVectorGradients(vectors[v]);
    }
    for (int32_t i = 0; i < def->customCount; ++i)
    {
        *curves += mvfxVectorCurves(&def->custom[i].initial);
        *gradients += mvfxVectorGradients(&def->custom[i].initial);
    }
    mvfxOverLifeTables(&def->overLife, def->custom, def->customCount, curves, gradients);
    *curves += mvfxLifeCurves(&def->ribbon.width);
    *curves += def->pairForce.enable ? mvfxLifeCurves(&def->pairForce.strength) : 0;
    *gradients += IsWhite(&def->ribbon.color) ? 0 : 1;
    for (int32_t f = 0; f < def->forceCount; ++f)
    {
        *curves += mvfxScalarCurves(&def->forces[f].strength);
    }
}

// Compiles an emitter's pair force, its strength's table into space.
static void CompilePairForce(const mvfxPairForceDef* pair, mvfxTableSpace* space,
                             mvfxCompiledEmitter* out)
{
    out->pairRadius = pair->enable ? pair->radius : 0.0f;
    out->pairStrength = (mvfxCompiledLife){0};
    if (pair->enable)
    {
        mvfxCompileLife(&pair->strength, space, &out->pairStrength);
    }
}

// Compiles an emitter's initial values into out, its tables into space.
static void CompileValues(const mvfxEmitterDef* def, mvfxTableSpace* space,
                          mvfxCompiledEmitter* out)
{
    bool d = def->duration > 0.0f;
    mvfxCompileScalar(&def->lifetime, d, space, &out->lifetime);
    mvfxCompileVector(&def->size, 3, d, space, &out->size);
    mvfxCompileVector(&def->color, 4, d, space, &out->color);
    mvfxCompileScalar(&def->angle, d, space, &out->angle);
    mvfxCompileScalar(&def->spin, d, space, &out->spin);
    mvfxCompileScalar(&def->motion.dragScale, d, space, &out->dragScale);
    for (int32_t k = 0; k < 3; ++k)
    {
        out->gravity[k] = def->motion.gravity[k];
    }
    out->gravitySpace = def->motion.gravitySpace;
    out->drag = def->motion.drag;
    out->angularDrag = def->motion.angularDrag;
    mvfxCompileVelocityModules(&def->velocityModules, space, &out->velocityModules);
    mvfxCompileOverLife(&def->overLife, def->custom, def->customCount, space, &out->overLife);
    out->events = def->events;
    out->eventSpawn = def->eventSpawn;
    out->drawPriority = def->drawPriority;
    out->fixedBounds = def->fixedBounds;
    for (int32_t k = 0; k < 3; ++k)
    {
        out->boundsMin[k] = def->boundsMin[k];
        out->boundsMax[k] = def->boundsMax[k];
    }
    out->eventCapacity = def->events != 0 ? def->eventCapacity : 0;
    out->collision = def->collision;
    out->colliderCount = def->colliderCount;
    for (int32_t c = 0; c < def->colliderCount; ++c)
    {
        out->colliders[c] = mvfxCompileCollider(&def->colliders[c]);
    }
    out->hostModuleCount = def->hostModuleCount;
    for (int32_t m = 0; m < def->hostModuleCount; ++m)
    {
        out->hostModules[m] = def->hostModules[m];
    }
    out->killRuleCount = def->killRuleCount;
    for (int32_t r = 0; r < def->killRuleCount; ++r)
    {
        out->killRules[r] = mvfxCompileKillRule(&def->killRules[r]);
    }
    out->gasSplat = def->gasSplat;
    out->water = def->water;
    out->forceCount = def->forceCount;
    for (int32_t f = 0; f < def->forceCount; ++f)
    {
        out->forces[f] = mvfxCompileForce(&def->forces[f]);
        mvfxCompileScalar(&def->forces[f].strength, d, space, &out->forceStrengths[f]);
    }
    out->orientationMode = def->orientationMode;
    mvfxCompileVector(&def->orientation, 3, d, space, &out->orientation);
    mvfxCompileVector(&def->angularVelocity, 3, d, space, &out->angularVelocity);
    out->frame = def->frame;
    out->meshIndex = def->meshIndex;
    out->light = def->light;
    out->sortMode = def->sortMode;
    out->sortMeasure = def->sortMeasure;
    out->ribbonIds = def->ribbon.ids;
    out->beam = def->beam;
    out->sprite = def->sprite;
    CompileRibbonShape(&def->ribbon, out);
    mvfxCompileLife(&def->ribbon.width, space, &out->ribbonWidth);
    CompilePairForce(&def->pairForce, space, out);
    out->ribbonColor = nullptr;
    if (!IsWhite(&def->ribbon.color))
    {
        mvfxGradientTable* table = &space->gradients[space->nextGradient];
        space->nextGradient += 1;
        mvfxBakeGradient(&def->ribbon.color, table);
        out->ribbonColor = table;
    }
    out->customCount = def->customCount;
    for (int32_t i = 0; i < def->customCount; ++i)
    {
        mvfxCompileVector(&def->custom[i].initial, def->custom[i].components, d, space,
                          &out->custom[i]);
    }
}

static mvfxCompiledBurst CompileBurst(const mvfxBurst* burst)
{
    return (mvfxCompiledBurst){
        .time = mvfxSecondsToTicks((double)burst->time),
        .interval = burst->cycles == 1 ? 0 : mvfxSecondsToTicks((double)burst->interval),
        .minCount = burst->minCount,
        .maxCount = burst->maxCount,
        .cycles = burst->cycles,
        .probability = burst->probability,
    };
}

// Where emitters' compiled parts go in an effect's block, and the next
// free place of each.
typedef struct mvfxEmitterSpace
{
    mvfxTableSpace values;
    mvfxRateCurve* curves;
    mvfxCompiledBurst* bursts;
    int32_t nextCurve;
    int32_t nextBurst;
} mvfxEmitterSpace;

// Compiles one emitter, its curves, bursts and tables into space.
static void CompileEmitter(const mvfxEmitterDef* source, struct mvfxMesh* mesh,
                           mvfxEmitterSpace* space, mvfxCompiledEmitter* out)
{
    const mvfxRateCurve* curve = nullptr;
    if (source->rateCurve.keyCount > 0)
    {
        mvfxBakeRateCurve(&source->rateCurve, source->rate, &space->curves[space->nextCurve]);
        curve = &space->curves[space->nextCurve++];
    }
    mvfxCompiledBurst* bursts = &space->bursts[space->nextBurst];
    for (int32_t b = 0; b < source->burstCount; ++b)
    {
        bursts[b] = CompileBurst(&source->bursts[b]);
    }
    *out = (mvfxCompiledEmitter){
        .capacity = source->capacity,
        // A particle a birth until mvfxCompileEffectParts says otherwise.
        .unitPoints = 1,
        .unitWidth = 1,
        .rate = source->rate,
        .rateCurve = curve,
        .bursts = bursts,
        .burstCount = source->burstCount,
        .firstBurst = space->nextBurst,
        .delay = mvfxSecondsToTicks((double)source->startDelay),
        .duration = mvfxSecondsToTicks((double)source->duration),
        .loops = source->duration > 0.0f ? source->loops : 0,
        .plan = PlanAttributes(source),
        .space = source->space,
        .ratePerDistance = source->ratePerDistance,
        .inheritVelocity = source->inheritVelocity,
        .hostSpawnCapacity = source->hostSpawnCapacity,
    };
    space->nextBurst += source->burstCount;
    CompileValues(source, &space->values, out);
    mvfxCompileShape(&source->shape, mesh, &out->shape);
    mvfxCompileVelocity(&source->velocity, &out->velocity);
    double seconds = (double)out->duration / (double)MVFX_TICKS_PER_SECOND;
    out->loopIntegral = curve != nullptr
                            ? seconds * mvfxCurveIntegralAt(&curve->table, &curve->integral, 1.0)
                            : seconds * (double)source->rate;
}

// Compiles an effect's gases and water surfaces into the room its block
// holds for them, their forces' strengths' tables into space.
static void CompileFields(const mvfxEffectDef* def, mvfxTableSpace* space, mvfxEffect* effect)
{
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        effect->gases[g] = mvfxCompileGas(&def->gases[g]);
        // A gas has no duration: its forces' curves are over seconds.
        for (int32_t f = 0; f < def->gases[g].forceCount; ++f)
        {
            mvfxCompileScalar(&def->gases[g].forces[f].strength, false, space,
                              &effect->gases[g].forceStrengths[f]);
        }
        bool surface = def->gases[g].kind == mvfx_gasSurface;
        effect->gases[g].emitter = surface ? def->gases[g].surface.emitter : mvfxLiquidOf(def, g);
    }
    effect->gasCount = def->gasCount;
    for (int32_t w = 0; w < def->waterCount; ++w)
    {
        effect->waters[w] = mvfxCompileWater(&def->waters[w]);
    }
    effect->waterCount = def->waterCount;
}

// The rate curves, bursts and value tables an effect's block holds.
static void CountEffect(const mvfxEffectDef* def, int32_t* curveCount, int32_t* burstCount,
                        int32_t* valueCurves, int32_t* valueGradients)
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        *curveCount += def->emitters[i].rateCurve.keyCount > 0 ? 1 : 0;
        *burstCount += def->emitters[i].burstCount;
        CountTables(&def->emitters[i], valueCurves, valueGradients);
    }
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        for (int32_t f = 0; f < def->gases[g].forceCount; ++f)
        {
            *valueCurves += mvfxScalarCurves(&def->gases[g].forces[f].strength);
        }
    }
}

mvfxEffect* mvfxCompileEffect(const mvfxEffectDef* def, struct mvfxMesh* const* meshes,
                              const mvfxAllocator* allocator)
{
    int32_t curveCount = 0;
    int32_t burstCount = 0;
    int32_t valueCurves = 0;
    int32_t valueGradients = 0;
    CountEffect(def, &curveCount, &burstCount, &valueCurves, &valueGradients);
    mvfxLayout layout = {0};
    (void)mvfxLayoutAdd(&layout, 1, sizeof(mvfxEffect), alignof(mvfxEffect));
    size_t emittersAt = mvfxLayoutAdd(&layout, (size_t)def->emitterCount,
                                      sizeof(mvfxCompiledEmitter), alignof(mvfxCompiledEmitter));
    size_t curvesAt =
        mvfxLayoutAdd(&layout, (size_t)curveCount, sizeof(mvfxRateCurve), alignof(mvfxRateCurve));
    size_t burstsAt = mvfxLayoutAdd(&layout, (size_t)burstCount, sizeof(mvfxCompiledBurst),
                                    alignof(mvfxCompiledBurst));
    size_t valueCurvesAt = mvfxLayoutAdd(&layout, (size_t)valueCurves, sizeof(mvfxCurveTable),
                                         alignof(mvfxCurveTable));
    size_t valueGradientsAt = mvfxLayoutAdd(&layout, (size_t)valueGradients,
                                            sizeof(mvfxGradientTable), alignof(mvfxGradientTable));
    size_t gasesAt = mvfxLayoutAdd(&layout, (size_t)def->gasCount, sizeof(mvfxCompiledGas),
                                   alignof(mvfxCompiledGas));
    size_t watersAt = mvfxLayoutAdd(&layout, (size_t)def->waterCount, sizeof(mvfxCompiledWater),
                                    alignof(mvfxCompiledWater));
    if (layout.overflow)
    {
        return nullptr;
    }
    unsigned char* block = mvfxAllocate(allocator, layout.size, layout.alignment);
    if (block == nullptr)
    {
        return nullptr;
    }
    mvfxEffect* effect = (mvfxEffect*)block;
    *effect = (mvfxEffect){
        .emitters = (mvfxCompiledEmitter*)(block + emittersAt),
        .emitterCount = def->emitterCount,
        .burstCount = burstCount,
        .gases = (mvfxCompiledGas*)(block + gasesAt),
        .waters = (mvfxCompiledWater*)(block + watersAt),
        .blockBytes = layout.size,
        .blockAlignment = layout.alignment,
    };
    effect->parameterCount = def->parameterCount;
    effect->qualityLevels[0] = (mvfxQualityLevel){.capacityScale = 1.0f, .rateScale = 1.0f};
    for (int32_t q = 0; q < def->qualityLevelCount; ++q)
    {
        effect->qualityLevels[q + 1] = def->qualityLevels[q];
    }
    effect->qualityLevelCount = def->qualityLevelCount + 1;
    for (int32_t p = 0; p < def->parameterCount; ++p)
    {
        for (int32_t c = 0; c < 4; ++c)
        {
            effect->parameters[p][c] = def->parameters[4 * p + c];
        }
    }
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        mvfxAddParameterReach(&def->emitters[i], effect->parameterReach);
    }
    mvfxEmitterSpace emitterSpace = {
        .values =
            {
                .curves = (mvfxCurveTable*)(block + valueCurvesAt),
                .gradients = (mvfxGradientTable*)(block + valueGradientsAt),
            },
        .curves = (mvfxRateCurve*)(block + curvesAt),
        .bursts = (mvfxCompiledBurst*)(block + burstsAt),
    };
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        CompileEmitter(&def->emitters[i], meshes[i], &emitterSpace, &effect->emitters[i]);
    }
    (void)mvfxOrderEmitters(def, effect->stepOrder);
    CompileFields(def, &emitterSpace.values, effect);
    return effect;
}

float mvfxSourceAge(const mvfxCompiledEmitter* emitter, int64_t tick)
{
    int64_t since = tick - emitter->delay;
    if (emitter->duration > 0)
    {
        // A birth before the delay ends (a host's spawn) reads the loop's
        // start.
        since = since > 0 ? since : 0;
        return (float)((double)(since % emitter->duration) / (double)emitter->duration);
    }
    return (float)((double)since / (double)MVFX_TICKS_PER_SECOND);
}

void mvfxReleaseEffect(mvfxEffect* effect, const mvfxAllocator* allocator)
{
    for (int32_t i = 0; i < effect->emitterCount; ++i)
    {
        mvfxRelease(allocator, effect->emitters[i].hull, sizeof(mvfxHull), alignof(mvfxHull));
    }
    mvfxRelease(allocator, effect, effect->blockBytes, effect->blockAlignment);
}
