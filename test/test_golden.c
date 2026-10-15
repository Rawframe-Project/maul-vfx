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
    0x47F5DD3D6C967FC1u, 0x4976DD7CF79D7410u, 0x087C88468F52B32Eu, 0xAD091EDEB74477EAu,
    0x1E1D19C262B387C6u, 0x44F6F99DF9ED864Cu, 0x29C2893986489276u, 0xF6310E1E7432C84Bu,
    0x5B38B6CE89248C0Au, 0xFD4800302CE71168u, 0x162F62B51D4BAC0Au, 0x27E69C5EDB607265u,
    0x0F1F76F76C501D1Cu, 0xF282B94EDCC75E83u, 0x370216E69CED7BD0u, 0x6C163DCBAEA81E02u,
    0x7C511838FE45E640u, 0x602FC60298B8C778u, 0xA6BF4BA3C2BE6E10u, 0xFE9B029580F610BFu,
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
