// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// How a space maps to the space an emitter's particles are stored in.

#ifndef MAUL_VFX_SRC_SPACE_H
#define MAUL_VFX_SRC_SPACE_H

#include "force.h"
#include "instance.h"

// How a space maps to the space an emitter's particles are stored in,
// under a transform of the instance: a world-space emitter's are offsets
// from its anchor.
mvfxSpaceMap mvfxSpaceMapAt(const mvfxInstance* instance, int32_t emitter,
                            mvfxSimulationSpace space, const mvfxTransform* transform);

#endif
