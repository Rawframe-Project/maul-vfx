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
#include "shape.h"
#include "value.h"
#include "velocity.h"

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
};

// How many attributes exist.
#define MVFX_ATTRIBUTE_COUNT (28 + 4 * MVFX_MAX_CUSTOM_ATTRIBUTES)

// The alignment of every attribute array, a cache line.
#define MVFX_ATTRIBUTE_ALIGNMENT 64

// The multiple of particles every array is padded to, so that loops run
// whole vector widths up to sixteen lanes.
#define MVFX_ATTRIBUTE_PADDING 16

// Which attributes an emitter stores, and where each array lies in the
// block of the emitter's particles.
typedef struct mvfxAttributePlan
{
    // Bit a set for each attribute a stored.
    uint64_t present;
    size_t offsets[MVFX_ATTRIBUTE_COUNT];
    size_t blockBytes;
} mvfxAttributePlan;

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
    mvfxOrientationMode orientationMode;
    mvfxCompiledVector orientation;
    mvfxCompiledVector angularVelocity;
    mvfxFrameStart frame;
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
    size_t blockBytes;
    size_t blockAlignment;
    // The instances playing it, which keep it from being destroyed.
    int64_t users;
} mvfxEffect;

// Whether a def and every emitter def in it are valid.
bool mvfxIsEffectDefValid(const mvfxEffectDef* def);

// Compiles a valid def into a block from allocator, each mesh shape's
// emitter i spawning from meshes[i]; NULL when the allocator fails or the
// block's size does not fit in size_t.
mvfxEffect* mvfxCompileEffect(const mvfxEffectDef* def, struct mvfxMesh* const* meshes,
                              const mvfxAllocator* allocator);

// Returns a compiled effect's block.
void mvfxReleaseEffect(mvfxEffect* effect, const mvfxAllocator* allocator);

#endif
