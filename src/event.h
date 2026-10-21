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

// Clears an emitter's events for a new context step; begins a step's,
// after those the context step's earlier steps wrote.
void mvfxClearEvents(mvfxInstance* instance, int32_t emitter);
void mvfxBeginEvents(mvfxInstance* instance, int32_t emitter);

// Records an event when the emitter writes its kind, or counts it
// dropped when the buffer is full.
void mvfxRecordEvent(mvfxInstance* instance, int32_t emitter, const mvfxEventFact* fact);

// Which events a unit's point holds: its first contact, its tear across,
// its tear down.
#define MVFX_HELD_CONTACT     1u
#define MVFX_HELD_TEAR_ACROSS 2u
#define MVFX_HELD_TEAR_DOWN   4u

// Begins holding an emitter's units' or rigid bodies' events over their
// substeps, each built when it happens: a unit's point or a body writes
// one collision event a step, its first contact's or strike's
// (mvfxHoldsContacts), and a point an event a tear, at most one an edge
// (mvfxHoldsTears). The held events join the step's after the substeps
// point by point, each point's contact, then its tear across, then its
// tear down (a unit's or a body's substeps are its one move). Begins holding
// the events of an emitter's first count points, when it holds any; until
// the end, a collision or a tear is held, not recorded.
void mvfxBeginHeldEvents(mvfxInstance* instance, int32_t emitter, int32_t count);

// Holds a collision fact's event, built now, unless its point holds one.
void mvfxHoldContact(mvfxInstance* instance, int32_t emitter, const mvfxEventFact* fact);

// Holds a tear fact's event, built now, as its point's tear across or down
// (MVFX_HELD_TEAR_*), when the emitter holds tears.
void mvfxHoldTear(mvfxInstance* instance, int32_t emitter, const mvfxEventFact* fact,
                  uint32_t edge);

// Appends the held events point by point, those past the buffer counted
// dropped, and stops holding.
void mvfxEndHeldEvents(mvfxInstance* instance, int32_t emitter, int32_t count);

#endif
