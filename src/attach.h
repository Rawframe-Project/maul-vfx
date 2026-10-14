// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What units' pinned points are held to: the instance, the host's frames
// or the particles of an event source, each unit's frame eased over a
// step's substeps.

#ifndef MAUL_VFX_SRC_ATTACH_H
#define MAUL_VFX_SRC_ATTACH_H

#include "force.h"
#include "instance.h"

// A unit's attachment flags: born this step and not yet attached; born of
// an event; held where it is, its frame never moving; let go, its points
// free.
#define MVFX_ATTACH_NEW    1u
#define MVFX_ATTACH_EVENT  2u
#define MVFX_ATTACH_HELD   4u
#define MVFX_ATTACH_LET_GO 8u

// Where a substep's tick finds the frames: the instance's space's map into
// the particles' for mvfx_attachInstance; else how far each unit's frame
// has gone from the step's start toward its end.
typedef struct mvfxPinFrame
{
    mvfxAttachKind kind;
    mvfxSpaceMap map;
    float fraction;
    const float* origin[3];
    const float* rotation[4];
    const float* targets;
} mvfxPinFrame;

// Marks a unit appended at a particle as new: born of an event, or of the
// host with its frame, or neither.
void mvfxMarkNewUnit(mvfxInstance* instance, int32_t emitter, int32_t at,
                     const mvfxHostBirth* given, bool event);

// Readies an emitter of units held to frames for its step: its event
// source's serials sorted, for the lookups that follow.
void mvfxPrepareAttachments(mvfxInstance* instance, int32_t emitter);

// Each live unit's frame at the step's end, before its substeps; a unit
// whose source is gone let go.
void mvfxBeginAttachments(mvfxInstance* instance, int32_t emitter);

// The frames at a tick of the step.
mvfxPinFrame mvfxPinFrameAt(const mvfxInstance* instance, int32_t emitter, int64_t tick);

// A pinned point's place at a frame: the point at particle i of unit u.
void mvfxPinPlace(const mvfxInstance* instance, int32_t emitter, const mvfxPinFrame* frame,
                  int32_t u, int32_t i, float out[3]);

// Each live unit's frame becomes its target, after its substeps.
void mvfxEndAttachments(mvfxInstance* instance, int32_t emitter);

// Attaches the units born this step at the step's end: each frame fixed
// and its pinned points' places in it.
void mvfxAttachNewUnits(mvfxInstance* instance, int32_t emitter);

#endif
