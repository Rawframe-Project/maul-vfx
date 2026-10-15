// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The scene's state hashed after every step against values committed here
// (record mvfx-0001: the same bits on every platform). CI runs it on every
// platform of its matrix; one hash differing anywhere fails. A change meant
// to change results updates the values and says so in the changelog. Run
// it with --print to print the values this build computes.

#include "vfx_scene.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const uint64_t s_golden[SCENE_STEP_COUNT] = {
    0xFB2667DDF0A287BBu, 0xAA7F494B174D51E9u, 0x4558CF19A8925585u, 0x5B98C7D87327A3C6u,
    0x9672CEB7E7C43D70u, 0xB9FED89E2EC55F6Du, 0x7E6588ABE7ED9E3Du, 0xF6EFAFC1D53E2293u,
    0x48B7486B8F2E9787u, 0x36FB75A7561C5A25u, 0xC5BFD6D7CC3891BCu, 0x1CC99BBCC8CD3354u,
    0x074D014558023543u, 0x2CB3F80D4E60F4C7u, 0x380E8D3A09BDF81Bu, 0xF5C57F91ED5950EEu,
    0xE7DD32373B0A0C6Cu, 0x20EC76BAF465144Du, 0x1C07BDEF88F1A225u, 0xFBD2A4E385440398u,
};

int main(int argc, char** argv)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    BuildScene(context);
    bool print = argc > 1 && strcmp(argv[1], "--print") == 0;
    for (int32_t step = 0; step < SCENE_STEP_COUNT; ++step)
    {
        MoveScene(context, step);
        CHECK(mvfxContext_StepTicks(context, s_sceneSteps[step]) == mvfx_success, "step");
        uint64_t hash = HashScene(context);
        if (print)
        {
            printf("    0x%016" PRIX64 "u,\n", hash);
        }
        CHECK(hash == s_golden[step], "the state matches the committed hash");
    }
    mvfxDestroyContext(context);
    return s_failures == 0 ? 0 : 1;
}
