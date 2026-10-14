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
    0x4C2BBF7CDD50460Eu, 0x1D7108571906DAC7u, 0xF1252F77069B4474u, 0x4B7F7AE5FF897052u,
    0xBABD968EAE566CDEu, 0x474A573883CE8168u, 0xBF0ED82D1959AC2Fu, 0x65AD4EC65C7ABB28u,
    0xE62BAA833FC89CC1u, 0xA8F1045B291D04AFu, 0xBA51B62116E40BF5u, 0x3B571F6D0C4A4362u,
    0x4798C730C2F30A7Du, 0xC4403A11722B745Bu, 0xC3534D18B4C15489u, 0x5E28A8D63E90307Fu,
    0x0BE62549EA886B6Au, 0x0EAB0E840CF2118Eu, 0xDD7112492D70A581u, 0x08EB113EB7557061u,
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
