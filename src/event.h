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

// Begins holding an emitter's units' contacts over their substeps
// (mvfxHoldsContacts): a unit's point writes one collision event a step,
// its first contact's, built at the contact; the held events join the
// step's after the substeps, in point order (a unit's substeps are its
// one move). Begins holding the contacts of an emitter's first count
// points, when it holds them; until the end, a collision is held, not
// recorded.
void mvfxBeginHeldContacts(mvfxInstance* instance, int32_t emitter, int32_t count);

// Holds a collision fact's event, built now, unless its point holds one.
void mvfxHoldContact(mvfxInstance* instance, int32_t emitter, const mvfxEventFact* fact);

// Appends the held events in point order, those past the buffer counted
// dropped, and stops holding.
void mvfxEndHeldContacts(mvfxInstance* instance, int32_t emitter, int32_t count);

#endif
