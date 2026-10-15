// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gases as the host reads them: their boxes in the world.

#ifndef MAUL_VFX_SRC_GAS_API_H
#define MAUL_VFX_SRC_GAS_API_H

#include "force.h"
#include "instance.h"

#include "maul-vfx/output.h"

// The box in the world around a gas's box now.
mvfxBounds mvfxGasBounds(const mvfxInstance* instance, int32_t gas);

// The map from a space (the world's, or the instance's frame) into a gas
// box's frame now.
mvfxSpaceMap mvfxGasMap(const mvfxInstance* instance, int32_t gas, mvfxSimulationSpace space);

// The map from a gas box's frame now into a space, given the map from the
// world into it.
mvfxSpaceMap mvfxGasToSpace(const mvfxInstance* instance, int32_t gas,
                            const mvfxSpaceMap* fromWorld);

// Points a gas force at its gas's velocity as it is now, and its lattice.
void mvfxGasFollow(const mvfxInstance* instance, int32_t gas, mvfxResolvedForce* force);

// Resolves a gas's obstacles (its own, then with hostObstacles the
// instance's colliders) into its box's frame as the step leaves them,
// keeping the step before's.
void mvfxResolveGasObstacles(mvfxInstance* instance, int32_t gas);

// A gas's gravity in its box's frame now: given there, or the world's
// turned into it and over its scale.
void mvfxGasGravity(const mvfxInstance* instance, int32_t gas, float out[3]);

#endif
