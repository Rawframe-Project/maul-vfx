// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "layout.h"

#include "elementary.h"
#include "half.h"

#include <math.h>
#include <string.h>

static uint32_t FormatBytes(mvfxFormat format)
{
    switch (format)
    {
    case mvfx_formatFloat16:
    case mvfx_formatUnorm16:
    case mvfx_formatSnorm16:
        return 2;
    case mvfx_formatUnorm8:
    case mvfx_formatSnorm8:
        return 1;
    default:
        return 4;
    }
}

// The components a field may have, from low to high.
static void ComponentRange(mvfxField field, uint8_t* low, uint8_t* high)
{
    static const uint8_t lows[22] = {1, 1, 1, 1, 4, 1, 1, 1, 1, 1, 1,
                                     1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1};
    static const uint8_t highs[22] = {3, 3, 4, 1, 4, 3, 1, 1, 4, 1, 1,
                                      3, 1, 1, 1, 1, 3, 3, 2, 1, 3, 3};
    *low = lows[field];
    *high = highs[field];
}

static bool IsFieldValid(const mvfxLayoutField* f, uint32_t stride)
{
    if (f->field > mvfx_recordNeighborVelocity || f->format > mvfx_formatUint32)
    {
        return false;
    }
    uint8_t low = 0;
    uint8_t high = 0;
    ComponentRange(f->field, &low, &high);
    // A frame, a mesh index or a ribbon as a word or a float, a serial as a
    // word, nothing else as one.
    bool word = f->format == mvfx_formatUint32;
    bool formatFits = f->field == mvfx_recordSerial ? word
                      : (f->field == mvfx_recordFrame || f->field == mvfx_recordMesh ||
                         f->field == mvfx_recordRibbon || f->field == mvfx_recordNeighborCount)
                          ? word || f->format == mvfx_formatFloat32
                          : !word;
    uint32_t bytes = FormatBytes(f->format);
    return formatFits && f->components >= low && f->components <= high &&
           (f->field != mvfx_recordCustom || f->custom < MVFX_MAX_CUSTOM_ATTRIBUTES) &&
           (!f->srgb || (f->field == mvfx_recordColor && f->format == mvfx_formatUnorm8)) &&
           f->offset % bytes == 0 && f->offset <= stride &&
           (uint64_t)f->offset + (uint64_t)bytes * f->components <= stride;
}

bool mvfxIsLayoutDefValid(const mvfxLayoutDef* def)
{
    if (def == nullptr || def->cookie != MVFX_LAYOUT_DEF_COOKIE || def->fields == nullptr ||
        def->fieldCount < 1 || def->fieldCount > MVFX_MAX_LAYOUT_FIELDS || def->stride < 1 ||
        def->stride > MVFX_MAX_LAYOUT_STRIDE)
    {
        return false;
    }
    for (int32_t i = 0; i < def->fieldCount; ++i)
    {
        if (!IsFieldValid(&def->fields[i], def->stride))
        {
            return false;
        }
    }
    // No two fields share a byte.
    for (int32_t i = 0; i < def->fieldCount; ++i)
    {
        const mvfxLayoutField* a = &def->fields[i];
        uint32_t aEnd = a->offset + FormatBytes(a->format) * a->components;
        for (int32_t j = i + 1; j < def->fieldCount; ++j)
        {
            const mvfxLayoutField* b = &def->fields[j];
            uint32_t bEnd = b->offset + FormatBytes(b->format) * b->components;
            if (a->offset < bEnd && b->offset < aEnd)
            {
                return false;
            }
        }
    }
    return true;
}

mvfxOutputLayout* mvfxBuildLayout(const mvfxLayoutDef* def, const mvfxAllocator* allocator)
{
    mvfxOutputLayout* layout =
        mvfxAllocate(allocator, sizeof(mvfxOutputLayout), alignof(mvfxOutputLayout));
    if (layout == nullptr)
    {
        return nullptr;
    }
    *layout = (mvfxOutputLayout){.fieldCount = def->fieldCount, .stride = def->stride};
    for (int32_t i = 0; i < def->fieldCount; ++i)
    {
        layout->fields[i] = def->fields[i];
    }
    return layout;
}

void mvfxReleaseLayout(mvfxOutputLayout* layout, const mvfxAllocator* allocator)
{
    mvfxRelease(allocator, layout, sizeof(mvfxOutputLayout), alignof(mvfxOutputLayout));
}

// A linear colour channel encoded to sRGB, from 0 to 1.
static float ToSrgb(float linear)
{
    float x = fminf(fmaxf(linear, 0.0f), 1.0f);
    return x <= 0.0031308f ? 12.92f * x : 1.055f * mvfxPow(x, 1.0f / 2.4f) - 0.055f;
}

// A value clamped to 0 to 1 (NaN to 0) in steps of 1 / top, rounded half
// up.
static uint32_t Unorm(float value, float top)
{
    float x = value >= 0.0f ? fminf(value, 1.0f) : 0.0f;
    return (uint32_t)floorf(x * top + 0.5f);
}

// A value clamped to -1 to 1 (NaN to 0) in steps of 1 / top, rounded half
// away from 0, as a two's complement word.
static int32_t Snorm(float value, float top)
{
    float x = value >= -1.0f ? fminf(value, 1.0f) : (value < -1.0f ? -1.0f : 0.0f);
    float scaled = x * top;
    return (int32_t)copysignf(floorf(fabsf(scaled) + 0.5f), scaled);
}

static void WriteComponent(const mvfxLayoutField* f, int32_t k, float value, uint32_t word,
                           unsigned char* at)
{
    uint8_t byte = 0;
    uint16_t half = 0;
    switch (f->format)
    {
    case mvfx_formatFloat32:
        memcpy(at, &value, 4);
        break;
    case mvfx_formatFloat16:
        half = mvfxHalfFromFloat(value);
        memcpy(at, &half, 2);
        break;
    case mvfx_formatUnorm8:
        byte = (uint8_t)Unorm(f->srgb && k < 3 ? ToSrgb(value) : value, 255.0f);
        memcpy(at, &byte, 1);
        break;
    case mvfx_formatSnorm8:
        byte = (uint8_t)(int8_t)Snorm(value, 127.0f);
        memcpy(at, &byte, 1);
        break;
    case mvfx_formatUnorm16:
        half = (uint16_t)Unorm(value, 65535.0f);
        memcpy(at, &half, 2);
        break;
    case mvfx_formatSnorm16:
        half = (uint16_t)(int16_t)Snorm(value, 32767.0f);
        memcpy(at, &half, 2);
        break;
    default:
        memcpy(at, &word, 4);
        break;
    }
}

void mvfxWriteField(const mvfxLayoutField* field, const float values[4], const uint32_t words[4],
                    unsigned char* record)
{
    uint32_t bytes = FormatBytes(field->format);
    for (int32_t k = 0; k < field->components; ++k)
    {
        WriteComponent(field, k, values[k], words[k], record + field->offset + k * bytes);
    }
}
