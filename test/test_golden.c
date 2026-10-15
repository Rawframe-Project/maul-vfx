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
    0xACB63EDBDAFB4536u, 0xD86017FF883399C0u, 0x5828AD9BA341CE3Bu, 0x216398D9BF2FCC8Au,
    0x4DCBC78E59EE205Au, 0xE831E6F5076EE2E3u, 0xCF81CFA16052680Bu, 0x96132A85D4E93D7Bu,
    0x38D16811B1C71132u, 0x5B8C7153819F7F87u, 0xC12C8C2903D114FEu, 0xFFB3789928609662u,
    0xBF48DDD2A0543D6Du, 0xDA7E3ABF9EC32F09u, 0x5957BA13C4CE6420u, 0x6AC1A0A333E9AE58u,
    0x234751E1339B192Eu, 0x076107A57D2B0DCAu, 0x450AC8EE1F0B195Du, 0xEBC31B00380B28F0u,
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
