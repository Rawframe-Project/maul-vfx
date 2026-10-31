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
// by tick) are dropped, counted, and take serials as the others do.
void mvfxBuildEventBirths(mvfxInstance* instance, int32_t emitter);

// The key a birth from an event draws its values by, in place of its
// serial, so births from events never share a serial's draws: the
// event's particle's serial in the low word; in the high, the top bit
// (no serial reaches it) and 31 bits of pcg4d of the event's tick and
// the birth's index among the event's. A count that differs changes
// that event's births alone.
uint64_t mvfxEventBirthKey(uint32_t source, int64_t tick, int32_t index);

#endif
