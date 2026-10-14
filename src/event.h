// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Events: what the step's particles did (births, deaths, collisions),
// written to each emitter's bounded buffer as the step meets them.

#ifndef MAUL_VFX_SRC_EVENT_H
#define MAUL_VFX_SRC_EVENT_H

#include "instance.h"

// What an event records about particle i.
typedef struct mvfxEventFact
{
    mvfxEventKind kind;
    bool killed;
    int32_t particle;
    // When it happened, and the tick whose transform maps its place into
    // the world.
    int64_t tick;
    int64_t placeTick;
    // A collision's contact and normal in the particles' space; NULL to
    // use the particle's place and no normal.
    const float* point;
    const float* normal;
} mvfxEventFact;

// Clears an emitter's events for a new step.
void mvfxBeginEvents(mvfxInstance* instance, int32_t emitter);

// Records an event when the emitter writes its kind, or counts it
// dropped when the buffer is full.
void mvfxRecordEvent(mvfxInstance* instance, int32_t emitter, const mvfxEventFact* fact);

#endif
