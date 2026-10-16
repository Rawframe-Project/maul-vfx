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
    0xD63F3D77B3F87DE4u, 0x6986AB7FE70337C3u, 0xFB6AA4F3AAC0578Du, 0x1CCEBC3D735235BFu,
    0x79A076DB5ECE3518u, 0x9C5ED52F6E5A8C3Cu, 0xE6B13E3BC634E020u, 0x5B00CBBE7D4E2EEAu,
    0xA6273A6B75558CA6u, 0x4D5AD71674344DC4u, 0xE9D6E62B8485857Bu, 0xEEDDC8CEA2F0472Du,
    0xDD79F8CCB2283300u, 0x8F30E62507B6FA80u, 0x9A57A76C92AF6683u, 0x75525E914BD1FB88u,
    0xCFDB948880C1503Bu, 0x1758196A311FD334u, 0xFA5C24B5E3320BE1u, 0x006EFD88B5FEDF49u};

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
