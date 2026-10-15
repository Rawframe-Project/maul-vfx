// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Liquids by position-based fluids (Macklin and Muller 2013): an emitter's
// particles kept at their rest density by Jacobi iterations on their
// places, each a gather over the neighbors in a grid's order.

#ifndef MAUL_VFX_SRC_LIQUID_H
#define MAUL_VFX_SRC_LIQUID_H

#include "instance.h"

// A liquid's kernels at its spacing in the particles' space: the kernel
// radius and its square; the poly6 kernel's factor and the spiky kernel's
// gradient's (3D, or 2D for a planar liquid, after Muller, Charypar and
// Gross 2003); the inverse of the rest density, the poly6 sum over an
// ideal lattice at the spacing, the particle itself included; the
// constraint's softening, the relaxation times the lattice's denominator;
// the inverse of the poly6 kernel at the tensile correction's reference
// distance, and its strength over the lattice's denominator (so it weighs
// as a density error of its strength would, at any scale).
typedef struct mvfxLiquidKernel
{
    float radius;
    float square;
    float poly6;
    float spiky;
    float restInverse;
    float softening;
    float tensileInverse;
    float tensileScale;
} mvfxLiquidKernel;

// The kernels of a liquid whose lengths scale by scale.
mvfxLiquidKernel mvfxLiquidKernelOf(const mvfxLiquidDef* def, float scale);

// The poly6 kernel at a squared distance: 0 at the radius and past it.
float mvfxLiquidPoly6(const mvfxLiquidKernel* kernel, float square);

// The spiky kernel's gradient at offset (from the neighbor to the
// particle), its length length, into out: 0 at the radius and past it,
// and at no distance.
void mvfxLiquidSpiky(const mvfxLiquidKernel* kernel, const float offset[3], float length,
                     float out[3]);

// Moves an emitter's particles alive at the step's start as its liquid,
// in its substeps.
void mvfxStepLiquid(mvfxInstance* instance, int32_t emitter);

#endif
