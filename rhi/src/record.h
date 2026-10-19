// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's statistics record (MVFX_GPU_STATISTICS_WORDS words): what
// the GPU writes at a step's end (counts, bounds in the particles'
// space), then the map to the world the step's transfer pass writes, so a
// record read back late maps itself.

#ifndef MAUL_VFX_RHI_SRC_RECORD_H
#define MAUL_VFX_RHI_SRC_RECORD_H

#include "maul-vfx-rhi/sim.h"

// The record's words: the live count, the live count before the step,
// those past the capacity, the growth (a binary32), the box's min and
// max (binary32 each), all the GPU's; then the space, the instance's
// scale (binary32), the plan's anchor after and the instance's origin at
// the plan's end (binary64 each, two words), its rotation (binary32);
// then the GPU's again: the events written and dropped, four words of
// its own, and for an emitter that spawns from events the serials its
// event births took (two words), those refused in the step and the births
// past its room (MVFX_GPU_EVENT_SERIALS), and for an emitter drawn as
// ribbons the GPU's breaks (MVFX_GPU_RIBBON_BREAKS); then for an emitter
// whose ribbons pin a head the step's transfer pass's again: whether the
// head shows at the step's end and its place in the world (binary64);
// then the GPU's: the neighbor caps (MVFX_GPU_NEIGHBORS_CAPPED), the
// contact caps (MVFX_GPU_CONTACTS_CAPPED), the step's links and the links
// dropped (MVFX_GPU_LINKS, MVFX_GPU_LINKS_DROPPED), the tears
// (MVFX_GPU_TORN), the self-contacts' caps (MVFX_GPU_SELF_CAPPED) and the
// rigid contacts' (MVFX_GPU_RIGID_CAPPED). The layout plan budgets the
// record at 64 words.
static_assert(MVFX_GPU_STATISTICS_WORDS <= 64, "the statistics record within its 64 words");

enum
{
    MVFX_RECORD_COUNT = 0,
    MVFX_RECORD_BEFORE = 1,
    MVFX_RECORD_PAST = 2,
    MVFX_RECORD_GROWTH = 3,
    MVFX_RECORD_MIN = 4,
    MVFX_RECORD_MAX = 7,
    MVFX_RECORD_SPACE = 10,
    MVFX_RECORD_SCALE = 11,
    MVFX_RECORD_ANCHOR = 12,
    MVFX_RECORD_ORIGIN = 18,
    MVFX_RECORD_ROTATION = 24,
    // The events written and those dropped (MVFX_GPU_EVENTS_WRITTEN), then
    // the kernels' words.
    MVFX_RECORD_EVENTS = 28,
    MVFX_RECORD_EVENT_SERIALS = 34,
    // The words the transfer pass writes, from MVFX_RECORD_SPACE on.
    MVFX_RECORD_MAP_WORDS = 18,
    MVFX_RECORD_HEAD = 39,
    MVFX_RECORD_HEAD_PLACE = 40,
    // The head's words the transfer pass writes, from MVFX_RECORD_HEAD on.
    MVFX_RECORD_HEAD_WORDS = 7
};

// Writes the map of a plan's step to the world, the record's words from
// MVFX_RECORD_SPACE on.
void mvfxRecordMap(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                   uint32_t words[MVFX_RECORD_MAP_WORDS]);

// Writes a plan's pinned head, the record's words from MVFX_RECORD_HEAD
// on.
void mvfxRecordHead(const mvfxEmitterPlan* plan, uint32_t words[MVFX_RECORD_HEAD_WORDS]);

// An emitter's bounds in the world from its view and a record.
mvfxBounds mvfxRecordBounds(const mvfxEmitterView* view, const uint32_t* record);

#endif
