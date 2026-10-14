// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An effect's compiled form: its emitters with their checked values and
// each emitter's attribute plan, the arrays a particle's data lives in
// (record mvfx-0001: one array per attribute, each 64-byte aligned and
// padded to a multiple of 16 particles, only the attributes the emitter
// uses).

#ifndef MAUL_VFX_SRC_EFFECT_H
#define MAUL_VFX_SRC_EFFECT_H

#include "curve.h"
#include "curve_integral.h"
#include "over_life.h"
#include "shape.h"
#include "value.h"
#include "velocity.h"
#include "velocity_modules.h"

#include "maul-vfx/effect.h"

#include <math.h>

// The attributes a particle may have, each one array of float or uint32_t.
typedef uint8_t mvfxAttribute;
enum
{
    // Position relative to the emitter's anchor, one array per axis.
    mvfx_attributePositionX = 0,
    mvfx_attributePositionY = 1,
    mvfx_attributePositionZ = 2,
    // The tick of its birth on the instance's clock, an int64 in two
    // uint32 arrays (low and high halves): its age is the clock minus it,
    // exact whatever steps came between.
    mvfx_attributeBirthLow = 3,
    mvfx_attributeBirthHigh = 4,
    // One over the particle's life, so that age times it is the normalized
    // age.
    mvfx_attributeInverseLife = 5,
    // The low 32 bits of the count of particles the emitter owed before
    // this one.
    mvfx_attributeSerial = 6,
    // Velocity, one array per axis.
    mvfx_attributeVelocityX = 7,
    mvfx_attributeVelocityY = 8,
    mvfx_attributeVelocityZ = 9,
    // The optional values, each stored only when the emitter sets it.
    mvfx_attributeSizeX = 10,
    mvfx_attributeSizeY = 11,
    mvfx_attributeSizeZ = 12,
    mvfx_attributeColorR = 13,
    mvfx_attributeColorG = 14,
    mvfx_attributeColorB = 15,
    mvfx_attributeColorA = 16,
    mvfx_attributeAngle = 17,
    mvfx_attributeSpin = 18,
    // The orientation's quaternion, x, y, z, w.
    mvfx_attributeOrientationX = 19,
    mvfx_attributeOrientationY = 20,
    mvfx_attributeOrientationZ = 21,
    mvfx_attributeOrientationW = 22,
    mvfx_attributeAngularX = 23,
    mvfx_attributeAngularY = 24,
    mvfx_attributeAngularZ = 25,
    // A uint32 frame index.
    mvfx_attributeFrame = 26,
    // A particle's drag multiplier.
    mvfx_attributeDrag = 27,
    // Custom attribute i's component c at 28 + 4 i + c.
    mvfx_attributeCustom = 28,
    // A uint32 count of a particle's collisions.
    mvfx_attributeCollisions = 60,
    // A particle's contact plane from the host's scene: a point, then a
    // normal (0 for none).
    mvfx_attributeScenePointX = 61,
    mvfx_attributeSceneNormalX = 64,
    // A uint32 index of the host's mesh it is drawn as.
    mvfx_attributeMesh = 67,
    // A uint32 id of the ribbon it joins.
    mvfx_attributeRibbon = 68,
    // The ribbon's distance at its birth in the particles' space, from its
    // ribbon's point before.
    mvfx_attributeRibbonDistance = 69,
    // A particle's count of neighbors, and the offsets to their mean place
    // and velocity.
    mvfx_attributeNeighborCount = 70,
    mvfx_attributeNeighborCenterX = 71,
    mvfx_attributeNeighborVelocityX = 74,
    // A unit's pinned point's place, where it is held: its birth place in
    // the particles' space, or for a unit held to a frame its place in it
    // (the instance's space for mvfx_attachInstance).
    mvfx_attributeAnchorX = 77,
    // A unit's frame, on its first point: its origin in the particles' space
    // and its orientation (a unit quaternion) as its step began, the host's
    // frame or the source's particle (its serial) it follows, and its
    // flags (MVFX_ATTACH_NEW and the others).
    mvfx_attributeAttachX = 80,
    mvfx_attributeAttachRotationX = 83,
    mvfx_attributeAttachSource = 87,
    mvfx_attributeAttachFlags = 88,
};

// How many attributes exist.
#define MVFX_ATTRIBUTE_COUNT (mvfx_attributeAttachFlags + 1)

