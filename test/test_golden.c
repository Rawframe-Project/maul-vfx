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
    0xA08288442CD357B1u, 0xE40EC75CC1F9641Fu, 0x0EEFCFB7810C3A60u, 0x383E5F9CE273B8D7u,
    0xAD647BF7DDF240A7u, 0xA44F79E789A06B85u, 0x789263805EE68EDBu, 0xD3D1F51174CD0F01u,
    0xE72684E51FD9D1DDu, 0xFB988B485E131911u, 0xC0ABCA70A136D434u, 0x586A1C176A77F5A4u,
    0x8426C8CD3432AF60u, 0xE322A5E3817115CEu, 0x2321F04DC3EB6D71u, 0xB706F25001D4EBAEu,
    0x29B56AEEA765DC1Cu, 0x1A5B1742F26367DCu, 0xDA25A371465D5DF6u, 0x28A42D1D8F8D27FCu,
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
