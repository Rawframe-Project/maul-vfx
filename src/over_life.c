// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "over_life.h"

#include "maul-vfx/shape.h"

#include <math.h>

static bool IsInputValid(mvfxOverLifeInput input, const float range[2])
{
    return input == mvfx_overAge ||
           (input == mvfx_overSpeed && fabsf(range[0]) <= MVFX_MAX_EXTENT &&
            fabsf(range[1]) <= MVFX_MAX_EXTENT && range[0] < range[1]);
}

static bool IsWhite(const mvfxGradient* gradient)
{
    return gradient->colorKeyCount == 0 && gradient->alphaKeyCount == 0;
}

static bool IsColorValid(const mvfxColorOverLife* color)
{
    return !color->enabled ||
           (IsInputValid(color->input, color->speedRange) &&
            (IsWhite(&color->gradient) || mvfxIsGradientValid(&color->gradient)) &&
            (!color->scaleAlpha || mvfxIsLifeCurveValid(&color->alpha)));
}

static bool IsSizeValid(const mvfxSizeOverLife* size)
{
    if (!size->enabled)
    {
        return true;
    }
    int32_t axes = size->perAxis ? 3 : 1;
    for (int32_t k = 0; k < axes; ++k)
    {
        if (!mvfxIsLifeCurveValid(&size->size[k]))
        {
            return false;
        }
    }
    return IsInputValid(size->input, size->speedRange);
}

static bool IsFrameValid(const mvfxFrameOverLife* frame)
{
    switch (frame->animation)
    {
    case mvfx_frameStill:
        return true;
    case mvfx_frameOverLife:
        return frame->frameCount >= 1 && frame->frameCount <= MVFX_MAX_FRAMES &&
               frame->cycles > 0.0f && frame->cycles <= 1000.0f;
    case mvfx_frameAtRate:
        return frame->frameCount >= 1 && frame->frameCount <= MVFX_MAX_FRAMES &&
               frame->frameRate > 0.0f && frame->frameRate <= 10000.0f;
    default:
        return false;
    }
}

bool mvfxIsOverLifeValid(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                         int32_t customCount)
{
    for (int32_t i = 0; i < customCount; ++i)
    {
        if (custom[i].scaledOverLife && !mvfxIsLifeCurveValid(&custom[i].overLife))
        {
            return false;
        }
    }
    return IsColorValid(&overLife->color) && IsSizeValid(&overLife->size) &&
           IsFrameValid(&overLife->frame) &&
           (!overLife->rotation.enabled || mvfxIsLifeCurveValid(&overLife->rotation.scale));
}

void mvfxOverLifeTables(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                        int32_t customCount, int32_t* curves, int32_t* gradients)
{
    const mvfxColorOverLife* color = &overLife->color;
    if (color->enabled)
    {
        *gradients += IsWhite(&color->gradient) ? 0 : 1;
        *curves += color->scaleAlpha ? mvfxLifeCurves(&color->alpha) : 0;
    }
    const mvfxSizeOverLife* size = &overLife->size;
    for (int32_t k = 0; size->enabled && k < (size->perAxis ? 3 : 1); ++k)
    {
        *curves += mvfxLifeCurves(&size->size[k]);
    }
    *curves += overLife->rotation.enabled ? mvfxLifeCurves(&overLife->rotation.scale) : 0;
    for (int32_t i = 0; i < customCount; ++i)
    {
        *curves += custom[i].scaledOverLife ? mvfxLifeCurves(&custom[i].overLife) : 0;
    }
}

static void CompileColor(const mvfxColorOverLife* color, mvfxTableSpace* space,
                         mvfxCompiledOverLife* out)
{
    out->color = color->enabled;
    out->colorInput = color->input;
    out->colorRange[0] = color->speedRange[0];
    out->colorRange[1] = color->speedRange[1];
    out->alpha = (mvfxCompiledLife){.value = 1.0f};
    if (!color->enabled)
    {
        return;
    }
    if (!IsWhite(&color->gradient))
    {
        mvfxGradientTable* table = &space->gradients[space->nextGradient];
        space->nextGradient += 1;
        mvfxBakeGradient(&color->gradient, table);
        out->gradient = table;
    }
    out->scaleAlpha = color->scaleAlpha;
    if (color->scaleAlpha)
    {
        mvfxCompileLife(&color->alpha, space, &out->alpha);
    }
}

void mvfxCompileOverLife(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                         int32_t customCount, mvfxTableSpace* space, mvfxCompiledOverLife* out)
{
    *out = (mvfxCompiledOverLife){0};
    CompileColor(&overLife->color, space, out);
    const mvfxSizeOverLife* size = &overLife->size;
    out->size = size->enabled;
    out->sizeInput = size->input;
    out->sizeRange[0] = size->speedRange[0];
    out->sizeRange[1] = size->speedRange[1];
    out->perAxis = size->perAxis;
    for (int32_t k = 0; size->enabled && k < (size->perAxis ? 3 : 1); ++k)
    {
        mvfxCompileLife(&size->size[k], space, &out->sizes[k]);
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        out->sizeBounds[k] =
            size->enabled ? mvfxLifeBound(&out->sizes[size->perAxis ? k : 0]) : 1.0f;
    }
    out->sizeMargin = size->enabled ? 1.0f + 0x1p-20f : 1.0f;
    out->frame = overLife->frame;
    out->rotation = overLife->rotation.enabled;
    out->rotationScale = (mvfxCompiledLife){.value = 1.0f};
    if (out->rotation)
    {
        mvfxCompileLife(&overLife->rotation.scale, space, &out->rotationScale);
    }
    for (int32_t i = 0; i < customCount; ++i)
    {
        out->customScaled[i] = custom[i].scaledOverLife;
        if (custom[i].scaledOverLife)
        {
            mvfxCompileLife(&custom[i].overLife, space, &out->custom[i]);
        }
    }
}
