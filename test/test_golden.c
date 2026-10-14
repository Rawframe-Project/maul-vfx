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
    0xAB96DD8309D50BB0u, 0xC278D19F4C222616u, 0xA9EF1A4B73EE91B0u, 0x25859FD5A5632B06u,
    0xEFB5663FD0293C59u, 0xCE73B6CF69E0519Du, 0xFEBE775C6F12DDC0u, 0x881158225488CAA2u,
    0xCF956AB136E6B73Fu, 0x35C23A33F9B190B2u, 0x414F3EAC8027F8C0u, 0xB998F31EFE969542u,
    0xD7A58D12AAA75718u, 0x5EF7647F07BDD01Au, 0xA0190F0F2F21F990u, 0xA458D2BB2E13B1FCu,
    0xB21F08C7A281DC31u, 0x2F71C4E667F48AFFu, 0xC40A1C15BECBD971u, 0x1910098B35E51764u,
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
