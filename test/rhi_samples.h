// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sample queries run on the device (rhi_samples.c): a host's points
// written into a buffer, the simulation's query added and recorded, the
// samples read back when the frame is done.

#ifndef MAUL_VFX_TEST_RHI_SAMPLES_H
#define MAUL_VFX_TEST_RHI_SAMPLES_H

#include "rhi_harness.h"

#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"

// Whether the next query's or read's frame adds the simulation's step
// passes (mvfxGpuSim_AddPasses) first, as a host samples or reads the
// step it simulates in that frame; once, then off. StepInFrame adds them
// to the frame open when asked (false when refused), and turns it off.
void SampleWithStep(bool on);
bool StepInFrame(mvfxGpuSim* sim);

// Samples a surface at count points (the world less origin, three
// binary32 each) through mvfxGpuSim_AddWaterSamples, and reads the
// samples (MVFX_GPU_WATER_SAMPLE_WORDS words each) back into out.
bool SampleWaterGpu(Gpu* gpu, mvfxGpuSim* sim, int32_t water, const double origin[3],
                    const float* points, int64_t count, uint32_t* out);

// Samples a gas's field at count points (the world less origin, three
// binary32 each) through mvfxGpuSim_AddGasSamples, and reads the samples
// (a binary32 each) back into out.
bool SampleGasGpu(Gpu* gpu, mvfxGpuSim* sim, int32_t gas, mvfxGasField field,
                  const double origin[3], const float* points, int64_t count, float* out);

// A lattice of points about a gas's box where it is now: 6 by 6 by 6
// over half again its sides in its frame (a 2D gas's Z over twenty of its
// cells), mapped into the world, some past its faces. Lays them out
// given from origin (binary32) and in the world (from those binary32, as
// the device takes them), and returns how many lie in the box.
#define GAS_SAMPLE_POINTS 216
int32_t LayGasPoints(const mvfxGasInfo* info, const double origin[3], float* points, double* world);

#endif
