// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A water surface read out, sampled at points, pushed and described.

#include "water_api.h"

#include "context.h"
#include "force.h"
#include "instance_api.h"
#include "layout.h"
#include "read_format.h"
#include "water.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

// The live instance an id names, with a water surface of that index: NULL
// and result stale for an id naming none, invalid (misuse counted) for an
// index out of range or a NULL context.
static const mvfxInstance* FindWater(const mvfxContext* context, mvfxInstanceId instance,
                                     int32_t water, mvfxResult* result)
{
    *result = mvfx_errorInvalid;
    if (context == nullptr)
    {
        return nullptr;
    }
    const mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        *result = mvfx_errorStale;
        return nullptr;
    }
    if (water < 0 || water >= live->effect->waterCount)
    {
        mvfxCountMisuse((mvfxContext*)context);
        return nullptr;
    }
    *result = mvfx_success;
    return live;
}

mvfxBoxPlace mvfxWaterPlace(const mvfxInstance* instance, int32_t w)
{
    const mvfxCompiledWater* water = &instance->effect->waters[w];
    const mvfxWaterState* state = &instance->waters[w];
    return mvfxPlaceBox(instance, water->def.space == mvfx_gasLocal, water->def.center,
                        state->origin, state->rotation, state->scale);
}

mvfxBounds mvfxWaterBounds(const mvfxInstance* instance, int32_t w)
{
    mvfxBoxPlace place = mvfxWaterPlace(instance, w);
    const mvfxWaterDef* def = &instance->effect->waters[w].def;
    float cell = place.scale * def->cellSize;
    const float half[3] = {0.5f * cell * (float)def->size[0],
                           place.scale * instance->waters[w].reach,
                           0.5f * cell * (float)def->size[1]};
    return mvfxBoxBounds(&place, half);
}

// The surface's height at cell (x, z), within the grid, over the grid's
// center: waves' displacement, or shallow water's ground and depth.
static float SurfaceAt(const mvfxCompiledWater* water, const mvfxWaterState* state, int32_t x,
                       int32_t z)
{
    int64_t i = (int64_t)z * water->def.size[0] + x;
    float u = state->heights[state->current][i];
    return state->ground != nullptr ? state->ground[i] + u : u;
}

// The surface's velocity at cell (x, z), within the grid: a wave's rate
// along Y, or shallow water's flow along X and Z, the mean of the cell's
// two faces' along each.
static void VelocityAt(const mvfxCompiledWater* water, const mvfxWaterState* state, int32_t x,
                       int32_t z, float out[3])
{
    int32_t n0 = water->def.size[0];
    if (state->ground == nullptr)
    {
        out[0] = 0.0f;
        out[1] = state->rates[(int64_t)z * n0 + x];
        out[2] = 0.0f;
        return;
    }
    const float* u = state->faces[0][state->faceCurrent];
    const float* w = state->faces[1][state->faceCurrent];
    int64_t across = (int64_t)z * (n0 + 1) + x;
    int64_t along = (int64_t)z * n0 + x;
    out[0] = 0.5f * (u[across] + u[across + 1]);
    out[1] = 0.0f;
    out[2] = 0.5f * (w[along] + w[along + n0]);
}

// The surface's unit normal at cell (x, z): its heights' central
// differences, one-sided at the grid's edges.
static void NormalAt(const mvfxCompiledWater* water, const mvfxWaterState* state, int32_t x,
                     int32_t z, float out[3])
{
    const int32_t* n = water->def.size;
    float h = water->def.cellSize;
    int32_t x0 = x > 0 ? x - 1 : x;
    int32_t x1 = x + 1 < n[0] ? x + 1 : x;
    int32_t z0 = z > 0 ? z - 1 : z;
    int32_t z1 = z + 1 < n[1] ? z + 1 : z;
    float dx =
        (SurfaceAt(water, state, x1, z) - SurfaceAt(water, state, x0, z)) / ((float)(x1 - x0) * h);
    float dz =
        (SurfaceAt(water, state, x, z1) - SurfaceAt(water, state, x, z0)) / ((float)(z1 - z0) * h);
    float length = sqrtf(dx * dx + 1.0f + dz * dz);
    out[0] = -dx / length;
    out[1] = 1.0f / length;
    out[2] = -dz / length;
}

// A field's value at cell (x, z).
static float FieldAt(const mvfxCompiledWater* water, const mvfxWaterState* state,
                     mvfxWaterField field, int32_t x, int32_t z)
{
    int64_t i = (int64_t)z * water->def.size[0] + x;
    float vector[3];
    switch (field)
    {
    case mvfx_waterHeight:
        return water->def.center[1] + SurfaceAt(water, state, x, z);
    case mvfx_waterVelocityX:
    case mvfx_waterVelocityY:
    case mvfx_waterVelocityZ:
        VelocityAt(water, state, x, z, vector);
        return vector[field - mvfx_waterVelocityX];
    case mvfx_waterNormalX:
    case mvfx_waterNormalY:
    case mvfx_waterNormalZ:
        NormalAt(water, state, x, z, vector);
        return vector[field - mvfx_waterNormalX];
    default:
        // The depth: shallow water's, waves' 0.
        return state->ground != nullptr ? state->heights[state->current][i] : 0.0f;
    }
}

