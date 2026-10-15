// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particles pouring into a gas: each live particle's density, temperature
// and fuel spread over the 8 cells nearest it, and its pull on the 8
// faces across each axis nearest it, by trilinear weights, in the
// particles' order (a serial scatter, the same sums a gather visiting the
// particles in their order makes).

#ifndef MAUL_VFX_SRC_GAS_SPLAT_H
#define MAUL_VFX_SRC_GAS_SPLAT_H

#include "force.h"
#include "gas.h"

// An emitter's particles as a gas reads them: their places and
// velocities, the map from their space into the gas's box, and the splat.
typedef struct mvfxGasPour
{
    const float* position[3];
    const float* velocity[3];
    int32_t count;
    mvfxSpaceMap map;
    const mvfxGasSplat* splat;
} mvfxGasPour;

// Pours the particles into a gas's current buffers over a substep.
void mvfxGasSplatInto(const mvfxGasLattice* l, const mvfxGasPour* pour, float* const velocity[3],
                      float* const scalars[3]);

#endif
