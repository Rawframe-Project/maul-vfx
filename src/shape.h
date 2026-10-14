// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Spawn shapes checked, compiled and sampled: a point uniform over the
// shape by the inverse-transform constructions (a disc's radius by a
// square root, a sphere's by a cube root, a cap's direction by a uniform
// cosine, a box's shell by slabs chosen by volume), then turned and moved
// into the emitter's space. Every draw comes from pcg4d words the caller
// takes at the particle's serial.

#ifndef MAUL_VFX_SRC_SHAPE_H
#define MAUL_VFX_SRC_SHAPE_H

#include "direction.h"
#include "random.h"

#include "maul-vfx/shape.h"

// A shape as the step samples it.
typedef struct mvfxCompiledShape
{
    mvfxShapeKind kind;
    mvfxArcMode arcMode;
    bool coneVolume;
    // The rotation as a row-major matrix, and the offset.
    float matrix[9];
    float offset[3];
    float radius;
    float length;
    float halfExtents[3];
    float tangent;
    float thickness;
    float arc;
    float arcSpeed;
    mvfxMeshMode meshMode;
    // A mesh shape's mesh, for instances that bind no other.
    struct mvfxMesh* mesh;
} mvfxCompiledShape;

// Where a particle is born and the shape's normal there, in the emitter's
// space.
typedef struct mvfxShapeSample
{
    float position[3];
    float normal[3];
    // The position before the shape's offset, for directions from its
    // center.
    float local[3];
} mvfxShapeSample;

// Whether a shape is valid (its kind and arc mode known, the fields its
// kind reads in range).
bool mvfxIsShapeValid(const mvfxShape* shape);

// Compiles a valid shape: its rotation as a normalized matrix, its cone's
// tangent, a mesh shape's mesh.
void mvfxCompileShape(const mvfxShape* shape, struct mvfxMesh* mesh, mvfxCompiledShape* out);

// The fraction of the arc a particle is born at: from words for the
// random mode, from its age in seconds since the emitter began (its
// birth less the start delay) for loop and ping-pong, from its place
// among a burst's particles for burst spread.
float mvfxArcFraction(const mvfxCompiledShape* shape, mvfxWords4 words, double age,
                      int64_t batchIndex, int64_t batchCount);

// Samples the shape at an arc fraction with four random words; a mesh
// shape samples mesh.
void mvfxSampleShape(const mvfxCompiledShape* shape, const struct mvfxMesh* mesh, mvfxWords4 words,
                     float arcFraction, mvfxShapeSample* out);

#endif
