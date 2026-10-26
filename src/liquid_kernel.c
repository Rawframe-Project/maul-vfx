// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "liquid_kernel.h"

#include <math.h>

float mvfxLiquidPoly6(const mvfxLiquidKernel* kernel, float square)
{
    float d = kernel->square - square;
    return d > 0.0f ? kernel->poly6 * d * d * d : 0.0f;
}

void mvfxLiquidSpiky(const mvfxLiquidKernel* kernel, const float offset[3], float length,
                     float out[3])
{
    float d = kernel->span - length;
    float factor = d > 0.0f && length > 0.0f ? -kernel->spiky * d * d / length : 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = factor * offset[k];
    }
}

// The kernels' factors at a kernel radius: poly6's 315 / (64 pi h^9) and
// the spiky gradient's 45 / (pi h^6) in 3D, 4 / (pi h^8) and 30 / (pi h^5)
// in 2D.
static void Factors(float radius, bool planar, mvfxLiquidKernel* out)
{
    const double pi = 3.14159265358979323846;
    double h = (double)radius;
    double h2 = h * h;
    double h4 = h2 * h2;
    double h8 = h4 * h4;
    out->poly6 = planar ? (float)(4.0 / (pi * h8)) : (float)(315.0 / (64.0 * pi * h8 * h));
    out->spiky = planar ? (float)(30.0 / (pi * h4 * h)) : (float)(45.0 / (pi * h4 * h2));
}

mvfxLiquidKernel mvfxLiquidKernelOf(const mvfxLiquidDef* def, float scale)
{
    mvfxLiquidKernel kernel = {.radius = def->spacing * scale * def->kernelScale};
    int exponent = 0;
    (void)frexp((double)kernel.radius, &exponent);
    kernel.unit = (float)ldexp(1.0, exponent - 1);
    kernel.inverseUnit = (float)ldexp(1.0, 1 - exponent);
    float spacing = def->spacing * scale * kernel.inverseUnit;
    kernel.span = kernel.radius * kernel.inverseUnit;
    kernel.square = kernel.span * kernel.span;
    Factors(kernel.span, def->planar, &kernel);
    // The ideal lattice about a particle, in a fixed order.
    int32_t reach = (int32_t)ceilf(def->kernelScale);
    int32_t layers = def->planar ? 0 : reach;
    float rest = 0.0f;
    float gradients = 0.0f;
    for (int32_t c = -layers; c <= layers; ++c)
    {
        for (int32_t b = -reach; b <= reach; ++b)
        {
            for (int32_t a = -reach; a <= reach; ++a)
            {
                const float offset[3] = {(float)a * spacing, (float)b * spacing,
                                         (float)c * spacing};
                float square =
                    offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2];
                float g[3];
                mvfxLiquidSpiky(&kernel, offset, sqrtf(square), g);
                rest += mvfxLiquidPoly6(&kernel, square);
                gradients += g[0] * g[0] + g[1] * g[1] + g[2] * g[2];
            }
        }
    }
    kernel.restInverse = 1.0f / rest;
    float full = gradients * kernel.restInverse * kernel.restInverse;
    kernel.softening = def->relaxation * full;
    kernel.tensileScale = def->tensile / full;
    float reference = def->tensileDistance * kernel.span;
    kernel.tensileInverse = 1.0f / mvfxLiquidPoly6(&kernel, reference * reference);
    return kernel;
}