// The words of an attribute plan's set, and room in them for every
// attribute.
#define MVFX_ATTRIBUTE_WORDS 2
static_assert(MVFX_ATTRIBUTE_COUNT <= 64 * MVFX_ATTRIBUTE_WORDS, "attribute set too narrow");

// The alignment of every attribute array, a cache line.
#define MVFX_ATTRIBUTE_ALIGNMENT 64

// The multiple of particles every array is padded to, so that loops run
// whole vector widths up to sixteen lanes.
#define MVFX_ATTRIBUTE_PADDING 16

// Which attributes an emitter stores, and where each array lies in the
// block of an instance's particles of the emitter: array slot a, each
// array its padded capacity's floats (a multiple of 64 bytes, so every
// array is aligned).
typedef struct mvfxAttributePlan
{
    // Bit a % 64 of word a / 64 set for each attribute a stored.
    uint64_t present[MVFX_ATTRIBUTE_WORDS];
    uint8_t slots[MVFX_ATTRIBUTE_COUNT];
    int32_t arrayCount;
} mvfxAttributePlan;

// The bytes of each of an emitter's arrays at a capacity: padded to a
// multiple of MVFX_ATTRIBUTE_PADDING particles.
static inline size_t mvfxArrayBytes(int32_t capacity)
{
    int32_t padded =
        (capacity + MVFX_ATTRIBUTE_PADDING - 1) / MVFX_ATTRIBUTE_PADDING * MVFX_ATTRIBUTE_PADDING;
    return (size_t)padded * sizeof(float);
}

// Whether a plan stores an attribute.
static inline bool mvfxHasAttribute(const mvfxAttributePlan* plan, int32_t attribute)
{
    return ((plan->present[attribute >> 6] >> (attribute & 63)) & 1u) != 0;
}

// The ticks in a span of seconds, rounded once to the nearest, ties up.
static inline int64_t mvfxSecondsToTicks(double seconds)
{
    return (int64_t)floor(seconds * (double)MVFX_TICKS_PER_SECOND + 0.5);
}

// A rate curve baked over a loop's normalized age, its samples already
// times the emitter's rate, and its integral over [0, 1].
typedef struct mvfxRateCurve
{
    mvfxCurveTable table;
    mvfxCurveIntegral integral;
} mvfxRateCurve;

// A burst in ticks.
typedef struct mvfxCompiledBurst
{
    int64_t time;
    int64_t interval;
    int32_t minCount;
    int32_t maxCount;
    int32_t cycles;
    float probability;
} mvfxCompiledBurst;

