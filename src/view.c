// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "view.h"

#include "bounds.h"
#include "context.h"
#include "fpenv.h"
#include "gas_api.h"
#include "instance.h"
#include "instance_api.h"
#include "refusal.h"
#include "water_api.h"

#include <math.h>

// The box around two boxes.
static mvfxBounds Join(mvfxBounds a, mvfxBounds b)
{
    if (a.empty)
    {
        return b;
    }
    if (b.empty)
    {
        return a;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        a.min[k] = fmin(a.min[k], b.min[k]);
        a.max[k] = fmax(a.max[k], b.max[k]);
    }
    return a;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_GetBoundsWork(mvfxContext* context,
                                                           mvfxInstanceId instance, int32_t emitter,
                                                           mvfxBounds* boundsOut)
{
    if (boundsOut != nullptr)
    {
        *boundsOut = (mvfxBounds){.empty = true};
    }
    if (context == nullptr || boundsOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    mvfxInstance* live = nullptr;
    mvfxResult found = mvfxFindEmitters(context, instance, emitter, &live);
    if (found != mvfx_success)
    {
        return found;
    }
    for (int32_t e = 0; e < live->effect->emitterCount; ++e)
    {
        if (emitter == -1 || emitter == e)
        {
            *boundsOut = Join(*boundsOut, mvfxBoundsOf(live, e));
        }
    }
    // The instance's bounds hold its gases' boxes and its water surfaces'.
    for (int32_t g = 0; emitter == -1 && g < live->effect->gasCount; ++g)
    {
        *boundsOut = Join(*boundsOut, mvfxGasBounds(live, g));
    }
    for (int32_t w = 0; emitter == -1 && w < live->effect->waterCount; ++w)
    {
        *boundsOut = Join(*boundsOut, mvfxWaterBounds(live, w));
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_GetBounds(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                  mvfxBounds* boundsOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_GetBoundsWork(context, instance, emitter, boundsOut);
    mvfxFloatEnvLeave(env);
    return result;
}

// Whether a box lies wholly outside one of a frustum's planes: its corner
// farthest along the plane's normal is behind it.
static bool IsOutside(const mvfxFrustum* frustum, const mvfxBounds* box)
{
    for (int32_t p = 0; frustum != nullptr && p < frustum->planeCount; ++p)
    {
        const double* plane = frustum->planes[p];
        double reach = plane[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            reach += plane[k] * (plane[k] >= 0.0 ? box->max[k] : box->min[k]);
        }
        if (reach < 0.0)
        {
            return true;
        }
    }
    return false;
}

// Whether a comes before b in the draw order.
static bool Before(const mvfxDrawItem* a, const mvfxDrawItem* b)
{
    if (a->priority != b->priority)
    {
        return a->priority < b->priority;
    }
    if (a->distance != b->distance)
    {
        return a->distance > b->distance;
    }
    if (a->instance.index1 != b->instance.index1)
    {
        return a->instance.index1 < b->instance.index1;
    }
    return a->emitter < b->emitter;
}

static void SiftDown(mvfxDrawItem* items, int64_t root, int64_t count)
{
    for (;;)
    {
        int64_t child = 2 * root + 1;
        if (child >= count)
        {
            return;
        }
        if (child + 1 < count && Before(&items[child], &items[child + 1]))
        {
            child += 1;
        }
        if (!Before(&items[root], &items[child]))
        {
            return;
        }
        mvfxDrawItem swap = items[root];
        items[root] = items[child];
        items[child] = swap;
        root = child;
    }
}

// Keeps the first capacity items of the order in a heap with the last of
// them on top.
static void Keep(mvfxDrawItem* heap, int64_t* kept, int64_t capacity, const mvfxDrawItem* item)
{
    if (*kept < capacity)
    {
        int64_t at = (*kept)++;
        heap[at] = *item;
        while (at > 0 && Before(&heap[(at - 1) / 2], &heap[at]))
        {
            mvfxDrawItem swap = heap[(at - 1) / 2];
            heap[(at - 1) / 2] = heap[at];
            heap[at] = swap;
            at = (at - 1) / 2;
        }
        return;
    }
    if (capacity > 0 && Before(item, &heap[0]))
    {
        heap[0] = *item;
        SiftDown(heap, 0, *kept);
    }
}

static mvfxResult CheckView(const double eye[3], const mvfxFrustum* frustum)
{
    if (!(fabs(eye[0]) <= 0x1p52 && fabs(eye[1]) <= 0x1p52 && fabs(eye[2]) <= 0x1p52))
    {
        return mvfx_errorInvalid;
    }
    if (frustum == nullptr)
    {
        return mvfx_success;
    }
    // The count bounds the planes read below.
    mvfxResult count = mvfxCountUpTo(frustum->planeCount, 0, MVFX_MAX_FRUSTUM_PLANES);
    for (int32_t p = 0; count == mvfx_success && p < frustum->planeCount; ++p)
    {
        for (int32_t k = 0; k < 4; ++k)
        {
            count = isfinite(frustum->planes[p][k]) ? count : mvfx_errorInvalid;
        }
    }
    return count;
}

// The draw item of an emitter for an eye: its bounds' center's distance.
static mvfxDrawItem ItemOf(const mvfxInstance* instance, mvfxInstanceId id, int32_t emitter,
                           const mvfxBounds* box, const double eye[3])
{
    double square = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        double d = 0.5 * (box->min[k] + box->max[k]) - eye[k];
        square += d * d;
    }
    return (mvfxDrawItem){.instance = id,
                          .emitter = emitter,
                          .priority = instance->effect->emitters[emitter].drawPriority,
                          .distance = sqrt(square)};
}

static MVFX_NOINLINE mvfxResult mvfxContext_OrderEmittersWork(mvfxContext* context,
                                                              const double eye[3],
                                                              const mvfxFrustum* frustum,
                                                              mvfxDrawItem* items, int64_t capacity,
                                                              int64_t* countOut)
{
    if (countOut != nullptr)
    {
        *countOut = 0;
    }
    mvfxResult valid = context == nullptr || eye == nullptr || countOut == nullptr ||
                               capacity < 0 || (capacity > 0 && items == nullptr)
                           ? mvfx_errorInvalid
                           : CheckView(eye, frustum);
    if (valid != mvfx_success)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return valid;
    }
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorBusy;
    }
    int64_t count = 0;
    int64_t kept = 0;
    for (uint32_t i = 0; i < context->instances.capacity; ++i)
    {
        const mvfxInstance* instance = context->instances.slots[i].object;
        for (int32_t e = 0; instance != nullptr && e < instance->effect->emitterCount; ++e)
        {
            mvfxBounds box = mvfxBoundsOf(instance, e);
            if (instance->emitters[e].count == 0 || box.empty || IsOutside(frustum, &box))
            {
                continue;
            }
            mvfxInstanceId id = {i + 1, context->instances.slots[i].generation};
            mvfxDrawItem item = ItemOf(instance, id, e, &box, eye);
            Keep(items, &kept, capacity, &item);
            ++count;
        }
    }
    for (int64_t end = kept - 1; end > 0; --end)
    {
        mvfxDrawItem swap = items[0];
        items[0] = items[end];
        items[end] = swap;
        SiftDown(items, 0, end);
    }
    *countOut = count;
    return mvfx_success;
}

mvfxResult mvfxContext_OrderEmitters(mvfxContext* context, const double eye[3],
                                     const mvfxFrustum* frustum, mvfxDrawItem* items,
                                     int64_t capacity, int64_t* countOut)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result =
        mvfxContext_OrderEmittersWork(context, eye, frustum, items, capacity, countOut);
    if (result != mvfx_success && result != mvfx_errorBusy)
    {
        mvfxClearSpan(items, capacity, sizeof(mvfxDrawItem), alignof(mvfxDrawItem));
    }
    mvfxFloatEnvLeave(env);
    return result;
}
