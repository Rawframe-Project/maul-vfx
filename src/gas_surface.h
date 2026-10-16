// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A surface gas's step: the signed distance to its emitter's particles'
// surface.

#ifndef MAUL_VFX_SRC_GAS_SURFACE_H
#define MAUL_VFX_SRC_GAS_SURFACE_H

#include "instance.h"
#include "tasks.h"

// Makes surface gas g of an instance anew from its emitter's particles as
// the step left them: each cell's distance gathered from the particles
// within its reach, then smoothed; the passes split over tasks.
void mvfxStepSurfaceGas(mvfxInstance* instance, int32_t g, const mvfxTasks* tasks);

// The largest eigenvalue of a symmetric matrix (XX, YY, ZZ, XY, XZ, YZ;
// for axes 2 its XY block alone), or, when it is below low, a bound on it
// below low (Gershgorin's): in 2D in closed form, in 3D by cyclic Jacobi
// rotations, accurate where eigenvalues repeat (a polynomial's repeated
// root is not).
float mvfxSurfaceEigenvalue(const float m[6], int32_t axes, float low);

#endif
