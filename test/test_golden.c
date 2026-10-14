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
    0xF56819602F0CDA3Au, 0x09A631180F339BFAu, 0x7909381F3B6D888Eu, 0xDB84192078AF1BEAu,
    0xBE7F4C8620A4F024u, 0x71A4E69898E6BC79u, 0xB13E18C639C1BF83u, 0x2DE1B26FDEC81BD7u,
    0x13305B23930A4BA1u, 0x4F098756D4E494C4u, 0xE44350E9BF959957u, 0x5B5687C07720C4C3u,
    0x82EEF4B014E298C7u, 0xF3C71616566DF596u, 0x3E246EBE5F53F463u, 0x70DCD34C4C19D4D7u,
    0x9401E71233956F94u, 0xAE7D58AD67359771u, 0x9A24B862AF6920F3u, 0x17EDEA883B1DBA7Du,
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
