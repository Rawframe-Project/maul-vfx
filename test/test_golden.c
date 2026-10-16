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
    0xBB23C133F8423867u, 0x8C0DF5525824CC70u, 0x80996C2D86769132u, 0x159FF3D2F160E969u,
    0xBBAF9D72C90102FEu, 0x79A3FE7441EE8D7Du, 0x42FD4DEDF95F8EA7u, 0x6EC20AF0FFAC5544u,
    0x70B53C940691EDC2u, 0xB012BCC2DE06B58Au, 0x7D50D8E4CCEFFB96u, 0x0D4F39450BCF6C46u,
    0x748980D31484656Eu, 0x7736330EB7064C46u, 0xD8A3F8D6E8E4BD7Du, 0xF47D20B08F5697EAu,
    0x4F10238C32D46911u, 0x7353E6D797847841u, 0xA7267ABC2D68F371u, 0xE059AA8F56B79AC6u};

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
