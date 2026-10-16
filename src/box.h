// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A box an instance carries (a gas's, a water surface's): where it lies in
// the world, the maps into and out of its frame, and its bounds.

#ifndef MAUL_VFX_SRC_BOX_H
#define MAUL_VFX_SRC_BOX_H

#include "force.h"
#include "instance.h"

#include "maul-vfx/output.h"

// Where a box lies now: its center and orientation in the world, and its
// scale (the world's lengths in its).
typedef struct mvfxBoxPlace
{
    double center[3];
    float rotation[4];
    float scale;
} mvfxBoxPlace;

// A box centered at center in the instance's frame: where the instance is
// at a transform when local, else where it began (origin, rotation and
// scale).
mvfxBoxPlace mvfxPlaceBox(const mvfxTransform* at, bool local, const float center[3],
                          const double origin[3], const float rotation[4], float scale);

// The map from a space (the world's, or the instance's frame) into a
// placed box's frame.
mvfxSpaceMap mvfxBoxMap(const mvfxInstance* instance, const mvfxBoxPlace* place,
                        mvfxSimulationSpace space);

// The map from a placed box's frame into a space, given the map from the
// world into it.
mvfxSpaceMap mvfxBoxToSpace(const mvfxBoxPlace* place, const mvfxSpaceMap* fromWorld);

// The box in the world around a placed box of half sizes (in the world's
// lengths).
mvfxBounds mvfxBoxBounds(const mvfxBoxPlace* place, const float half[3]);

#endif
