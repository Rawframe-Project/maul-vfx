// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's units held to the host's frames on the GPU: the frames the
// plan gives (mvfxEmitterPlan.attachFrames, at the plan's end in the
// particles' space) staged eight words each after the births, and where
// the unit pass finds them (sim.wgsl's unitFrames).

#include "sim_state.h"
#include "upload.h"

#include <string.h>

uint64_t mvfxSimFrameBytes(const Emitter* emitter)
{
    return (uint64_t)emitter->frameCapacity * MVFX_FRAME_WORDS * 4u;
}

void mvfxSimFrameNumbers(Emitter* emitter, const mvfxEmitterPlan* plan)
{
    int32_t count = plan->attachFrameCount < emitter->frameCapacity ? plan->attachFrameCount
                                                                    : emitter->frameCapacity;
    for (int32_t f = 0; f < count; ++f)
    {
        uint32_t* at = emitter->frameWords + MVFX_FRAME_WORDS * f;
        const float* frame = plan->attachFrames + 7 * f;
        mvfxFloatWords(at, frame[0], frame[1], frame[2], 0.0f);
        memcpy(at + 4, frame + 3, 4 * sizeof(float));
    }
    emitter->frameCount = count;
    uint32_t* units = emitter->words + MVFX_UNITS_FIRST;
    units[28] = (uint32_t)(emitter->framesAt / 16u);
    units[29] = (uint32_t)count;
}

bool mvfxUploadSimFrames(mvfxGpuSim* sim, Emitter* emitter, mrhiPassId pass)
{
    return emitter->frameCount == 0 ||
           mvfxUpload(sim->device, &sim->uploaded, pass, emitter->resources[BUFFER_BIRTHS],
                      emitter->framesAt, emitter->frameWords,
                      (uint64_t)emitter->frameCount * MVFX_FRAME_WORDS * 4u);
}
