// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A unit's frame is fixed when its birth's step ends, its pinned points'
// places stored in it. Each step finds where the frame is at the step's
// end (the host's frame mapped into the particles' space, or the source's
// particle, found by its serial) and
// eases it there over the substeps: its origin linearly, its orientation
// by a normalized linear blend along the shorter way. The instance's
// space needs no frame: its map at each substep's tick is exact.

#include "attach.h"

#include "direction.h"
#include "space.h"

#include <math.h>

// The identity frame: no origin, no turn.
static const float s_identity[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

static uint32_t* FlagsOf(const mvfxInstance* instance, int32_t emitter)
{
    return mvfxAttributeArray(instance, emitter, mvfx_attributeAttachFlags);
}

static float* FrameArray(const mvfxInstance* instance, int32_t emitter, int32_t k)
{
    return mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeAttachX + k));
}

// A unit's stored frame (seven floats) at its first particle.
static void ReadFrame(const mvfxInstance* instance, int32_t emitter, int32_t first, float out[7])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = FrameArray(instance, emitter, k)[first];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        const float* rotation = mvfxAttributeArray(
            instance, emitter, (mvfxAttribute)(mvfx_attributeAttachRotationX + k));
        out[3 + k] = rotation[first];
    }
}

static void WriteFrame(mvfxInstance* instance, int32_t emitter, int32_t first, const float frame[7])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        FrameArray(instance, emitter, k)[first] = frame[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        float* rotation = mvfxAttributeArray(instance, emitter,
                                             (mvfxAttribute)(mvfx_attributeAttachRotationX + k));
        rotation[first] = frame[3 + k];
    }
}

void mvfxMarkNewUnit(mvfxInstance* instance, int32_t emitter, int32_t at,
                     const mvfxHostBirth* given, bool event)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    FlagsOf(instance, emitter)[at] = MVFX_ATTACH_NEW | (event ? MVFX_ATTACH_EVENT : 0u);
    if (mvfxHasAttribute(&compiled->plan, mvfx_attributeAttachSource))
    {
        uint32_t* source = mvfxAttributeArray(instance, emitter, mvfx_attributeAttachSource);
        source[at] = given == nullptr                                ? 0u
                     : compiled->unit.attach == mvfx_attachParticles ? given->source
                                                                     : (uint32_t)given->frame;
    }
}

// The particle of the event source with a serial, or -1. Live particles
// are stored in the order of their births, which take serials in order,
// so their stored serials (the low 32 bits) ascend modulo 2^32 from the
// oldest's: a binary search by the serials less the oldest's finds one.
static int32_t FindSource(const mvfxInstance* instance, int32_t emitter, uint32_t serial)
{
    int32_t source = instance->effect->emitters[emitter].eventSpawn.source;
    int32_t count = instance->emitters[source].count;
    const uint32_t* serials = mvfxAttributeArray(instance, source, mvfx_attributeSerial);
    if (count == 0)
    {
        return -1;
    }
    uint32_t base = serials[0];
    uint32_t wanted = serial - base;
    int32_t low = 0;
    int32_t high = count;
    while (low < high)
    {
        int32_t middle = low + (high - low) / 2;
        if (serials[middle] - base < wanted)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low < count && serials[low] == serial ? low : -1;
}

// The frame of the source's particle i: its place and, where its emitter
// stores one, its orientation.
static void SourceFrame(const mvfxInstance* instance, int32_t emitter, int32_t i, float out[7])
{
    int32_t source = instance->effect->emitters[emitter].eventSpawn.source;
    bool turned =
        mvfxHasAttribute(&instance->effect->emitters[source].plan, mvfx_attributeOrientationX);
    for (int32_t k = 0; k < 7; ++k)
    {
        out[k] = s_identity[k];
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        const float* place =
            mvfxAttributeArray(instance, source, (mvfxAttribute)(mvfx_attributePositionX + k));
        out[k] = place[i];
    }
    for (int32_t k = 0; turned && k < 4; ++k)
    {
        const float* q =
            mvfxAttributeArray(instance, source, (mvfxAttribute)(mvfx_attributeOrientationX + k));
        out[3 + k] = q[i];
    }
}

// The host's frame f at the step's end in the particles' space.
static void HostFrame(const mvfxInstance* instance, int32_t emitter, int32_t f, float out[7])
{
    const mvfxAttachFrame* frame = &instance->emitters[emitter].attachFrames[f];
    mvfxTransform transform;
    mvfxTransformAt(instance, instance->clock, &transform);
    mvfxSpaceMap map = mvfxSpaceMapAt(instance, emitter, mvfx_spaceWorld, &transform);
    mvfxMapPointDouble(&map, frame->position, out);
    mvfxMultiplyRotations(map.rotation, frame->rotation, out + 3);
}

// Where a unit's frame is at the step's end: its target, or false for a
// unit whose source is gone.
static bool TargetOf(const mvfxInstance* instance, int32_t emitter, int32_t first, float out[7])
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    uint32_t flags = FlagsOf(instance, emitter)[first];
    ReadFrame(instance, emitter, first, out);
    if ((flags & (MVFX_ATTACH_HELD | MVFX_ATTACH_LET_GO)) != 0)
    {
        return true;
    }
    const uint32_t* source = mvfxAttributeArray(instance, emitter, mvfx_attributeAttachSource);
    if (compiled->unit.attach == mvfx_attachHost)
    {
        if (source[first] < (uint32_t)instance->emitters[emitter].attachFrameCount)
        {
            HostFrame(instance, emitter, (int32_t)source[first], out);
        }
        return true;
    }
    int32_t i = FindSource(instance, emitter, source[first]);
    if (i < 0)
    {
        return false;
    }
    SourceFrame(instance, emitter, i, out);
    return true;
}

