// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host modules: their runs at a step's stages.

#ifndef MAUL_VFX_SRC_HOST_MODULE_H
#define MAUL_VFX_SRC_HOST_MODULE_H

#include "instance.h"

// Runs the emitter's host modules of a stage, in order, on its particles;
// after each, a particle it left holding a value that is not finite or is
// past its limit is marked killed and counted in valuesMarked.
void mvfxRunHostModules(mvfxInstance* instance, int32_t emitter, mvfxModuleStage stage);

#endif