// Whether a read is in range for a surface, its row pitch resolved and
// the bytes it reaches into end.
static bool IsReadValid(const mvfxWaterRead* read, const mvfxCompiledWater* water, int64_t* pitch,
                        int64_t* end)
{
    uint32_t bytes = mvfxReadFormatBytes(read->format);
    bool valid = read->field <= mvfx_waterDepth && bytes > 0 &&
                 (!mvfxIsReadNormalized(read->format) ||
                  (isfinite(read->low) && isfinite(read->high) && read->high > read->low));
    for (int32_t k = 0; valid && k < 2; ++k)
    {
        valid = read->first[k] >= 0 && read->size[k] >= 1 &&
                read->size[k] <= water->def.size[k] - read->first[k];
    }
    if (!valid)
    {
        return false;
    }
    int64_t row = (int64_t)read->size[0] * bytes;
    *pitch = read->rowBytes == 0 ? row : (int64_t)read->rowBytes;
    *end = *pitch * (read->size[1] - 1) + row;
    return *pitch >= row;
}

mvfxResult mvfxInstance_ReadWater(const mvfxContext* context, mvfxInstanceId instance,
                                  int32_t water, const mvfxWaterRead* read, void* out,
                                  int64_t bytes)
{
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = FindWater(context, instance, water, &result);
    if (live == nullptr)
    {
        return result;
    }
    const mvfxCompiledWater* compiled = &live->effect->waters[water];
    int64_t pitch = 0;
    int64_t end = 0;
    if (read == nullptr || out == nullptr || !IsReadValid(read, compiled, &pitch, &end) ||
        bytes < end)
    {
        mvfxCountMisuse((mvfxContext*)context);
        return mvfx_errorInvalid;
    }
    const mvfxWaterState* state = &live->waters[water];
    const mvfxLayoutField field = {.format = read->format, .components = 1};
    uint32_t size = mvfxReadFormatBytes(read->format);
    static const uint32_t words[4] = {0, 0, 0, 0};
    for (int32_t z = 0; z < read->size[1]; ++z)
    {
        unsigned char* row = (unsigned char*)out + pitch * z;
        for (int32_t x = 0; x < read->size[0]; ++x)
        {
            float value =
                FieldAt(compiled, state, read->field, read->first[0] + x, read->first[1] + z);
            const float values[4] = {mvfxReadMapped(read->format, read->low, read->high, value),
                                     0.0f, 0.0f, 0.0f};
            mvfxWriteField(&field, values, words, row + (size_t)x * size);
        }
    }
    return mvfx_success;
}

