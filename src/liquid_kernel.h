// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A particle liquid's smoothing kernels (Muller, Charypar and Gross
// 2003): what the liquid's step and a plan's resolved liquid share.

#ifndef MAUL_VFX_SRC_LIQUID_KERNEL_H
#define MAUL_VFX_SRC_LIQUID_KERNEL_H

#include "maul-vfx/effect.h"

// A liquid's kernels at its spacing in the particles' space: the kernel
// radius there, which the grids take; the unit the kernels measure lengths
// in, the power of two that leaves the radius from 1 to 2 in it, and its
// inverse; then, in that unit, the radius and its square, the poly6
// kernel's factor and the spiky kernel's gradient's (3D, or 2D for a
// planar liquid, after Muller, Charypar and Gross 2003), the inverse of
// the rest density, the poly6 sum over an ideal lattice at the spacing,
// the particle itself included; the constraint's softening, the
// relaxation times the lattice's denominator; the inverse of the poly6
// kernel at the tensile correction's reference distance, and its strength
// over the lattice's denominator (so it weighs as a density error of its
// strength would, at any scale). Measured in the unit, every product the
// solver forms stays within binary32 over the radius's whole range, and
// since the unit is a power of two the solver's results are the bits
// lengths in the particles' space would give wherever those stay in range.
typedef struct mvfxLiquidKernel
{
    float radius;
    float unit;
    float inverseUnit;
    float span;
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

// The poly6 kernel at a squared distance in the kernel's unit: 0 at the
// radius and past it.
float mvfxLiquidPoly6(const mvfxLiquidKernel* kernel, float square);

// The spiky kernel's gradient at offset (from the neighbor to the
// particle), its length length, both in the kernel's unit, into out: 0
// at the radius and past it, and at no distance.
void mvfxLiquidSpiky(const mvfxLiquidKernel* kernel, const float offset[3], float length,
                     float out[3]);

#endif
