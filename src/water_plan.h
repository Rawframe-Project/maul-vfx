// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A planned instance's water surfaces: resolved where the CPU path would
// step them, and handed over a step at a time (mvfxInstance_GetWaterPlan).

#ifndef MAUL_VFX_SRC_WATER_PLAN_H
#define MAUL_VFX_SRC_WATER_PLAN_H

#include "instance.h"

// Resolves a planned instance's surfaces for the context's step, where
// the CPU path steps them: the host's pushes into each surface's frame
// (then dropped, as a step drops them) and a shallow water's terrain.
void mvfxPlanWaters(mvfxInstance* instance);

#endif
