// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the host gives an instance between steps: particles to spawn,
// frames to attach units to, a depth image, bodies woken and an
// emitter's mesh.

#include "context.h"
#include "fpenv.h"
#include "instance.h"
#include "instance_api.h"
#include "mesh_api.h"
#include "rigid_wake.h"
#include "spawn.h"

#include <math.h>

// Whether count values of components each are finite and each component
// at most its bound in size; true for NULL.
static bool AreValuesWithin(const float* values, int64_t count, int32_t components,
                            const float* bounds)
{
    for (int64_t i = 0; values != nullptr && i < count * components; ++i)
    {
        if (!(fabsf(values[i]) <= bounds[i % components]))
        {
            return false;
        }
    }
    return true;
}

// Whether a spawn request is valid for an emitter with room in its queue.
static bool IsSpawnValid(const mvfxCompiledEmitter* emitter, const mvfxSpawnRequest* request)
{
    if (request->count < 1 || request->positions == nullptr ||
        (request->colors != nullptr && !mvfxHasAttribute(&emitter->plan, mvfx_attributeColorR)) ||
        (request->sizes != nullptr && !mvfxHasAttribute(&emitter->plan, mvfx_attributeSizeX)))
    {
        return false;
    }
    int64_t count = request->count;
    for (int64_t i = 0; i < 3 * count; ++i)
    {
        if (!(fabs(request->positions[i]) <= 0x1p52))
        {
            return false;
        }
    }
    bool hosted = emitter->unitPoints > 1 && emitter->unit.attach == mvfx_attachHost;
    for (int64_t i = 0; request->attachFrames != nullptr && i < count; ++i)
    {
        int32_t frame = request->attachFrames[i];
        if (!hosted || frame < 0 || frame >= emitter->unit.attachFrameCapacity)
        {
            return false;
        }
    }
    // Colours and sizes within what the emitter's particles may store.
    static const float speeds[3] = {MVFX_MAX_EXTENT, MVFX_MAX_EXTENT, MVFX_MAX_EXTENT};
    const mvfxStoredCaps* caps = &emitter->overLife.caps;
    return AreValuesWithin(request->velocities, count, 3, speeds) &&
           AreValuesWithin(request->colors, count, 4, caps->color) &&
           AreValuesWithin(request->sizes, count, 3, caps->size);
}

// Copies a request's i-th particle, flagging the values it gives.
static mvfxHostBirth HostBirthOf(const mvfxSpawnRequest* request, int32_t i)
{
    mvfxHostBirth host = {.frame = request->attachFrames != nullptr ? request->attachFrames[i] : 0};
    for (int32_t k = 0; k < 3; ++k)
    {
        host.position[k] = request->positions[3 * i + k];
    }
    if (request->velocities != nullptr)
    {
        host.given |= mvfx_givenVelocity;
        for (int32_t k = 0; k < 3; ++k)
        {
            host.velocity[k] = request->velocities[3 * i + k];
        }
    }
    if (request->colors != nullptr)
    {
        host.given |= mvfx_givenColor;
        for (int32_t k = 0; k < 4; ++k)
        {
            host.color[k] = request->colors[4 * i + k];
        }
    }
    if (request->sizes != nullptr)
    {
        host.given |= mvfx_givenSize;
        for (int32_t k = 0; k < 3; ++k)
        {
            host.size[k] = request->sizes[3 * i + k];
        }
    }
    return host;
}

