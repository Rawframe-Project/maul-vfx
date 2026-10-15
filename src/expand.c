// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "expand.h"

#include "appearance.h"
#include "context.h"
#include "direction.h"
#include "elementary.h"
#include "instance_api.h"
#include "layout_api.h"
#include "output.h"
#include "unit.h"

#include <math.h>

// A quad's axes in the world: right across its width, up along its
// height, each unit or 0.
typedef struct Axes
{
    double right[3];
    double up[3];
} Axes;

static void Cross(const double a[3], const double b[3], double out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void Normalize(double v[3])
{
    double length = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int32_t k = 0; k < 3; ++k)
    {
        v[k] = length > 0.0 ? v[k] / length : 0.0;
    }
}

// Axes upright on up and turned about it to face the eye from center.
static Axes Facing(const double up[3], const double center[3], const double eye[3])
{
    Axes axes;
    double toEye[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        axes.up[k] = up[k];
        toEye[k] = eye[k] - center[k];
    }
    Normalize(axes.up);
    Cross(axes.up, toEye, axes.right);
    Normalize(axes.right);
    return axes;
}

// Axes in the view's plane: right is forward crossed with up, up is right
// crossed with forward; turned by angle about the view axis.
static Axes InView(const mvfxView* view, float angle)
{
    Axes axes;
    double forward[3];
    double up[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        forward[k] = (double)view->forward[k];
        up[k] = (double)view->up[k];
    }
    Cross(forward, up, axes.right);
    Normalize(axes.right);
    Cross(axes.right, forward, axes.up);
    Normalize(axes.up);
    float sine = 0.0f;
    float cosine = 1.0f;
    mvfxSinCos(angle, &sine, &cosine);
    for (int32_t k = 0; k < 3; ++k)
    {
        double right = axes.right[k];
        double upright = axes.up[k];
        axes.right[k] = (double)cosine * right + (double)sine * upright;
        axes.up[k] = (double)cosine * upright - (double)sine * right;
    }
    return axes;
}

static float FloatAt(const mvfxRecordSource* source, mvfxAttribute attribute, int32_t i)
{
    return ((const float*)mvfxAttributeArray((mvfxInstance*)source->instance, source->emitter,
                                             attribute))[i];
}

// Axes in the plane of particle i's orientation in the world.
static Axes Oriented(const mvfxRecordSource* source, int32_t i)
{
    float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (mvfxHasAttribute(source->plan, mvfx_attributeOrientationX))
    {
        for (int32_t k = 0; k < 4; ++k)
        {
            q[k] = FloatAt(source, (mvfxAttribute)(mvfx_attributeOrientationX + k), i);
        }
    }
    const float* m = source->map.rotation;
    const float turn[4] = {-m[0], -m[1], -m[2], m[3]};
    float world[4];
    mvfxMultiplyRotations(turn, q, world);
    static const float x[3] = {1.0f, 0.0f, 0.0f};
    static const float y[3] = {0.0f, 1.0f, 0.0f};
    float right[3];
    float up[3];
    mvfxRotate(world, x, right);
    mvfxRotate(world, y, up);
    Axes axes;
    for (int32_t k = 0; k < 3; ++k)
    {
        axes.right[k] = (double)right[k];
        axes.up[k] = (double)up[k];
    }
    return axes;
}

// Particle i's velocity in the world.
static void VelocityOf(const mvfxRecordSource* source, int32_t i, float out[3])
{
    float v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        v[k] = FloatAt(source, (mvfxAttribute)(mvfx_attributeVelocityX + k), i);
    }
    mvfxUnmapVector(&source->map, v, out);
}

// Particle i's quad: its axes by the billboard, and its half width and
// height in the world (a directional one's height stretched).
static void QuadOf(const mvfxRecordSource* source, const mvfxView* view, int32_t i,
                   const double center[3], Axes* axes, double half[2])
{
    const mvfxSpriteDef* sprite = &source->instance->effect->emitters[source->emitter].sprite;
    float size[3];
    mvfxSizeOf(source->instance, source->emitter, i, size);
    half[0] = 0.5 * (double)(size[0] / source->map.sigma);
    half[1] = 0.5 * (double)(size[1] / source->map.sigma);
    static const double worldUp[3] = {0.0, 1.0, 0.0};
    float velocity[3];
    double along[3];
    switch (sprite->billboard)
    {
    case mvfx_billboardRotated:
        *axes = InView(view, mvfxHasAttribute(source->plan, mvfx_attributeAngle)
                                 ? FloatAt(source, mvfx_attributeAngle, i)
                                 : 0.0f);
        break;
    case mvfx_billboardAxisY:
        *axes = Facing(worldUp, center, view->eye);
        break;
    case mvfx_billboardFixed:
        *axes = Oriented(source, i);
        break;
    case mvfx_billboardDirectional:
        VelocityOf(source, i, velocity);
        for (int32_t k = 0; k < 3; ++k)
        {
            along[k] = (double)velocity[k];
        }
        *axes = Facing(along, center, view->eye);
        half[1] += 0.5 * (double)sprite->stretch *
                   sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
        break;
    default:
        *axes = InView(view, 0.0f);
        break;
    }
}

