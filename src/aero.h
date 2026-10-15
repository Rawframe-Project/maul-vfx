// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sheets' aerodynamics: each whole triangle's drag along its normal and
// across it against the air.

#ifndef MAUL_VFX_SRC_AERO_H
#define MAUL_VFX_SRC_AERO_H

#include "instance.h"

// Changes an emitter of sheets' velocities by their triangles' drag over
// a substep of h seconds from a tick, each change found from the
// velocities at its start; room holds three arrays of the capacity.
void mvfxBlowSheets(mvfxInstance* instance, int32_t emitter, int64_t tick, float h, float* room);

#endif