static bool IsFramed(const mvfxInstance* instance, int32_t emitter)
{
    mvfxAttachKind kind = instance->effect->emitters[emitter].unit.attach;
    return kind == mvfx_attachHost || kind == mvfx_attachParticles;
}

void mvfxBeginAttachments(mvfxInstance* instance, int32_t emitter)
{
    if (!IsFramed(instance, emitter))
    {
        return;
    }
    int32_t points = instance->effect->emitters[emitter].unitPoints;
    int32_t units = instance->emitters[emitter].count / points;
    float* targets = instance->emitters[emitter].attachTargets;
    uint32_t* flags = FlagsOf(instance, emitter);
    for (int32_t u = 0; u < units; ++u)
    {
        if (!TargetOf(instance, emitter, u * points, targets + 7 * u))
        {
            flags[u * points] |= MVFX_ATTACH_LET_GO;
        }
    }
}

mvfxPinFrame mvfxPinFrameAt(const mvfxInstance* instance, int32_t emitter, int64_t tick)
{
    mvfxPinFrame frame = {.kind = instance->effect->emitters[emitter].unit.attach,
                          .fraction = 1.0f};
    if (frame.kind == mvfx_attachInstance)
    {
        mvfxTransform transform;
        mvfxTransformAt(instance, tick, &transform);
        frame.map = mvfxSpaceMapAt(instance, emitter, mvfx_spaceLocal, &transform);
    }
    else if (frame.kind != mvfx_attachNone)
    {
        int64_t span = instance->clock - instance->stepStart;
        frame.fraction =
            span > 0 ? (float)((double)(tick - instance->stepStart) / (double)span) : 1.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            frame.origin[k] = FrameArray(instance, emitter, k);
        }
        for (int32_t k = 0; k < 4; ++k)
        {
            frame.rotation[k] = mvfxAttributeArray(
                instance, emitter, (mvfxAttribute)(mvfx_attributeAttachRotationX + k));
        }
        frame.targets = instance->emitters[emitter].attachTargets;
    }
    return frame;
}

// The frame of unit u (first particle first) a share of the way to its
// target.
static void EasedFrame(const mvfxPinFrame* frame, int32_t u, int32_t first, float out[7])
{
    const float* target = frame->targets + 7 * u;
    float f = frame->fraction;
    for (int32_t k = 0; k < 3; ++k)
    {
        float from = frame->origin[k][first];
        out[k] = from + (target[k] - from) * f;
    }
    float dot = 0.0f;
    for (int32_t k = 0; k < 4; ++k)
    {
        dot += frame->rotation[k][first] * target[3 + k];
    }
    float sign = dot < 0.0f ? -1.0f : 1.0f;
    float square = 0.0f;
    for (int32_t k = 0; k < 4; ++k)
    {
        float from = frame->rotation[k][first];
        out[3 + k] = from + (sign * target[3 + k] - from) * f;
        square += out[3 + k] * out[3 + k];
    }
    float length = sqrtf(square);
    for (int32_t k = 0; length > 0.0f && k < 4; ++k)
    {
        out[3 + k] /= length;
    }
}

void mvfxPinPlace(const mvfxInstance* instance, int32_t emitter, const mvfxPinFrame* frame,
                  int32_t u, int32_t i, float out[3])
{
    float anchor[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        const float* a =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeAnchorX + k));
        anchor[k] = a[i];
        out[k] = anchor[k];
    }
    if (frame->kind == mvfx_attachInstance)
    {
        mvfxMapPoint(&frame->map, anchor, out);
    }
    else if (frame->kind != mvfx_attachNone)
    {
        float eased[7];
        EasedFrame(frame, u, u * instance->effect->emitters[emitter].unitPoints, eased);
        mvfxRotate(eased + 3, anchor, out);
        for (int32_t k = 0; k < 3; ++k)
        {
            out[k] += eased[k];
        }
    }
}