// An emitter as an instance runs it.
typedef struct mvfxCompiledEmitter
{
    int32_t capacity;
    // The capacity rounded up to MVFX_ATTRIBUTE_PADDING.
    int32_t paddedCapacity;
    float rate;
    // NULL for a constant rate.
    const mvfxRateCurve* rateCurve;
    const mvfxCompiledBurst* bursts;
    int32_t burstCount;
    // Where its bursts' cursors start in an instance's array of them.
    int32_t firstBurst;
    int64_t delay;
    // 0 without end.
    int64_t duration;
    int32_t loops;
    // The particles the rate owes over one loop, with a duration.
    double loopIntegral;
    mvfxSimulationSpace space;
    float ratePerDistance;
    float inheritVelocity;
    int32_t hostSpawnCapacity;
    mvfxCompiledScalar lifetime;
    mvfxCompiledShape shape;
    mvfxCompiledVelocity velocity;
    mvfxCompiledVector size;
    mvfxCompiledVector color;
    mvfxCompiledScalar angle;
    mvfxCompiledScalar spin;
    // How particles move; dragScale is drawn when the drag attribute is
    // stored.
    float gravity[3];
    mvfxSimulationSpace gravitySpace;
    float drag;
    float angularDrag;
    mvfxCompiledScalar dragScale;
    // Its forces, compiled.
    mvfxForce forces[MVFX_MAX_FORCES];
    int32_t forceCount;
    // The grid each vector field reads, which the effect counts a user of.
    struct mvfxGrid* forceGrids[MVFX_MAX_FORCES];
    mvfxCompiledVelocityModules velocityModules;
    mvfxCompiledOverLife overLife;
    mvfxKillRule killRules[MVFX_MAX_KILL_RULES];
    int32_t killRuleCount;
    mvfxHostModule hostModules[MVFX_MAX_HOST_MODULES];
    int32_t hostModuleCount;
    uint8_t events;
    int32_t eventCapacity;
    mvfxEventSpawn eventSpawn;
    // Its neighbor queries, the source resolved to an index; and the cell of
    // its own grid, the largest radius querying it (0 for no grid).
    mvfxNeighborDef neighbors;
    float gridCell;
    // Its steering, the path's points copied.
    mvfxSteeringDef steering;
    float pathPoints[MVFX_MAX_PATH_POINTS][3];
    mvfxContactDef contacts;
    mvfxPlexusDef plexus;
    // Its units: the def, the directions normalized (what its kind does not
    // read cleared); its points, 1 for a particle; its points across, a
    // chain's all of them; its rest lengths before the instance's scale,
    // across, down, along the diagonal across and down and along the other
    // (a sheet's last three).
    mvfxUnitDef unit;
    int32_t unitPoints;
    int32_t unitWidth;
    float unitRests[4];
    // Its pair force: the radius (0 for none) and the strength over the
    // distance.
    float pairRadius;
    mvfxCompiledLife pairStrength;
    int32_t drawPriority;
    bool fixedBounds;
    float boundsMin[3];
    float boundsMax[3];
    mvfxCollision collision;
    mvfxCollider colliders[MVFX_MAX_COLLIDERS];
    int32_t colliderCount;
    // The grids of its heightfields and distance grids (NULL for the
    // primitives), held while the effect lives.
    struct mvfxGrid* colliderGrids[MVFX_MAX_COLLIDERS];
    mvfxOrientationMode orientationMode;
    mvfxCompiledVector orientation;
    mvfxCompiledVector angularVelocity;
    mvfxFrameStart frame;
    mvfxFrameStart meshIndex;
    mvfxLightDef light;
    mvfxSortMode sortMode;
    mvfxSortMeasure sortMeasure;
    mvfxRibbonIds ribbonIds;
    int32_t ribbonPieces;
    // The cosine of the turn k + 1 pieces allow a segment, -2 for any.
    float ribbonCosLimits[MVFX_MAX_RIBBON_PIECES];
    mvfxRibbonTexture ribbonTexture;
    float ribbonTileLength;
    float ribbonScroll;
    mvfxRibbonFacing ribbonFacing;
    float ribbonBreakDistance;
    bool ribbonPinHead;
    mvfxBeamDef beam;
    mvfxSpriteDef sprite;
    float ribbonNormal[3];
    mvfxCompiledLife ribbonWidth;
    // NULL for white.
    const mvfxGradientTable* ribbonColor;
    mvfxCompiledVector custom[MVFX_MAX_CUSTOM_ATTRIBUTES];
    int32_t customCount;
    mvfxAttributePlan plan;
} mvfxCompiledEmitter;

// A compiled effect, in one block: this header, its emitters, their rate
// curves and their bursts.
typedef struct mvfxEffect
{
    mvfxCompiledEmitter* emitters;
    int32_t emitterCount;
    // The bursts of every emitter.
    int32_t burstCount;
    // Its parameters' defaults.
    float parameters[MVFX_MAX_PARAMETERS][4];
    int32_t parameterCount;
    // Its quality levels, level 0 the full one; levels from 1 as given.
    mvfxQualityLevel qualityLevels[MVFX_MAX_QUALITY_LEVELS + 1];
    int32_t qualityLevelCount;
    size_t blockBytes;
    size_t blockAlignment;
    // The instances playing it, which keep it from being destroyed.
    int64_t users;
    // The order its emitters step in: each source before the emitters
    // spawning from its events, otherwise by index.
    int32_t stepOrder[MVFX_MAX_EMITTERS];
} mvfxEffect;

// Whether a def and every emitter def in it are valid.
bool mvfxIsEffectDefValid(const mvfxEffectDef* def);

// Compiles a valid def into a block from allocator, each mesh shape's
// emitter i spawning from meshes[i]; NULL when the allocator fails or the
// block's size does not fit in size_t.
mvfxEffect* mvfxCompileEffect(const mvfxEffectDef* def, struct mvfxMesh* const* meshes,
                              const mvfxAllocator* allocator);

// Whether a unit's point (its index in its unit) is pinned: a pinned first
// point, or one of a pinned first row.
static inline bool mvfxIsPinnedPoint(const mvfxCompiledEmitter* emitter, int32_t point)
{
    return (emitter->unit.pinFirst && point == 0) ||
           (emitter->unit.pinFirstRow && point < emitter->unitWidth);
}

// Returns a compiled effect's block.
void mvfxReleaseEffect(mvfxEffect* effect, const mvfxAllocator* allocator);

#endif
