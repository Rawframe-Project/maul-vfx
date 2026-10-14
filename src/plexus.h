// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Plexus links: pairs of an emitter's particles closer than a distance.

#ifndef MVFX_PLEXUS_H
#define MVFX_PLEXUS_H

#include "instance.h"

// At an emitter's step's end, its particles placed: finds its plexus
// links, each pair once (the lower index owns it), counting those past the
// most kept or owned.
void mvfxFindLinks(mvfxInstance* instance, int32_t emitter);

#endif
