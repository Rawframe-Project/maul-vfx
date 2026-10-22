// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A run's surface gases on both paths (rhi_surfaces.c), for the harness's
// runs: each one's distance compared after every step.

#ifndef MAUL_VFX_TEST_RHI_SURFACES_H
#define MAUL_VFX_TEST_RHI_SURFACES_H

#include "rhi_harness.h"

// The most cells a run's surface gas has, and the tolerance of a distance
// relative to one more than its size: a device may contract a product and
// a sum (WGSL allows it), and the paths' particles differ within the
// run's tolerance.
#define SURFACE_CELLS     (48 * 48 * 32)
#define SURFACE_TOLERANCE 1e-3f

// Whether each of the run's surface gases' distance is the CPU path's,
// relative to one more than its size within the tolerance; the largest
// difference over the run so far, and the least distance a CPU path's
// cell held (inside a particle below 0), from 0 and the most after a
// reset.
bool SameSurfaces(Gpu* gpu, mvfxGpuSim* sim, const Run* run, float tolerance);
float SurfaceError(void);
float SurfaceLeast(void);
void ResetSurfaces(void);

#endif
