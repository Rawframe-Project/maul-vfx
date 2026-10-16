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
    0x44993AC9E0C36A3Du, 0xA9100100E9B88F46u, 0xE0CC87DFF3EEB465u, 0x5681F4AFD5D63D4Eu,
    0x98468158E43CDE6Eu, 0xED343F5696646CABu, 0xD17FA88E9EFE4B8Cu, 0x417B5629DC0FDD6Cu,
    0x0AC543DFC0F6F958u, 0x1AC5FD3D61FD5812u, 0xA18B3E99B22E7073u, 0x88B56917639DE95Au,
    0x4F243F33DBD05595u, 0x7D59A1AE0F024CCFu, 0x1548DA867F830035u, 0xD55442A384E38AF7u,
    0x7A03C5FB4D4A99FFu, 0xC9398FC4D6B0A305u, 0x489D61338541DD31u, 0x3CD232062B30E2A6u};

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
