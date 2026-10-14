// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "output.h"

#include "appearance.h"
#include "context.h"
#include "direction.h"
#include "instance_api.h"
#include "layout_api.h"
#include "sort.h"
#include "space.h"

#include <math.h>
#include <stdint.h>

static float FloatOf(const mvfxRecordSource* s, int32_t attribute, int32_t i)
{
    return ((const float*)mvfxAttributeArray(s->instance, s->emitter, attribute))[i];
}

// Particle i's word of a uint32 attribute; 0 where the emitter stores
// none.
static uint32_t WordOf(const mvfxRecordSource* s, int32_t attribute, int32_t i)
{
    if (!mvfxHasAttribute(s->plan, attribute))
    {
        return 0;
    }
    return ((const uint32_t*)mvfxAttributeArray(s->instance, s->emitter, attribute))[i];
}

static void VectorOf(const mvfxRecordSource* s, int32_t first, int32_t i, float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = FloatOf(s, first + k, i);
    }
}

// A world quaternion of particle i: the instance's turn after its own, a
// local emitter's; identity where it stores none.
static void OrientationOf(const mvfxRecordSource* s, int32_t i, float out[4])
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

// A ribbon field's values: a point's, or for a particle's record a zero
// tangent, length, coordinate and ribbon, and its size's X as its width.
static void RibbonValuesOf(const mvfxRecordSource* s, const mvfxLayoutField* f, int32_t i,
                           const mvfxRibbonPoint* point, float v[4], uint32_t w[4])
{
    static const mvfxRibbonPoint none = {.width = 1.0f};
    const mvfxRibbonPoint* at = point != nullptr ? point : &none;
    float size[3];
    switch (f->field)
    {
    case mvfx_recordTangent:
        for (int32_t k = 0; k < 3; ++k)
        {
            v[k] = at->tangent[k];
        }
        break;
    case mvfx_recordWidth:
        mvfxSizeOf(s->instance, s->emitter, i, size);
        v[0] = size[0] / s->map.sigma * at->width;
        break;
    case mvfx_recordLength:
        v[0] = at->length;
        break;
    case mvfx_recordAlong:
        v[0] = at->along;
        break;
    case mvfx_recordSide:
        for (int32_t k = 0; k < 3; ++k)
        {
            v[k] = at->side[k];
        }
        break;
    case mvfx_recordFaceNormal:
        for (int32_t k = 0; k < 3; ++k)
        {
            v[k] = at->faceNormal[k];
        }
        break;
    case mvfx_recordCorner:
        v[0] = at->corner[0];
        v[1] = at->corner[1];
        break;
    default:
        w[0] = at->ribbon;
        v[0] = (float)at->ribbon;
        break;
    }
}

// Particle i's values for a field: floats, or words for the word fields;
// a ribbon point's where point is given.
static void ValuesOf(const mvfxRecordSource* s, const mvfxLayoutField* f, int32_t i,
                     const mvfxRibbonPoint* point, float v[4], uint32_t w[4])
{
    if (f->field >= mvfx_recordTangent)
    {
        RibbonValuesOf(s, f, i, point, v, w);
        return;
    }
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
        for (int32_t k = 0; point != nullptr && k < 4; ++k)
        {
            v[k] *= point->color[k];
        }
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
    case mvfx_recordMesh:
        w[0] = WordOf(s, mvfx_attributeMesh, i);
        v[0] = (float)w[0];
        break;
    default:
        w[0] = WordOf(s, mvfx_attributeSerial, i);
        break;
    }
}

mvfxRecordSource mvfxOpenRecordSource(const mvfxInstance* instance, int32_t emitter,
                                      const double viewOrigin[3])
{
    return (mvfxRecordSource){
        .instance = instance,
        .emitter = emitter,
        .plan = &instance->effect->emitters[emitter].plan,
        .map = mvfxSpaceMapAt(instance, emitter, mvfx_spaceWorld, &instance->to),
        .viewOrigin = viewOrigin,
    };
}

void mvfxWorldPlaceOf(const mvfxRecordSource* source, int32_t particle, double out[3])
{
    float p[3];
    VectorOf(source, mvfx_attributePositionX, particle, p);
    mvfxUnmapPoint(&source->map, p, out);
}

void mvfxWriteRecord(const mvfxRecordSource* source, const mvfxOutputLayout* layout,
                     int32_t particle, const mvfxRibbonPoint* point, unsigned char* record)
{
    for (int32_t f = 0; f < layout->fieldCount; ++f)
    {
        float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        uint32_t words[4] = {0, 0, 0, 0};
        ValuesOf(source, &layout->fields[f], particle, point, values, words);
        mvfxWriteField(&layout->fields[f], values, words, record);
    }
}

// Whether a field counts (a frame, serial, mesh or ribbon), so a point
// between particles takes the nearer one's.
static bool IsCounted(mvfxField field)
{
    return field == mvfx_recordFrame || field == mvfx_recordSerial || field == mvfx_recordMesh ||
           field == mvfx_recordRibbon;
}

