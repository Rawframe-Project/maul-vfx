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
    0xA7CA7B3095CC9E1Bu, 0xD86939E33750A917u, 0x2E455AB8733D7D6Au, 0xA64034D943E99EA3u,
    0xED3BA493BDA37B46u, 0x00AB945250A3EF0Du, 0x714DFB455AB30719u, 0x493CD6CF2AB7A097u,
    0xFEFE1D9766FE56D4u, 0xEA9BBE6344B59232u, 0x7B5CCD7527750EAFu, 0xD088321247DEAAA6u,
    0xCA105357A01F2E0Fu, 0xAD33CDD5D0136721u, 0x50CD532B2BE141A6u, 0xDFF971C05B2E675Eu,
    0xA97B27AEC99D9FC8u, 0xC531864B605CB77Bu, 0xC2991C8F572A5DC7u, 0x8646054B72720070u,
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
