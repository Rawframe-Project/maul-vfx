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
    0xA08288442CD357B1u, 0xE40EC75CC1F9641Fu, 0x0EEFCFB7810C3A60u, 0x7A0EA1809AE33BBFu,
    0x369F6BAC78B80A3Eu, 0x222F24FF471A8E14u, 0xD8C300311DFA8D7Eu, 0xCECEF624163940FEu,
    0xE2B03CCD91181BCAu, 0xF625066903B02AFEu, 0x33F5E6E95815F31Eu, 0xC567B1471315013Au,
    0x06F850BE72756E42u, 0x8784AD1792753A6Au, 0x9BC1A8CEC396A3BAu, 0x85FC456095EECD48u,
    0x9DB78D7E952A37CDu, 0xB177FED61D53CF8Cu, 0x942DBD521AEC15E3u, 0xD7EA3BEC41483F31u,
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