// Writes particle i's quad: four vertices from record on, and the six
// indices of its two triangles from base.
static void WriteQuad(const mvfxRecordSource* source, const mvfxOutputLayout* layout,
                      const mvfxView* view, int32_t i, unsigned char* record, uint32_t* indices,
                      uint32_t base)
{
    double center[3];
    mvfxWorldPlaceOf(source, i, center);
    Axes axes;
    double half[2];
    QuadOf(source, view, i, center, &axes, half);
    for (int32_t c = 0; c < 4; ++c)
    {
        double across = (c & 1) != 0 ? half[0] : -half[0];
        double high = (c & 2) != 0 ? half[1] : -half[1];
        double place[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            place[k] = center[k] + axes.right[k] * across + axes.up[k] * high;
        }
        mvfxRibbonPoint corner = {.width = 1.0f,
                                  .color = {1.0f, 1.0f, 1.0f, 1.0f},
                                  .corner = {(float)(c & 1), (float)((c >> 1) & 1)}};
        mvfxWriteRibbonRecord(source, layout, i, i, 0.0f, place, &corner,
                              record + (int64_t)c * (int64_t)layout->stride);
    }
    static const uint32_t triangles[6] = {0, 1, 2, 2, 1, 3};
    for (int32_t k = 0; k < 6; ++k)
    {
        indices[k] = base + triangles[k];
    }
}

static bool AreBuffersValid(const mvfxExpandBuffers* buffers)
{
    return buffers->vertexCapacity >= 0 &&
           (buffers->vertexCapacity == 0 || buffers->vertices != nullptr) &&
           mvfxIsBufferValid(buffers->indices, buffers->indexCapacity, alignof(uint32_t)) &&
           mvfxIsBufferValid(buffers->scratch, buffers->scratchBytes, 4);
}

mvfxResult mvfxInstance_ExpandSprites(mvfxContext* context, mvfxInstanceId instance,
                                      int32_t emitter, mvfxLayoutId layout, const mvfxView* view,
                                      const mvfxExpandBuffers* buffers, int64_t* countOut)
{
    if (context == nullptr || view == nullptr || buffers == nullptr || countOut == nullptr)
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
    if (!mvfxIsViewValid(view) || !AreBuffersValid(buffers))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxRecordOrder order = {0};
    if (!mvfxOrderRecords(live, emitter, view, buffers->scratch, buffers->scratchBytes, &order))
    {
        return mvfx_errorCapacity;
    }
    int64_t count = live->emitters[emitter].count;
    int64_t fits = buffers->vertexCapacity / 4 < buffers->indexCapacity / 6
                       ? buffers->vertexCapacity / 4
                       : buffers->indexCapacity / 6;
    mvfxRecordSource source = mvfxOpenRecordSource(live, emitter, view->origin);
    for (int64_t r = 0; r < count && r < fits; ++r)
    {
        WriteQuad(&source, format, view, mvfxOrderAt(&order, r),
                  (unsigned char*)buffers->vertices + 4 * r * (int64_t)format->stride,
                  buffers->indices + 6 * r, (uint32_t)(4 * r));
    }
    *countOut = count;
    return mvfx_success;
}

// A sheet's frame at its vertex in row r, place c: the tangent across and
// the side down from its neighbors either way (one-sided at the edges),
// and the face normal their cross product, each unit or 0.
static void SheetFrame(const mvfxRecordSource* source, int32_t first, int32_t width, int32_t rows,
                       int32_t r, int32_t c, mvfxRibbonPoint* out)
{
    double ends[4][3];
    mvfxWorldPlaceOf(source, first + r * width + (c + 1 < width ? c + 1 : c), ends[0]);
    mvfxWorldPlaceOf(source, first + r * width + (c > 0 ? c - 1 : c), ends[1]);
    mvfxWorldPlaceOf(source, first + (r + 1 < rows ? r + 1 : r) * width + c, ends[2]);
    mvfxWorldPlaceOf(source, first + (r > 0 ? r - 1 : r) * width + c, ends[3]);
    double across[3];
    double down[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        across[k] = ends[0][k] - ends[1][k];
        down[k] = ends[2][k] - ends[3][k];
    }
    double normal[3];
    Cross(across, down, normal);
    Normalize(across);
    Normalize(down);
    Normalize(normal);
    for (int32_t k = 0; k < 3; ++k)
    {
        out->tangent[k] = (float)across[k];
        out->side[k] = (float)down[k];
        out->faceNormal[k] = (float)normal[k];
    }
}

