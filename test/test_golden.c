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
    0x6A7631AE21348FDDu, 0xCC6E1763855D21E9u, 0xE332CF440EBEAD7Bu, 0x330448BDE3219666u,
    0xF2AB6C0B7F150BDFu, 0xE72D7333488B7E59u, 0x4C46343342542FF9u, 0x9C0AA2C337AA9DE8u,
    0x015CAF880D6C82B1u, 0x7E8E3A6383272A69u, 0x8C4EA8C29A9B5634u, 0x7C25BA74C051086Fu,
    0xEFAC3920F0DEBAC7u, 0xC0F3A77193437B38u, 0x8FCC9FA7B191F335u, 0xAA3E638CE51FC548u,
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
