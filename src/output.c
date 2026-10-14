// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "output.h"

#include "appearance.h"
#include "context.h"
#include "direction.h"
#include "instance_api.h"
#include "layout_api.h"
#include "space.h"

#include <math.h>

// What a record reads of a particle: the emitter, the map from its
// particles' space into the world, and the view's origin.
typedef struct Source
{
    const mvfxInstance* instance;
    int32_t emitter;
    const mvfxAttributePlan* plan;
    mvfxSpaceMap map;
    const double* viewOrigin;
} Source;

static float FloatOf(const Source* s, int32_t attribute, int32_t i)
{
    return ((const float*)mvfxAttributeArray(s->instance, s->emitter, attribute))[i];
}

static void VectorOf(const Source* s, int32_t first, int32_t i, float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = FloatOf(s, first + k, i);
    }
}

// A world quaternion of particle i: the instance's turn after its own, a
// local emitter's; identity where it stores none.
static void OrientationOf(const Source* s, int32_t i, float out[4])
{
    float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (mvfxHasAttribute(s->plan, mvfx_attributeOrientationX))
    {
        for (int32_t k = 0; k < 4; ++k)
        {
            q[k] = FloatOf(s, mvfx_attributeOrientationX + k, i);
        }
    }
    const float* m = s->map.rotation;
    const float turn[4] = {-m[0], -m[1], -m[2], m[3]};
    mvfxMultiplyRotations(turn, q, out);
}

// Particle i's values for a field: floats, or words for the word fields.
static void ValuesOf(const Source* s, const mvfxLayoutField* f, int32_t i, float v[4],
                     uint32_t w[4])
{
    float p[3];
    double world[3];
    switch (f->field)
    {
    case mvfx_recordPosition:
        VectorOf(s, mvfx_attributePositionX, i, p);
        mvfxUnmapPoint(&s->map, p, world);
        for (int32_t k = 0; k < 3; ++k)
        {
            v[k] = (float)(world[k] - s->viewOrigin[k]);
        }
        break;
    case mvfx_recordSize:
        mvfxSizeOf(s->instance, s->emitter, i, v);
        for (int32_t k = 0; k < 3; ++k)
        {
            v[k] /= s->map.sigma;
        }
        break;
    case mvfx_recordColor:
        mvfxColorOf(s->instance, s->emitter, i, v);
        break;
    case mvfx_recordAngle:
        v[0] = mvfxHasAttribute(s->plan, mvfx_attributeAngle) ? FloatOf(s, mvfx_attributeAngle, i)
                                                              : 0.0f;
        break;
    case mvfx_recordOrientation:
        OrientationOf(s, i, v);
        break;
    case mvfx_recordVelocity:
        VectorOf(s, mvfx_attributeVelocityX, i, p);
        mvfxUnmapVector(&s->map, p, v);
        break;
    case mvfx_recordAge:
        v[0] = mvfxNormalizedAge(s->instance, s->emitter, i);
        break;
    case mvfx_recordFrame:
        w[0] = mvfxFrameOf(s->instance, s->emitter, i);
        v[0] = (float)w[0];
        break;
    case mvfx_recordCustom:
        mvfxCustomOf(s->instance, s->emitter, i, f->custom, v);
        break;
    default:
        w[0] =
            ((const uint32_t*)mvfxAttributeArray(s->instance, s->emitter, mvfx_attributeSerial))[i];
        break;
    }
}

void mvfxWriteRecords(const mvfxInstance* instance, int32_t emitter, const mvfxOutputLayout* layout,
                      const double viewOrigin[3], unsigned char* records, int64_t count)
{
    Source s = {
        .instance = instance,
        .emitter = emitter,
        .plan = &instance->effect->emitters[emitter].plan,
        .map = mvfxSpaceMapAt(instance, emitter, mvfx_spaceWorld, &instance->to),
        .viewOrigin = viewOrigin,
    };
    for (int64_t r = 0; r < count; ++r)
    {
        unsigned char* record = records + r * (int64_t)layout->stride;
        for (int32_t f = 0; f < layout->fieldCount; ++f)
        {
            float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            uint32_t words[4] = {0, 0, 0, 0};
            ValuesOf(&s, &layout->fields[f], (int32_t)r, values, words);
            mvfxWriteField(&layout->fields[f], values, words, record);
        }
    }
}

static bool IsOriginValid(const double origin[3])
{
    return fabs(origin[0]) <= 0x1p52 && fabs(origin[1]) <= 0x1p52 && fabs(origin[2]) <= 0x1p52;
}

mvfxResult mvfxInstance_WriteParticles(mvfxContext* context, mvfxInstanceId instance,
                                       int32_t emitter, mvfxLayoutId layout,
                                       const double viewOrigin[3], void* records, int64_t capacity,
                                       int64_t* countOut)
{
    if (context == nullptr || viewOrigin == nullptr || countOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *countOut = 0;
    mvfxInstance* live = nullptr;
    mvfxResult found = mvfxFindEmitter(context, instance, emitter, &live);
    if (found != mvfx_success)
    {
        return found;
    }
    const mvfxOutputLayout* format = mvfxContextLayout(context, layout);
    if (format == nullptr)
    {
        return mvfx_errorStale;
    }
    if (capacity < 0 || (capacity > 0 && records == nullptr) || !IsOriginValid(viewOrigin))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    int64_t count = live->emitters[emitter].count;
    *countOut = count;
    mvfxWriteRecords(live, emitter, format, viewOrigin, records,
                     count < capacity ? count : capacity);
    return mvfx_success;
}
