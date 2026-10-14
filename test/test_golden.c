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
    0x74CEE800FA815EC5u, 0xCB6BDA465CC610B0u, 0x11588830C5143267u, 0x01ADE8A675568823u,
    0x4DC0BD6E6F619394u, 0xFA1D562C313D4590u, 0xF892A6F26D4794B4u, 0x3B1ADD3358603FDAu,
    0x7D2D554790DD3FC5u, 0xC6C023CDADD409A1u, 0x8C666E6320055417u, 0xFEF41C01DDEFBAD9u,
    0x6D2A59F5BB1E3BB0u, 0xDF1BB31674B37AC4u, 0xE992E3E17B83BFB1u, 0x169947695F14061Fu,
    0x00018589631AC633u, 0x968D1C0D96C52573u, 0xCF25791EB082BC6Du, 0x3DBA5D07826EBE07u,
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
