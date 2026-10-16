// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A water surface's step: its pushes, then substeps of its model.

#ifndef MAUL_VFX_SRC_WATER_STEP_H
#define MAUL_VFX_SRC_WATER_STEP_H

#include "instance.h"
#include "tasks.h"

// Steps water surface w of an instance over its last step: the pushes
// given for it, then the substeps the step's length takes, each pass
// split over tasks; then whether it moves and how far it reaches.
void mvfxStepWater(mvfxInstance* instance, int32_t w, const mvfxTasks* tasks);

#endif
