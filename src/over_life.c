// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "over_life.h"

#include "refusal.h"

#include "maul-vfx/shape.h"

#include <math.h>

// An input's status, naming the field of inputTag or rangeTag below at.
static mvfxResult CheckInput(const mvfxFieldPath* at, uint32_t inputTag, uint32_t rangeTag,
                             mvfxOverLifeInput input, const float range[2])
{
    if (input == mvfx_overAge)
    {
        return mvfx_success;
    }
    if (input != mvfx_overSpeed)
    {
        return mvfxRefuseField(at, inputTag, mvfx_errorInvalid);
    }
    if (!(range[0] < range[1]))
    {
        return mvfxRefuseField(at, rangeTag, mvfx_errorInvalid);
    }
    mvfxResult low =
        mvfxRefuseItem(at, rangeTag, 0, mvfxBounded(range[0], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT));
    return low != mvfx_success
               ? low
               : mvfxRefuseItem(at, rangeTag, 1,
                                mvfxBounded(range[1], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT));
}

static bool IsWhite(const mvfxGradient* gradient)
{
    return gradient->colorKeyCount == 0 && gradient->alphaKeyCount == 0;
}

static mvfxResult CheckColor(const mvfxFieldPath* at, const mvfxColorOverLife* color)
{
    if (!color->enable)
    {
        return mvfx_success;
    }
    mvfxResult input =
        CheckInput(at, MVFX_TAG_COLOR_OVER_LIFE_INPUT, MVFX_TAG_COLOR_OVER_LIFE_SPEED_RANGE,
                   color->input, color->speedRange);
    if (input != mvfx_success)
    {
        return input;
    }
    mvfxFieldPath gradientAt = mvfxFieldIn(at, MVFX_TAG_COLOR_OVER_LIFE_GRADIENT, -1);
    mvfxResult gradient =
        IsWhite(&color->gradient) ? mvfx_success : mvfxCheckGradient(&gradientAt, &color->gradient);
    if (gradient != mvfx_success)
    {
        return gradient;
    }
    mvfxFieldPath alpha = mvfxFieldIn(at, MVFX_TAG_COLOR_OVER_LIFE_ALPHA, -1);
    return color->scaleAlpha ? mvfxCheckLifeCurve(&alpha, &color->alpha) : mvfx_success;
}

static mvfxResult CheckSize(const mvfxFieldPath* at, const mvfxSizeOverLife* size)
{
    if (!size->enable)
    {
        return mvfx_success;
    }
    int32_t axes = size->perAxis ? 3 : 1;
    for (int32_t k = 0; k < axes; ++k)
    {
        mvfxFieldPath axis = mvfxFieldIn(at, MVFX_TAG_SIZE_OVER_LIFE_SIZE, k);
        mvfxResult life = mvfxCheckLifeCurve(&axis, &size->size[k]);
        if (life != mvfx_success)
        {
            return life;
        }
    }
    return CheckInput(at, MVFX_TAG_SIZE_OVER_LIFE_INPUT, MVFX_TAG_SIZE_OVER_LIFE_SPEED_RANGE,
                      size->input, size->speedRange);
}

static mvfxResult CheckFrame(const mvfxFieldPath* at, const mvfxFrameOverLife* frame)
{
    switch (frame->animation)
    {
    case mvfx_frameStill:
        return mvfx_success;
    case mvfx_frameOverLife:
        if (!(frame->cycles > 0.0f && frame->cycles <= 1000.0f))
        {
            return mvfxRefuseField(at, MVFX_TAG_FRAME_OVER_LIFE_CYCLES, mvfx_errorInvalid);
        }
        return mvfxRefuseField(at, MVFX_TAG_FRAME_OVER_LIFE_FRAME_COUNT,
                               mvfxCountUpTo(frame->frameCount, 1, MVFX_MAX_FRAMES));
    case mvfx_frameAtRate:
        if (!(frame->frameRate > 0.0f && frame->frameRate <= 10000.0f))
        {
            return mvfxRefuseField(at, MVFX_TAG_FRAME_OVER_LIFE_FRAME_RATE, mvfx_errorInvalid);
        }
        return mvfxRefuseField(at, MVFX_TAG_FRAME_OVER_LIFE_FRAME_COUNT,
                               mvfxCountUpTo(frame->frameCount, 1, MVFX_MAX_FRAMES));
    default:
        return mvfxRefuseField(at, MVFX_TAG_FRAME_OVER_LIFE_ANIMATION, mvfx_errorInvalid);
    }
}

