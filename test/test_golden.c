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
    0x1E8C1175EDD30CCFu, 0xC56E47F65D569A7Au, 0x176D44CCA17E9F21u, 0x01623416083921DDu,
    0x4D1BAB7A42607A49u, 0x2B1A5F83264DA200u, 0x0123DD9E204B2553u, 0x5AE10E31976A5B95u,
    0x1333130135FAA501u, 0x4B5E786927DAE944u, 0x700257402E5C5797u, 0xEE1163A48E5F6EBFu,
    0xC9A00B90CD38C5D5u, 0x30956808EF9BD829u, 0xB84E68FF1B8D62E8u, 0x347B7485B7264E0Fu,
    0xD9DACD90C461B29Eu, 0xC46E9F244B9AF966u, 0x704873396B32A502u, 0xB2BE6B9C8FF1EB56u,
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
