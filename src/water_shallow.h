// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shallow water's step: its ground, its sources and pushes, then the
// virtual pipe model's substeps.

#ifndef MAUL_VFX_SRC_WATER_SHALLOW_H
#define MAUL_VFX_SRC_WATER_SHALLOW_H

#include "instance.h"
#include "tasks.h"

// Steps shallow water surface w of an instance over its last step: its
// ground read, filled at its first step, its sources and pushes, then
// substeps while time is left, each pass split over tasks; then whether
// it moves and how far its surface reaches.
void mvfxStepShallow(mvfxInstance* instance, int32_t w, const mvfxTasks* tasks);

#endif
