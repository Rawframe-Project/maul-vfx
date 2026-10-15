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
    0xA2BA28C9F91198DAu, 0x4871525D5D378B08u, 0x8E882E1048CE9A33u, 0xEB503FDC2E4C40E7u,
    0x0788503C1C8C8B72u, 0x8687F52F82290039u, 0xBCC8510A0AF8A701u, 0x0E5503F41DCDC71Cu,
    0xA72A7FD2A8B40B05u, 0xA4445C31FD341A97u, 0xDCCF3055AD032816u, 0xC60795E1B631B75Fu,
    0x977174A83C72A554u, 0x847D693956F503C0u, 0x260781F3190F4814u, 0x64D2BB962DAB927Eu,
    0x86FF12B3FA6159CEu, 0xFA4D69CB7FE9B0EDu, 0xB930A9E1709E1617u, 0x40A2A6FCAF0CB0E2u,
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