mvfxResult mvfxInstance_Spawn(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                              const mvfxSpawnRequest* request)
{
    if (context == nullptr || request == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorState;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (emitter < 0 || emitter >= live->effect->emitterCount ||
        !IsSpawnValid(&live->effect->emitters[emitter], request))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxEmitterState* state = &live->emitters[emitter];
    if (request->count > live->effect->emitters[emitter].hostSpawnCapacity - state->hostCount)
    {
        return mvfx_errorCapacity;
    }
    for (int32_t i = 0; i < request->count; ++i)
    {
        state->hostQueue[state->hostCount++] = HostBirthOf(request, i);
    }
    return mvfx_success;
}

// Whether a host's frame is valid: its place finite and at most 2^52 in
// size, its orientation of length 1 within 1e-3.
static bool IsAttachFrameValid(const mvfxAttachFrame* frame)
{
    double square = 0.0;
    for (int32_t k = 0; k < 4; ++k)
    {
        square += (double)frame->rotation[k] * (double)frame->rotation[k];
    }
    bool placed = true;
    for (int32_t k = 0; k < 3; ++k)
    {
        placed = placed && fabs(frame->position[k]) <= 0x1p52;
    }
    return placed && fabs(sqrt(square) - 1.0) <= 1e-3;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_SetAttachFramesWork(mvfxContext* context,
                                                                 mvfxInstanceId instance,
                                                                 int32_t emitter,
                                                                 const mvfxAttachFrame* frames,
                                                                 int32_t count)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorState;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    const mvfxCompiledEmitter* compiled = emitter >= 0 && emitter < live->effect->emitterCount
                                              ? &live->effect->emitters[emitter]
                                              : nullptr;
    bool valid = compiled != nullptr && compiled->unitPoints > 1 &&
                 compiled->unit.attach == mvfx_attachHost && count >= 0 &&
                 count <= compiled->unit.attachFrameCapacity && (count == 0 || frames != nullptr);
    for (int32_t f = 0; valid && f < count; ++f)
    {
        valid = IsAttachFrameValid(&frames[f]);
    }
    if (!valid)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxEmitterState* state = &live->emitters[emitter];
    for (int32_t f = 0; f < count; ++f)
    {
        state->attachFrames[f] = frames[f];
    }
    state->attachFrameCount = count;
    return mvfx_success;
}

mvfxResult mvfxInstance_SetAttachFrames(mvfxContext* context, mvfxInstanceId instance,
                                        int32_t emitter, const mvfxAttachFrame* frames,
                                        int32_t count)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_SetAttachFramesWork(context, instance, emitter, frames, count);
    mvfxFloatEnvLeave(env);
    return result;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_SetDepthImageWork(mvfxContext* context,
                                                               mvfxInstanceId instance,
                                                               const mvfxDepthImage* image)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorState;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (image == nullptr)
    {
        live->depthSerial += 1u;
        live->hasDepthView = false;
        return mvfx_success;
    }
    mvfxDepthView view;
    if (!mvfxPrepareDepthView(image, live->planned, &view))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    live->depthSerial += 1u;
    live->depthView = view;
    live->hasDepthView = true;
    return mvfx_success;
}

mvfxResult mvfxInstance_SetDepthImage(mvfxContext* context, mvfxInstanceId instance,
                                      const mvfxDepthImage* image)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_SetDepthImageWork(context, instance, image);
    mvfxFloatEnvLeave(env);
    return result;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_WakeBodiesWork(mvfxContext* context,
                                                            mvfxInstanceId instance,
                                                            int32_t emitter, const float center[3],
                                                            float radius)
{
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorState;
    }
    bool valid = context != nullptr && center != nullptr && isfinite(center[0]) &&
                 isfinite(center[1]) && isfinite(center[2]) && radius >= 0.0f;
    if (!valid)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (emitter < 0 || emitter >= live->effect->emitterCount)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxWakeWithin(live, emitter, center, radius);
    return mvfx_success;
}

mvfxResult mvfxInstance_WakeBodies(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                   const float center[3], float radius)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_WakeBodiesWork(context, instance, emitter, center, radius);
    mvfxFloatEnvLeave(env);
    return result;
}

mvfxResult mvfxInstance_SetEmitterMesh(mvfxContext* context, mvfxInstanceId instance,
                                       int32_t emitter, mvfxMeshId mesh)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorState;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    mvfxMesh* bound = mvfxContextMesh(context, mesh);
    if (live == nullptr || bound == nullptr)
    {
        return mvfx_errorStale;
    }
    if (emitter < 0 || emitter >= live->effect->emitterCount ||
        live->effect->emitters[emitter].shape.kind != mvfx_shapeMesh ||
        !mvfxMeshHasMode(bound, live->effect->emitters[emitter].shape.meshMode))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxEmitterState* state = &live->emitters[emitter];
    state->mesh->users -= 1;
    bound->users += 1;
    state->mesh = bound;
    return mvfx_success;
}