// Values a fraction of the way from a to b, an orientation's along the
// shorter way and renormalized.
static void Between(mvfxField field, float a[4], const float b[4], float fraction)
{
    float sign = 1.0f;
    if (field == mvfx_recordOrientation)
    {
        float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
        sign = dot < 0.0f ? -1.0f : 1.0f;
    }
    float square = 0.0f;
    for (int32_t k = 0; k < 4; ++k)
    {
        a[k] += (sign * b[k] - a[k]) * fraction;
        square += a[k] * a[k];
    }
    if (field == mvfx_recordOrientation && square > 0.0f)
    {
        float length = sqrtf(square);
        for (int32_t k = 0; k < 4; ++k)
        {
            a[k] /= length;
        }
    }
}

void mvfxWriteRibbonRecord(const mvfxRecordSource* source, const mvfxOutputLayout* layout,
                           int32_t first, int32_t second, float fraction, const double place[3],
                           const mvfxRibbonPoint* point, unsigned char* record)
{
    int32_t nearer = fraction < 0.5f ? first : second;
    for (int32_t f = 0; f < layout->fieldCount; ++f)
    {
        const mvfxLayoutField* field = &layout->fields[f];
        float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        uint32_t words[4] = {0, 0, 0, 0};
        if (field->field == mvfx_recordPosition)
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                values[k] = (float)(place[k] - source->viewOrigin[k]);
            }
        }
        else if (IsCounted(field->field) || fraction == 0.0f || first == second)
        {
            ValuesOf(source, field, nearer, point, values, words);
        }
        else
        {
            float other[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            ValuesOf(source, field, first, point, values, words);
            ValuesOf(source, field, second, point, other, words);
            Between(field->field, values, other, fraction);
        }
        mvfxWriteField(field, values, words, record);
    }
}

void mvfxWriteRecords(const mvfxInstance* instance, int32_t emitter, const mvfxOutputLayout* layout,
                      const double viewOrigin[3], const mvfxRecordOrder* order,
                      unsigned char* records, int64_t count)
{
    mvfxRecordSource s = mvfxOpenRecordSource(instance, emitter, viewOrigin);
    for (int64_t r = 0; r < count; ++r)
    {
        int32_t i = mvfxOrderAt(order, r);
        mvfxWriteRecord(&s, layout, i, nullptr, records + r * (int64_t)layout->stride);
    }
}

bool mvfxIsViewValid(const mvfxView* view)
{
    bool valid = true;
    for (int32_t k = 0; k < 3; ++k)
    {
        valid = valid && fabs(view->origin[k]) <= 0x1p52 && fabs(view->eye[k]) <= 0x1p52 &&
                isfinite(view->forward[k]) && isfinite(view->up[k]);
    }
    return valid;
}

// Whether an emitter sorts by place, needing scratch.
static bool SortsByPlace(const mvfxCompiledEmitter* compiled)
{
    return compiled->sortMode == mvfx_sortBackToFront || compiled->sortMode == mvfx_sortFrontToBack;
}

// Whether an emitter's output needs scratch: a sort by place, or ribbons.
static bool NeedsScratch(const mvfxCompiledEmitter* compiled)
{
    return SortsByPlace(compiled) || compiled->ribbonIds != mvfx_ribbonNone;
}

// Sorts an emitter's live particles by their distance or depth from a
// view's eye, in scratch of four arrays of live words: the order is the
// second.
static const uint32_t* SortByPlace(const mvfxInstance* instance, int32_t emitter,
                                   const mvfxView* view, uint32_t* scratch, int32_t live)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxRecordSource s = {
        .instance = instance,
        .emitter = emitter,
        .plan = &compiled->plan,
        .map = mvfxSpaceMapAt(instance, emitter, mvfx_spaceWorld, &instance->to),
    };
    uint32_t* keys = scratch;
    uint32_t* indices = scratch + live;
    // Back to front sorts the keys' complements, so equal keys keep the
    // pool's order either way.
    uint32_t flip = compiled->sortMode == mvfx_sortBackToFront ? 0xFFFFFFFFu : 0u;
    bool depth = compiled->sortMeasure == mvfx_sortByDepth;
    for (int32_t i = 0; i < live; ++i)
    {
        float p[3];
        double world[3];
        VectorOf(&s, mvfx_attributePositionX, i, p);
        mvfxUnmapPoint(&s.map, p, world);
        double measure = 0.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            double d = world[k] - view->eye[k];
            measure += depth ? d * (double)view->forward[k] : d * d;
        }
        keys[i] = mvfxOrderedKey((float)measure) ^ flip;
        indices[i] = (uint32_t)i;
    }
    mvfxRadixSort(keys, indices, scratch + 2 * (int64_t)live, scratch + 3 * (int64_t)live, live);
    return indices;
}

