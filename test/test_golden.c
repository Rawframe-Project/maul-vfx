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
    0x26EF7C2FCC4C9563u, 0xAC32E5337A2B04EAu, 0x1C7E0E38B7A61BE6u, 0xE30C5EC530C535E4u,
    0x90E854D5ECE31E58u, 0xA5D39BEF915184C7u, 0xCCF997F479AAE06Eu, 0x8C6D59937B22C05Au,
    0xF243E22D6E96A1BDu, 0x8668B847212A480Au, 0xE5C0E7B2D7BC6BDFu, 0x2A2CCF7D50150C9Cu,
    0x67AF4561DFD5C733u, 0x1F38D3B3ECFB50EFu, 0x7B2A437FFF4FB9B3u, 0xD73FA71385C693FBu,
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