// The surface at a point of its frame (x, z): its height over the grid's
// center, velocity and normal, bilinear between the cells' centers and
// held at the outer cells' within the grid's edge; whether the point is
// over the grid (if not, none is written).
static bool Under(const mvfxCompiledWater* water, const mvfxWaterState* state, const float local[3],
                  float* height, float velocity[3], float normal[3])
{
    const int32_t* n = water->def.size;
    float h = water->def.cellSize;
    const float p[2] = {local[0] / h + 0.5f * (float)n[0], local[2] / h + 0.5f * (float)n[1]};
    bool inside = p[0] >= 0.0f && p[0] <= (float)n[0] && p[1] >= 0.0f && p[1] <= (float)n[1];
    if (!inside)
    {
        return false;
    }
    float c[2];
    int32_t lo[2];
    float t[2];
    for (int32_t k = 0; k < 2; ++k)
    {
        c[k] = fminf(fmaxf(p[k] - 0.5f, 0.0f), (float)(n[k] - 1));
        lo[k] = (int32_t)c[k] < n[k] - 1 ? (int32_t)c[k] : n[k] - 2;
        t[k] = c[k] - (float)lo[k];
    }
    *height = 0.0f;
    float sum[3] = {0.0f, 0.0f, 0.0f};
    for (int32_t corner = 0; corner < 4; ++corner)
    {
        int32_t x = lo[0] + (corner & 1);
        int32_t z = lo[1] + (corner >> 1);
        float w =
            ((corner & 1) != 0 ? t[0] : 1.0f - t[0]) * ((corner >> 1) != 0 ? t[1] : 1.0f - t[1]);
        float cornerNormal[3];
        float cornerVelocity[3];
        NormalAt(water, state, x, z, cornerNormal);
        VelocityAt(water, state, x, z, cornerVelocity);
        *height += w * SurfaceAt(water, state, x, z);
        for (int32_t k = 0; k < 3; ++k)
        {
            sum[k] += w * cornerNormal[k];
            velocity[k] += w * cornerVelocity[k];
        }
    }
    float length = sqrtf(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
    for (int32_t k = 0; k < 3; ++k)
    {
        normal[k] = sum[k] / length;
    }
    return true;
}

// Samples one point: the surface along the frame's Y through it.
static void SampleOne(const mvfxInstance* live, int32_t water, const mvfxSpaceMap* map,
                      const double point[3], mvfxWaterSample* out)
{
    float local[3];
    mvfxMapPointDouble(map, point, local);
    // Off the grid, the rest plane's, still.
    float height = 0.0f;
    float velocity[3] = {0.0f, 0.0f, 0.0f};
    float normal[3] = {0.0f, 1.0f, 0.0f};
    out->inside =
        Under(&live->effect->waters[water], &live->waters[water], local, &height, velocity, normal);
    const float surface[3] = {local[0], height, local[2]};
    mvfxUnmapPoint(map, surface, out->surface);
    // Directions turn back with the frame; a normal keeps its length.
    float turned[3];
    mvfxUnmapVector(map, normal, turned);
    float length = sqrtf(turned[0] * turned[0] + turned[1] * turned[1] + turned[2] * turned[2]);
    for (int32_t k = 0; k < 3; ++k)
    {
        out->normal[k] = turned[k] / length;
    }
    mvfxUnmapVector(map, velocity, out->velocity);
}

mvfxResult mvfxInstance_SampleWater(const mvfxContext* context, mvfxInstanceId instance,
                                    int32_t water, const double* points, int64_t count,
                                    mvfxWaterSample* out)
{
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = FindWater(context, instance, water, &result);
    if (live == nullptr)
    {
        return result;
    }
    bool valid = count >= 0 && count <= INT64_MAX / 3 &&
                 (count == 0 || (points != nullptr && out != nullptr));
    for (int64_t i = 0; valid && i < 3 * count; ++i)
    {
        valid = isfinite(points[i]);
    }
    if (!valid)
    {
        mvfxCountMisuse((mvfxContext*)context);
        return mvfx_errorInvalid;
    }
    mvfxBoxPlace place = mvfxWaterPlace(live, water);
    mvfxSpaceMap map = mvfxBoxMap(live, &place, mvfx_spaceWorld);
    for (int64_t i = 0; i < count; ++i)
    {
        SampleOne(live, water, &map, &points[3 * i], &out[i]);
    }
    return mvfx_success;
}

// Whether a push is in range.
static bool IsPushValid(const mvfxWaterPush* push)
{
    bool valid = push->radius > 0.0f && push->radius <= MVFX_MAX_EXTENT && isfinite(push->depth) &&
                 fabsf(push->depth) <= MVFX_MAX_EXTENT;
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid = fabs(push->point[k]) <= 0x1p52;
    }
    return valid;
}

mvfxResult mvfxInstance_PushWater(mvfxContext* context, mvfxInstanceId instance, int32_t water,
                                  const mvfxWaterPush* pushes, int32_t count)
{
    mvfxResult result = mvfx_success;
    mvfxInstance* live = (mvfxInstance*)FindWater(context, instance, water, &result);
    if (live == nullptr)
    {
        return result;
    }
    bool valid = count >= 0 && (count == 0 || pushes != nullptr);
    for (int32_t i = 0; valid && i < count; ++i)
    {
        valid = IsPushValid(&pushes[i]);
    }
    if (!valid)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxWaterState* state = &live->waters[water];
    if (count > MVFX_MAX_WATER_PUSHES - state->pushCount)
    {
        return mvfx_errorCapacity;
    }
    if (count > 0)
    {
        memcpy(&state->pushes[state->pushCount], pushes, (size_t)count * sizeof(mvfxWaterPush));
    }
    state->pushCount += count;
    return mvfx_success;
}

mvfxResult mvfxInstance_GetWaterInfo(const mvfxContext* context, mvfxInstanceId instance,
                                     int32_t water, mvfxWaterInfo* infoOut)
{
    if (infoOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse((mvfxContext*)context);
        }
        return mvfx_errorInvalid;
    }
    *infoOut = (mvfxWaterInfo){0};
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = FindWater(context, instance, water, &result);
    if (live == nullptr)
    {
        return result;
    }
    mvfxBoxPlace place = mvfxWaterPlace(live, water);
    for (int32_t k = 0; k < 3; ++k)
    {
        infoOut->center[k] = place.center[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        infoOut->rotation[k] = place.rotation[k];
    }
    infoOut->cellSize = place.scale * live->effect->waters[water].def.cellSize;
    infoOut->size[0] = live->effect->waters[water].def.size[0];
    infoOut->size[1] = live->effect->waters[water].def.size[1];
    infoOut->stepsSlowed = live->waters[water].stepsSlowed;
    infoOut->facesHeld = live->waters[water].facesHeld;
    return mvfx_success;
}
