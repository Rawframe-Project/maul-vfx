// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Beams: an emitter's points placed each step along a curve from a start
// to an end, with noise across it faded to 0 at both ends.

#ifndef MAUL_VFX_SRC_BEAM_H
#define MAUL_VFX_SRC_BEAM_H

#include "instance.h"

// Places a beam emitter's live points at the step's end: along its curve
// in the instance's space through the transform into the particles'
// space, at rest, their distances along it stored for a tile by distance.
void mvfxPlaceBeam(mvfxInstance* instance, int32_t emitter);

#endif