bool mvfxOrderRecords(const mvfxInstance* instance, int32_t emitter, const mvfxView* view,
                      void* scratch, int64_t scratchBytes, mvfxRecordOrder* orderOut)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    int32_t count = instance->emitters[emitter].count;
    *orderOut =
        (mvfxRecordOrder){.reverse = compiled->sortMode == mvfx_sortYoungestFirst, .live = count};
    if (!SortsByPlace(compiled))
    {
        return true;
    }
    if (scratchBytes < (int64_t)count * MVFX_SORT_BYTES_PER_ITEM)
    {
        return false;
    }
    orderOut->indices = SortByPlace(instance, emitter, view, scratch, count);
    return true;
}

int32_t mvfxOrderAt(const mvfxRecordOrder* order, int64_t r)
{
    return order->indices != nullptr ? (int32_t)order->indices[r]
           : order->reverse          ? order->live - 1 - (int32_t)r
                                     : (int32_t)r;
}

mvfxResult mvfxInstance_GetSortScratchBytes(mvfxContext* context, mvfxInstanceId instance,
                                            int32_t emitter, int64_t* bytesOut)
{
    if (context == nullptr || bytesOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *bytesOut = 0;
    mvfxInstance* live = nullptr;
    mvfxResult found = mvfxFindEmitter(context, instance, emitter, &live);
    if (found != mvfx_success)
    {
        return found;
    }
    const mvfxCompiledEmitter* compiled = &live->effect->emitters[emitter];
    *bytesOut = NeedsScratch(compiled)
                    ? (int64_t)live->emitters[emitter].capacity * MVFX_SORT_BYTES_PER_ITEM
                    : 0;
    return mvfx_success;
}

bool mvfxIsBufferValid(const void* buffer, int64_t bytes, uintptr_t alignment)
{
    return bytes >= 0 && (bytes == 0 || buffer != nullptr) && ((uintptr_t)buffer % alignment) == 0;
}

mvfxResult mvfxInstance_WriteParticles(mvfxContext* context, mvfxInstanceId instance,
                                       int32_t emitter, mvfxLayoutId layout, const mvfxView* view,
                                       void* scratch, int64_t scratchBytes, void* records,
                                       int64_t capacity, int64_t* countOut)
{
    if (context == nullptr || view == nullptr || countOut == nullptr)
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
    if (capacity < 0 || (capacity > 0 && records == nullptr) || !mvfxIsViewValid(view) ||
        !mvfxIsBufferValid(scratch, scratchBytes, 4))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    int32_t count = live->emitters[emitter].count;
    mvfxRecordOrder order = {0};
    if (!mvfxOrderRecords(live, emitter, view, scratch, scratchBytes, &order))
    {
        return mvfx_errorCapacity;
    }
    *countOut = count;
    mvfxWriteRecords(live, emitter, format, view->origin, &order, records,
                     count < capacity ? count : capacity);
    return mvfx_success;
}

// The light particle i carries for a view.
static mvfxLight LightOf(const mvfxRecordSource* s, const mvfxLightDef* def, int32_t i)
{
    float p[3];
    double world[3];
    VectorOf(s, mvfx_attributePositionX, i, p);
    mvfxUnmapPoint(&s->map, p, world);
    float color[4];
    float size[3];
    mvfxColorOf(s->instance, s->emitter, i, color);
    mvfxSizeOf(s->instance, s->emitter, i, size);
    float largest = fmaxf(fabsf(size[0]), fmaxf(fabsf(size[1]), fabsf(size[2]))) / s->map.sigma;
    float scale = def->intensity * (def->alphaScales ? color[3] : 1.0f);
    mvfxLight light = {.radius = def->radius + def->radiusBySize * largest,
                       .serial = WordOf(s, mvfx_attributeSerial, i)};
    for (int32_t k = 0; k < 3; ++k)
    {
        light.position[k] = (float)(world[k] - s->viewOrigin[k]);
        light.color[k] = color[k] * scale;
    }
    return light;
}

mvfxResult mvfxInstance_WriteLights(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                    const mvfxView* view, mvfxLight* lights, int64_t capacity,
                                    int64_t* countOut)
{
    if (context == nullptr || view == nullptr || countOut == nullptr)
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
    if (capacity < 0 || (capacity > 0 && lights == nullptr) || !mvfxIsViewValid(view))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    const mvfxLightDef* def = &live->effect->emitters[emitter].light;
    if (!def->enabled)
    {
        return mvfx_success;
    }
    mvfxRecordSource s = {
        .instance = live,
        .emitter = emitter,
        .plan = &live->effect->emitters[emitter].plan,
        .map = mvfxSpaceMapAt(live, emitter, mvfx_spaceWorld, &live->to),
        .viewOrigin = view->origin,
    };
    int64_t count = 0;
    for (int32_t i = 0; i < live->emitters[emitter].count; ++i)
    {
        if (WordOf(&s, mvfx_attributeSerial, i) % (uint32_t)def->every != 0)
        {
            continue;
        }
        if (count < capacity)
        {
            lights[count] = LightOf(&s, def, i);
        }
        ++count;
    }
    *countOut = count;
    return mvfx_success;
}
