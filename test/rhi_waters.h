// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A run's water surfaces on both paths (rhi_waters.c), for the harness's
// runs: the host's pushes given to both instances, each surface's record
// read back and reported, its fields compared after every step.

#ifndef MAUL_VFX_TEST_RHI_WATERS_H
#define MAUL_VFX_TEST_RHI_WATERS_H

#include "rhi_harness.h"

#include "maul-vfx/output.h"

// The most cells a run's surface has.
#define WATER_CELLS (64 * 64)

// Gives both instances the pushes of step s on each of the run's
// surfaces: a few every fourth step of the first 24, one reaching past
// an edge, one lifting.
void PushWaters(const Run* run, int32_t s);

// Reads each surface's statistics record back after its step and reports
// it to the planned instance, as a host does a frame or more late.
bool ReportWaters(Gpu* gpu, mvfxGpuSim* sim);

// Writes a read of a surface from a byte of a buffer of bytes, filled
// with fill first, through mvfxGpuSim_AddWaterRead, and reads the buffer
// back into out.
bool ReadWaterGpu(Gpu* gpu, mvfxGpuSim* sim, int32_t water, const mvfxWaterRead* read,
                  uint64_t bytes, uint64_t offset, uint8_t fill, void* out);

// Whether each field of each of the run's surfaces is the CPU path's,
// relative to one more than its size within the tolerance; the largest
// difference over the run so far, and the largest a CPU path's cell rose
// or sank from its rest level, from 0 after a reset.
bool SameWaters(Gpu* gpu, mvfxGpuSim* sim, const Run* run, float tolerance);
float WaterError(void);
float WaterMoved(void);
void ResetWaters(void);

#endif