void mvfxEndAttachments(mvfxInstance* instance, int32_t emitter)
{
    if (!IsFramed(instance, emitter))
    {
        return;
    }
    int32_t points = instance->effect->emitters[emitter].unitPoints;
    int32_t units = instance->emitters[emitter].count / points;
    const float* targets = instance->emitters[emitter].attachTargets;
    for (int32_t u = 0; u < units; ++u)
    {
        WriteFrame(instance, emitter, u * points, targets + 7 * u);
    }
}

// A new unit's frame at the step's end, and its flags: held for one whose
// host frame is not set or that was not born of an event. One whose source
// is already gone keeps the identity frame; the next step's start lets it
// go, before any substep holds it.
static uint32_t NewFrame(const mvfxInstance* instance, int32_t emitter, int32_t first,
                         uint32_t flags, float out[7])
{
    for (int32_t k = 0; k < 7; ++k)
    {
        out[k] = s_identity[k];
    }
    const uint32_t* source = mvfxAttributeArray(instance, emitter, mvfx_attributeAttachSource);
    if (instance->effect->emitters[emitter].unit.attach == mvfx_attachHost)
    {
        if (source[first] < (uint32_t)instance->emitters[emitter].attachFrameCount)
        {
            HostFrame(instance, emitter, (int32_t)source[first], out);
            return flags;
        }
        return flags | MVFX_ATTACH_HELD;
    }
    if ((flags & MVFX_ATTACH_EVENT) == 0)
    {
        return flags | MVFX_ATTACH_HELD;
    }
    int32_t i = FindSource(instance, emitter, source[first]);
    if (i >= 0)
    {
        SourceFrame(instance, emitter, i, out);
    }
    return flags;
}

// Stores a new unit's pinned points' places in a frame.
static void AnchorIn(mvfxInstance* instance, int32_t emitter, int32_t first, const float frame[7])
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const float inverse[4] = {-frame[3], -frame[4], -frame[5], frame[6]};
    for (int32_t j = 0; j < compiled->unitPoints; ++j)
    {
        if (!mvfxIsPinnedPoint(compiled, j))
        {
            continue;
        }
        float* anchor[3];
        float offset[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            anchor[k] =
                mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeAnchorX + k));
            offset[k] = anchor[k][first + j] - frame[k];
        }
        float turned[3];
        mvfxRotate(inverse, offset, turned);
        for (int32_t k = 0; k < 3; ++k)
        {
            anchor[k][first + j] = turned[k];
        }
    }
}

// Stores a new unit's pinned points' places in the instance's space at
// the step's end.
static void AnchorInInstance(mvfxInstance* instance, int32_t emitter, int32_t first)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxTransform transform;
    mvfxTransformAt(instance, instance->clock, &transform);
    mvfxSpaceMap map = mvfxSpaceMapAt(instance, emitter, mvfx_spaceLocal, &transform);
    for (int32_t j = 0; j < compiled->unitPoints; ++j)
    {
        float* anchor[3];
        float place[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            anchor[k] =
                mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeAnchorX + k));
            place[k] = anchor[k][first + j];
        }
        double local[3];
        mvfxUnmapPoint(&map, place, local);
        for (int32_t k = 0; mvfxIsPinnedPoint(compiled, j) && k < 3; ++k)
        {
            anchor[k][first + j] = (float)local[k];
        }
    }
}

void mvfxAttachNewUnits(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    if (compiled->unit.attach == mvfx_attachNone)
    {
        return;
    }
    int32_t points = compiled->unitPoints;
    int32_t units = instance->emitters[emitter].count / points;
    uint32_t* flags = FlagsOf(instance, emitter);
    for (int32_t u = 0; u < units; ++u)
    {
        int32_t first = u * points;
        if ((flags[first] & MVFX_ATTACH_NEW) == 0)
        {
            continue;
        }
        uint32_t kept = flags[first] & ~MVFX_ATTACH_NEW;
        if (compiled->unit.attach == mvfx_attachInstance)
        {
            AnchorInInstance(instance, emitter, first);
            flags[first] = kept;
            continue;
        }
        float frame[7];
        flags[first] = NewFrame(instance, emitter, first, kept, frame);
        WriteFrame(instance, emitter, first, frame);
        AnchorIn(instance, emitter, first, frame);
    }
}
