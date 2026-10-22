// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A host's points sampled on the GPU (mvfxGpuSim_AddWaterSamples,
// mvfxGpuSim_AddGasSamples): a query's buffers and bytes checked, and a
// box's map from the world less the query's origin into its frame.

#ifndef MAUL_VFX_RHI_SRC_SAMPLES_H
#define MAUL_VFX_RHI_SRC_SAMPLES_H

#include "maul-rhi/encoder.h"

#include <stdbool.h>
#include <stdint.h>

// A query as a samples def holds it: its origin in the world, the
// points' buffer, their first byte and the bytes from there, how many,
// and the samples' buffer, first byte and bytes from there.
typedef struct SampleQuery
{
    const double* origin;
    mrhiResourceId points;
    uint64_t pointOffset;
    uint64_t pointBytes;
    int64_t count;
    mrhiResourceId target;
    uint64_t offset;
    uint64_t bytes;
} SampleQuery;

// The words of a box's map (the samples kernels' rotation and into).
#define SAMPLE_MAP_WORDS 8u

// Whether a query of samples words long each is in range: its origin,
// its count, its buffers and their bytes.
bool mvfxIsSampleQueryValid(const SampleQuery* query, uint32_t words);

// A box's map from the world less origin into its frame (the core's
// mvfxBoxMap of the world: the box's turn back, 1 / scale, its center),
// rebased on the origin in binary64 and rounded once: the turn, then the
// offset into the frame with 1 / scale.
void mvfxSampleMapWords(const double origin[3], const double center[3], const float rotation[4],
                        float scale, uint32_t words[SAMPLE_MAP_WORDS]);

#endif
