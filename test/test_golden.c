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
    0xA2BA28C9F91198DAu, 0x4871525D5D378B08u, 0x8E882E1048CE9A33u, 0xEB503FDC2E4C40E7u,
    0x6A7631AE21348FDDu, 0xCC6E1763855D21E9u, 0xE332CF440EBEAD7Bu, 0x330448BDE3219666u,
    0xF2AB6C0B7F150BDFu, 0xD26B4941070C4D13u, 0xAF9901E2B19435DBu, 0x25118F49925D13CBu,
    0x8A3E6B9E929D73DAu, 0xB4413FD351CD2940u, 0x7FB31028A82467AFu, 0x3AAA6BBB9E003A72u,
    0xFA1946B68B51D382u, 0xE5C89F72107F1B18u, 0xF6AE5D7A8043D15Au, 0x85AD11256E848952u,
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
