// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Spawning from events: the births an emitter's source's events of the
// step give it, drawn, placed and sorted by tick.

#ifndef MAUL_VFX_SRC_EVENT_SPAWN_H
#define MAUL_VFX_SRC_EVENT_SPAWN_H

#include "instance.h"

// Fills an emitter's event queue from its source's events of the step,
// when it spawns from them; births past the link's capacity (the latest
// by tick) are counted refused and take serials as the others do.
void mvfxBuildEventBirths(mvfxInstance* instance, int32_t emitter);

#endif
