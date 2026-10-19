// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Flocking and steering: accelerations from neighbors, points and a path.

#ifndef MAUL_VFX_SRC_STEER_H
#define MAUL_VFX_SRC_STEER_H

#include "instance.h"

// A steering emitter's targets in the particles' space with the instance
// at a place (its step's start; zeros for one that does not steer).
void mvfxResolveSteering(const mvfxInstance* instance, int32_t emitter, const mvfxTransform* at,
                         mvfxPlannedSteering* targetsOut);

// At an emitter's step, after mvfxFindNeighbors and before motion: for a
// steering emitter or one with a pair force, adds each particle's
// steering and pull over the step to its velocity, from the step's start;
// walks the neighbors (storing their attributes) for one that finds
// them.
void mvfxSteerEmitter(mvfxInstance* instance, int32_t emitter);

#endif
