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
    0x8E8C9A99BA6E66E7u, 0x92DD807F4DEC4633u, 0x9A6834E8E45BCF42u, 0x846EBAEA45266148u,
    0x5D34839FC7B685A8u, 0x86113E9FF9CD5D36u, 0x79A3E7C5B3DD4FD2u, 0xB02970D7D6C47D58u,
    0xCD3075F45AA44D9Eu, 0xAF4274CCECD506BDu, 0xFF4C1365206B2591u, 0x7AA961C5D0F826D9u,
    0x12B73531DDCBD720u, 0x57B7E156E45E77A7u, 0x1028E5F0968964F4u, 0x951B27D421FE8913u,
    0x4FD13BBA49D22D96u, 0x6FF0C5BC0461897Cu, 0xD5AE6DD8582548A0u, 0x0F7362811075C25Du,
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
