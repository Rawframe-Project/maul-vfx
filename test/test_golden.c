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
    0x3EEB6A00FF592072u, 0x662455AB1E55F6A3u, 0x26CCF75FFE636D46u, 0x28575C38AE5F2433u,
    0x9FC42F0840F28037u, 0xF5FA92AED5E39B12u, 0x83155C1861A0F5A4u, 0x91F4499243ED6BA2u,
    0x28DBF7B1C8D4F6EDu, 0xC50195D797100161u, 0x628348388E5230DCu, 0x5EB059954C94EB99u,
    0x06E953293B004630u, 0xFA5CC4920BD2575Au, 0xAA8439BD9571BD9Bu, 0xF42593F32E14E653u,
    0x7BC81B03CC72188Au, 0x2D91A92C642355BBu, 0x0854BB03D886A974u, 0x0EBD24C17D72809Bu,
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