mvfxResult mvfxCheckOverLife(const mvfxFieldPath* emitter, const mvfxOverLife* overLife,
                             const mvfxCustomAttribute* custom, int32_t customCount)
{
    for (int32_t i = 0; i < customCount; ++i)
    {
        mvfxFieldPath item = mvfxFieldIn(emitter, MVFX_TAG_EMITTER_DEF_CUSTOM, i);
        mvfxFieldPath scale = mvfxFieldIn(&item, MVFX_TAG_CUSTOM_ATTRIBUTE_OVER_LIFE, -1);
        mvfxResult life = custom[i].scaledOverLife ? mvfxCheckLifeCurve(&scale, &custom[i].overLife)
                                                   : mvfx_success;
        if (life != mvfx_success)
        {
            return life;
        }
    }
    mvfxFieldPath at = mvfxFieldIn(emitter, MVFX_TAG_EMITTER_DEF_OVER_LIFE, -1);
    mvfxFieldPath colorAt = mvfxFieldIn(&at, MVFX_TAG_OVER_LIFE_COLOR, -1);
    mvfxResult color = CheckColor(&colorAt, &overLife->color);
    if (color != mvfx_success)
    {
        return color;
    }
    mvfxFieldPath sizeAt = mvfxFieldIn(&at, MVFX_TAG_OVER_LIFE_SIZE, -1);
    mvfxResult size = CheckSize(&sizeAt, &overLife->size);
    if (size != mvfx_success)
    {
        return size;
    }
    mvfxFieldPath frameAt = mvfxFieldIn(&at, MVFX_TAG_OVER_LIFE_FRAME, -1);
    mvfxResult frame = CheckFrame(&frameAt, &overLife->frame);
    if (frame != mvfx_success)
    {
        return frame;
    }
    mvfxFieldPath rotation = mvfxFieldIn(&at, MVFX_TAG_OVER_LIFE_ROTATION, -1);
    mvfxFieldPath scale = mvfxFieldIn(&rotation, MVFX_TAG_ROTATION_OVER_LIFE_SCALE, -1);
    return overLife->rotation.enable ? mvfxCheckLifeCurve(&scale, &overLife->rotation.scale)
                                     : mvfx_success;
}

// A role's limit over the most a value over life scales it by, at most
// MVFX_MAX_DRAW.
static float CapOf(float limit, double scale)
{
    return scale * (double)MVFX_MAX_DRAW > (double)limit ? (float)((double)limit / scale)
                                                         : MVFX_MAX_DRAW;
}

void mvfxStoredCapsOf(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                      int32_t customCount, mvfxStoredCaps* out)
{
    const mvfxSizeOverLife* size = &overLife->size;
    for (int32_t k = 0; k < 3; ++k)
    {
        double scale =
            size->enable ? mvfxLifeCurveLargest(&size->size[size->perAxis ? k : 0]) : 1.0;
        out->size[k] = CapOf(MVFX_MAX_EXTENT, scale);
    }
    const mvfxColorOverLife* color = &overLife->color;
    double tint = 1.0;
    double alpha = 1.0;
    if (color->enable && !IsWhite(&color->gradient))
    {
        mvfxGradientLargest(&color->gradient, &tint, &alpha);
    }
    if (color->enable && color->scaleAlpha)
    {
        alpha *= mvfxLifeCurveLargest(&color->alpha);
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        out->color[k] = CapOf(MVFX_MAX_COLOR, tint);
    }
    out->color[3] = CapOf(MVFX_MAX_COLOR, alpha);
    const mvfxRotationOverLife* rotation = &overLife->rotation;
    out->spin =
        CapOf(MVFX_MAX_SPIN, rotation->enable ? mvfxLifeCurveLargest(&rotation->scale) : 1.0);
    for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
    {
        bool scaled = i < customCount && custom[i].scaledOverLife;
        out->custom[i] =
            CapOf(MVFX_MAX_VALUE, scaled ? mvfxLifeCurveLargest(&custom[i].overLife) : 1.0);
    }
}

void mvfxSourceCapsOf(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                      int32_t customCount, mvfxSourceCaps* out)
{
    mvfxStoredCapsOf(overLife, custom, customCount, &out->stored);
    for (int32_t k = 0; k < 4; ++k)
    {
        out->spin[k] = out->stored.spin;
        out->any[k] = MVFX_MAX_DRAW;
        for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
        {
            out->custom[i][k] = out->stored.custom[i];
        }
    }
}

void mvfxOverLifeTables(const mvfxOverLife* overLife, const mvfxCustomAttribute* custom,
                        int32_t customCount, int32_t* curves, int32_t* gradients)
{
    const mvfxColorOverLife* color = &overLife->color;
    if (color->enable)
    {
        *gradients += IsWhite(&color->gradient) ? 0 : 1;
        *curves += color->scaleAlpha ? mvfxLifeCurves(&color->alpha) : 0;
    }
    const mvfxSizeOverLife* size = &overLife->size;
    for (int32_t k = 0; size->enable && k < (size->perAxis ? 3 : 1); ++k)
    {
        *curves += mvfxLifeCurves(&size->size[k]);
    }
    *curves += overLife->rotation.enable ? mvfxLifeCurves(&overLife->rotation.scale) : 0;
    for (int32_t i = 0; i < customCount; ++i)
    {
        *curves += custom[i].scaledOverLife ? mvfxLifeCurves(&custom[i].overLife) : 0;
    }
}

static void CompileColor(const mvfxColorOverLife* color, mvfxTableSpace* space,
                         mvfxCompiledOverLife* out)
{
    out->color = color->enable;
    out->colorInput = color->input;
    out->colorRange[0] = color->speedRange[0];
    out->colorRange[1] = color->speedRange[1];
    out->alpha = (mvfxCompiledLife){.value = 1.0f};
    if (!color->enable)
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
    out->size = size->enable;
    out->sizeInput = size->input;
    out->sizeRange[0] = size->speedRange[0];
    out->sizeRange[1] = size->speedRange[1];
    out->perAxis = size->perAxis;
    for (int32_t k = 0; size->enable && k < (size->perAxis ? 3 : 1); ++k)
    {
        mvfxCompileLife(&size->size[k], space, &out->sizes[k]);
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        out->sizeBounds[k] =
            size->enable ? mvfxLifeBound(&out->sizes[size->perAxis ? k : 0]) : 1.0f;
    }
    out->sizeMargin = size->enable ? 1.0f + 0x1p-20f : 1.0f;
    out->frame = overLife->frame;
    out->rotation = overLife->rotation.enable;
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
    mvfxStoredCapsOf(overLife, custom, customCount, &out->caps);
}
