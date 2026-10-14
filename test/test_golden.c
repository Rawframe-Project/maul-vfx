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
    0x63017F9525B6DF52u, 0x42B43DD68D638BF9u, 0x0598806B8A8BEB58u, 0x508E4E1C58CEE511u,
    0x9B4E96A5F46EC15Cu, 0xA3D8EA1D096C0D98u, 0xA369EC3C797BFF13u, 0x0B0DABD651F25A09u,
    0xB6626D8A9F66AF36u, 0xCD8B93418499CBC3u, 0xE20909DA3EC47400u, 0x75E58DB926B324EAu,
    0xD6885378FDF46173u, 0x1FCA2852D43190C6u, 0xB9387BE82DCD759Au, 0xF4770A3EAB1ABD5Au,
    0xFC47746410EA64DFu, 0xA941CD197CFC28F0u, 0x92718B2D7EC8E4C0u, 0x61C4D201BF31AA5Cu,
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
