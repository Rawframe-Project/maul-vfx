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
    0x24F53EFC6AEB9201u, 0xD7A602AC5DE53090u, 0xD5FEF223400F9351u, 0xD2A4E2726D0483E2u,
    0x1C9A93D19F5D2DFDu, 0x8BFD5B47AEF04A8Du, 0x550E24787025B212u, 0xC5EB2F9E6C0E2ED7u,
    0x46E4AD364E88E55Fu, 0x9E18C5CA4FFC3479u, 0xA24F3E1E13C0DD00u, 0x190B5F47FCBD9857u,
    0x3F16215DAD22189Du, 0xE17C65F246D35BD9u, 0xC1AF7CFA44511E06u, 0x01B507E0AF36F251u,
    0x98AFB66EA931D143u, 0xC01FD2773501FA57u, 0x5F5DE8A560A722AFu, 0xF16878B592F0742Fu,
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
