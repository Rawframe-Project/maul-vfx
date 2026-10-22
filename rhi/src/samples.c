// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A host's points sampled on the GPU: a query's checks and a box's map,
// shared by the water and the gas samples.

#include "samples.h"

#include "numbers.h"

#include "maul-vfx-rhi/sim.h"

#include <math.h>
#include <string.h>

bool mvfxIsSampleQueryValid(const SampleQuery* query, uint32_t words)
{
    bool valid = query->count >= 0 && query->count <= MVFX_GPU_SAMPLE_POINTS &&
                 query->points.index1 != 0 && query->target.index1 != 0 &&
                 query->points.index1 != query->target.index1 && query->pointOffset % 4u == 0 &&
                 query->offset % 4u == 0 && query->pointOffset <= UINT32_MAX - 4u &&
                 query->offset <= UINT32_MAX - 4u;
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid = fabs(query->origin[k]) <= 0x1p52;
    }
    uint64_t count = valid ? (uint64_t)query->count : 0u;
    return valid && query->pointBytes >= 12u * count && query->bytes >= 4u * count * words;
}

// Turns a binary64 vector by a unit quaternion, as the core's
// mvfxRotateDouble.
static void RotateDouble(const float q[4], const double v[3], double out[3])
{
    double x = (double)q[0];
    double y = (double)q[1];
    double z = (double)q[2];
    double w = (double)q[3];
    double tx = 2.0 * (y * v[2] - z * v[1]);
    double ty = 2.0 * (z * v[0] - x * v[2]);
    double tz = 2.0 * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}

void mvfxSampleMapWords(const double origin[3], const double center[3], const float rotation[4],
                        float scale, uint32_t words[SAMPLE_MAP_WORDS])
{
    const float turn[4] = {-rotation[0], -rotation[1], -rotation[2], rotation[3]};
    float sigma = 1.0f / scale;
    double from[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        from[k] = (origin[k] - center[k]) * (double)sigma;
    }
    double into[3];
    RotateDouble(turn, from, into);
    memcpy(words, turn, sizeof turn);
    mvfxFloatWords(words + 4, (float)into[0], (float)into[1], (float)into[2], sigma);
}
