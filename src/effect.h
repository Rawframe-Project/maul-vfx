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

#include "maul-vfx/effect.h"

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
};

// How many attributes exist.
#define MVFX_ATTRIBUTE_COUNT 7

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
    uint32_t present;
    size_t offsets[MVFX_ATTRIBUTE_COUNT];
    size_t blockBytes;
} mvfxAttributePlan;

// An emitter as an instance runs it.
typedef struct mvfxCompiledEmitter
{
    int32_t capacity;
    // The capacity rounded up to MVFX_ATTRIBUTE_PADDING.
    int32_t paddedCapacity;
    float rate;
    mvfxRange lifetime;
    mvfxAttributePlan plan;
} mvfxCompiledEmitter;

// A compiled effect, in one block: this header and its emitters after it.
typedef struct mvfxEffect
{
    mvfxCompiledEmitter* emitters;
    int32_t emitterCount;
    size_t blockBytes;
    size_t blockAlignment;
    // The instances playing it, which keep it from being destroyed.
    int64_t users;
} mvfxEffect;

// Whether a def and every emitter def in it are valid.
bool mvfxIsEffectDefValid(const mvfxEffectDef* def);

// Compiles a valid def into a block from allocator; NULL when the
// allocator fails or the block's size does not fit in size_t.
mvfxEffect* mvfxCompileEffect(const mvfxEffectDef* def, const mvfxAllocator* allocator);

// Returns a compiled effect's block.
void mvfxReleaseEffect(mvfxEffect* effect, const mvfxAllocator* allocator);

#endif
