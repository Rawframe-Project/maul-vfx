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
    0x24F53EFC6AEB9201u, 0x6D718BB5895FA4AEu, 0xCEE6B5ADA82835E1u, 0xFBE1EAFD07CC85A5u,
    0xA005BBB2BA7577C8u, 0xDF150173F655FF67u, 0x33A2F6F9808AB015u, 0xC3230DC950108326u,
    0x5D4AA2FFD8C75027u, 0x8C8DCE3DD115E502u, 0x5805E9B547F0B5F0u, 0x190B5F47FCBD9857u,
    0xF48BEB63AB2C1D97u, 0xE17C65F246D35BD9u, 0x2E291715CB21A7CBu, 0xD5E8B0887C160AA8u,
    0x5FF8D099BED755D8u, 0x3A2BB3F1AB88BADCu, 0x066E7E38C8F5F647u, 0x438DF66D177BD235u,
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