// A sheet emitter's torn edges, or NULL for one that does not tear.
static const uint32_t* TornOf(const mvfxRecordSource* source)
{
    const mvfxCompiledEmitter* compiled = &source->instance->effect->emitters[source->emitter];
    return mvfxHasAttribute(&compiled->plan, mvfx_attributeTorn)
               ? mvfxAttributeArray(source->instance, source->emitter, mvfx_attributeTorn)
               : nullptr;
}

// The indices of sheet u's whole quads.
static int64_t IndicesOf(const mvfxRecordSource* source, int32_t u)
{
    const mvfxCompiledEmitter* compiled = &source->instance->effect->emitters[source->emitter];
    int32_t width = compiled->unitWidth;
    int32_t rows = compiled->unitPoints / width;
    int32_t first = u * compiled->unitPoints;
    const uint32_t* torn = TornOf(source);
    int64_t whole = 0;
    for (int32_t r = 0; r + 1 < rows; ++r)
    {
        for (int32_t c = 0; c + 1 < width; ++c)
        {
            whole += mvfxIsQuadWhole(torn, first + r * width + c, width) ? 1 : 0;
        }
    }
    return 6 * whole;
}

// Writes sheet u's vertices from record, and its whole quads' indices from
// those vertices' first, base.
static void WriteSheet(const mvfxRecordSource* source, const mvfxOutputLayout* layout, int32_t u,
                       unsigned char* record, uint32_t* indices, uint32_t base)
{
    const mvfxCompiledEmitter* compiled = &source->instance->effect->emitters[source->emitter];
    int32_t width = compiled->unitWidth;
    int32_t rows = compiled->unitPoints / width;
    int32_t first = u * compiled->unitPoints;
    for (int32_t r = 0; r < rows; ++r)
    {
        for (int32_t c = 0; c < width; ++c)
        {
            mvfxRibbonPoint point = {
                .width = 1.0f,
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
                .ribbon = (uint32_t)u,
                .corner = {(float)c / (float)(width - 1), (float)r / (float)(rows - 1)}};
            SheetFrame(source, first, width, rows, r, c, &point);
            int64_t at = (int64_t)r * width + c;
            mvfxWriteRecord(source, layout, first + (int32_t)at, &point,
                            record + at * (int64_t)layout->stride);
        }
    }
    const uint32_t* torn = TornOf(source);
    for (int32_t r = 0; r + 1 < rows; ++r)
    {
        for (int32_t c = 0; c + 1 < width; ++c)
        {
            if (!mvfxIsQuadWhole(torn, first + r * width + c, width))
            {
                continue;
            }
            uint32_t v = base + (uint32_t)(r * width + c);
            uint32_t below = v + (uint32_t)width;
            const uint32_t quad[6] = {v, v + 1, below, below, v + 1, below + 1};
            for (int32_t k = 0; k < 6; ++k)
            {
                indices[k] = quad[k];
            }
            indices += 6;
        }
    }
}

mvfxResult mvfxInstance_ExpandSheets(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                     mvfxLayoutId layout, const mvfxView* view,
                                     const mvfxExpandBuffers* buffers, int64_t* vertexCountOut,
                                     int64_t* indexCountOut)
{
    if (context == nullptr || view == nullptr || buffers == nullptr || vertexCountOut == nullptr ||
        indexCountOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *vertexCountOut = 0;
    *indexCountOut = 0;
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
    if (!mvfxIsViewValid(view) || !AreBuffersValid(buffers))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    const mvfxCompiledEmitter* compiled = &live->effect->emitters[emitter];
    if (compiled->unit.kind != mvfx_unitSheet)
    {
        return mvfx_success;
    }
    int64_t points = compiled->unitPoints;
    int64_t sheets = live->emitters[emitter].count / points;
    mvfxRecordSource source = mvfxOpenRecordSource(live, emitter, view->origin);
    // The first sheets that fit whole, then the counts of all of them.
    int64_t indices = 0;
    bool fitting = true;
    for (int64_t u = 0; u < sheets; ++u)
    {
        int64_t own = IndicesOf(&source, (int32_t)u);
        fitting = fitting && (u + 1) * points <= buffers->vertexCapacity &&
                  indices + own <= buffers->indexCapacity;
        if (fitting)
        {
            WriteSheet(&source, format, (int32_t)u,
                       (unsigned char*)buffers->vertices + u * points * (int64_t)format->stride,
                       buffers->indices + indices, (uint32_t)(points * u));
        }
        indices += own;
    }
    *vertexCountOut = sheets * points;
    *indexCountOut = indices;
    return mvfx_success;
}
