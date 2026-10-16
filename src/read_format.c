// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "read_format.h"

uint32_t mvfxReadFormatBytes(mvfxFormat format)
{
    switch (format)
    {
    case mvfx_formatFloat32:
        return 4;
    case mvfx_formatFloat16:
    case mvfx_formatUnorm16:
    case mvfx_formatSnorm16:
        return 2;
    case mvfx_formatUnorm8:
    case mvfx_formatSnorm8:
        return 1;
    default:
        return 0;
    }
}

bool mvfxIsReadNormalized(mvfxFormat format)
{
    return format != mvfx_formatFloat32 && format != mvfx_formatFloat16;
}

float mvfxReadMapped(mvfxFormat format, float low, float high, float value)
{
    switch (format)
    {
    case mvfx_formatUnorm8:
    case mvfx_formatUnorm16:
        return (value - low) / (high - low);
    case mvfx_formatSnorm8:
    case mvfx_formatSnorm16:
        return 2.0f * (value - low) / (high - low) - 1.0f;
    default:
        return value;
    }
}
