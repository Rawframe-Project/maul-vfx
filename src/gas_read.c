// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas read out: a field over a box of cells in a format, and a field
// at points in the world.

#include "context.h"
#include "fpenv.h"
#include "gas_api.h"
#include "gas_lattice.h"
#include "instance.h"
#include "instance_api.h"
#include "layout.h"
#include "read_format.h"

#include "maul-vfx/output.h"

#include <math.h>

// A field's value at a cell: a scalar's (a surface's distance its
// first), or a velocity component's mean of the two faces about the
// center.
static float CellValue(const mvfxCompiledGas* gas, const mvfxGasState* state, mvfxGasField field,
                       const int32_t at[3])
{
    const int32_t* n = gas->def.size;
    if (field < mvfx_gasVelocityX || field == mvfx_gasDistance)
    {
        int64_t i = ((int64_t)at[2] * n[1] + at[1]) * n[0] + at[0];
        return state->scalars[field == mvfx_gasDistance ? 0 : field][state->current][i];
    }
    int32_t k = field - mvfx_gasVelocityX;
    if (n[k] == 1)
    {
        return 0.0f;
    }
    const int32_t m[3] = {n[0] + (k == 0 ? 1 : 0), n[1] + (k == 1 ? 1 : 0),
                          n[2] + (k == 2 ? 1 : 0)};
    const int64_t step[3] = {1, m[0], (int64_t)m[0] * m[1]};
    int64_t low = ((int64_t)at[2] * m[1] + at[1]) * m[0] + at[0];
    const float* faces = state->velocity[k][state->current];
    return 0.5f * (faces[low] + faces[low + step[k]]);
}

// Whether a read is in range for a gas, its pitches resolved and the
// bytes it reaches into end.
static bool IsReadValid(const mvfxGasRead* read, const mvfxCompiledGas* gas, int64_t pitch[2],
                        int64_t* end)
{
    const int32_t* n = gas->def.size;
    uint32_t bytes = mvfxReadFormatBytes(read->format);
    bool valid = mvfxIsGasFieldOf(gas, read->field) && bytes > 0;
    bool normalized = mvfxIsReadNormalized(read->format);
    valid = valid && (!normalized ||
                      (isfinite(read->low) && isfinite(read->high) && read->high > read->low));
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid = read->first[k] >= 0 && read->size[k] >= 1 && read->size[k] <= n[k] - read->first[k];
    }
    if (!valid)
    {
        return false;
    }
    int64_t row = (int64_t)read->size[0] * bytes;
    pitch[0] = read->rowBytes == 0 ? row : (int64_t)read->rowBytes;
    int64_t layer = pitch[0] * read->size[1];
    pitch[1] = read->layerBytes == 0 ? layer : (int64_t)read->layerBytes;
    *end = pitch[1] * (read->size[2] - 1) + pitch[0] * (read->size[1] - 1) + row;
    return pitch[0] >= row && pitch[1] >= layer;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_ReadGasAsWork(const mvfxContext* context,
                                                           mvfxInstanceId instance, int32_t gas,
                                                           const mvfxGasRead* read, void* out,
                                                           int64_t bytes)
{
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = mvfxFindGas(context, instance, gas, &result);
    if (live == nullptr)
    {
        return result;
    }
    const mvfxCompiledGas* compiled = &live->effect->gases[gas];
    int64_t pitch[2] = {0, 0};
    int64_t end = 0;
    if (read == nullptr || out == nullptr || !IsReadValid(read, compiled, pitch, &end) ||
        bytes < end)
    {
        mvfxCountMisuse((mvfxContext*)context);
        return mvfx_errorInvalid;
    }
    if (live->planned)
    {
        // Its fields are the simulation's.
        return mvfx_errorUnsupported;
    }
    const mvfxGasState* state = &live->gases[gas];
    const mvfxLayoutField field = {.format = read->format, .components = 1};
    uint32_t size = mvfxReadFormatBytes(read->format);
    static const uint32_t words[4] = {0, 0, 0, 0};
    for (int32_t z = 0; z < read->size[2]; ++z)
    {
        for (int32_t y = 0; y < read->size[1]; ++y)
        {
            unsigned char* row = (unsigned char*)out + pitch[1] * z + pitch[0] * y;
            for (int32_t x = 0; x < read->size[0]; ++x)
            {
                const int32_t at[3] = {read->first[0] + x, read->first[1] + y, read->first[2] + z};
                const float values[4] = {
                    mvfxReadMapped(read->format, read->low, read->high,
                                   CellValue(compiled, state, read->field, at)),
                    0.0f, 0.0f, 0.0f};
                mvfxWriteField(&field, values, words, row + (size_t)x * size);
            }
        }
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_ReadGasAs(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                                  const mvfxGasRead* read, void* out, int64_t bytes)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result = mvfxInstance_ReadGasAsWork(context, instance, gas, read, out, bytes);
    mvfxFloatEnvLeave(env);
    return result;
}

// A field at a place in the box's frame, 0 outside the box (a surface's
// distance its reach), a 2D gas's reaching along its Z without end.
static float SampleAt(const mvfxCompiledGas* gas, const mvfxGasState* state, mvfxGasField field,
                      const float local[3])
{
    const mvfxGasLattice l = {.n = {gas->def.size[0], gas->def.size[1], gas->def.size[2]},
                              .cell = gas->def.cellSize};
    bool distance = field == mvfx_gasDistance;
    float p[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = local[k] / l.cell + 0.5f * (float)l.n[k];
        if (l.n[k] > 1 && !(p[k] >= 0.0f && p[k] <= (float)l.n[k]))
        {
            return distance ? mvfxSurfaceReach(gas) : 0.0f;
        }
    }
    if (field < mvfx_gasVelocityX || distance)
    {
        return mvfxGasSample(state->scalars[distance ? 0 : field][state->current], &l, -1, p);
    }
    int32_t k = field - mvfx_gasVelocityX;
    return l.n[k] > 1 ? mvfxGasSample(state->velocity[k][state->current], &l, k, p) : 0.0f;
}

static MVFX_NOINLINE mvfxResult mvfxInstance_SampleGasWork(const mvfxContext* context,
                                                           mvfxInstanceId instance, int32_t gas,
                                                           mvfxGasField field, const double* points,
                                                           int64_t count, float* out)
{
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = mvfxFindGas(context, instance, gas, &result);
    if (live == nullptr)
    {
        return result;
    }
    const mvfxCompiledGas* compiled = &live->effect->gases[gas];
    bool valid = mvfxIsGasFieldOf(compiled, field) && count >= 0 && count <= INT64_MAX / 3 &&
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
    if (live->planned)
    {
        return mvfx_errorUnsupported;
    }
    const mvfxGasState* state = &live->gases[gas];
    mvfxSpaceMap map = mvfxGasMap(live, gas, mvfx_spaceWorld);
    for (int64_t i = 0; i < count; ++i)
    {
        float local[3];
        mvfxMapPointDouble(&map, &points[3 * i], local);
        out[i] = SampleAt(compiled, state, field, local);
    }
    return mvfx_success;
}

mvfxResult mvfxInstance_SampleGas(const mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                                  mvfxGasField field, const double* points, int64_t count,
                                  float* out)
{
    uint64_t env = mvfxFloatEnvEnter();
    mvfxResult result =
        mvfxInstance_SampleGasWork(context, instance, gas, field, points, count, out);
    mvfxFloatEnvLeave(env);
    return result;
}
