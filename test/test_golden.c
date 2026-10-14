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
    0xF1B294B1116EECACu, 0x29FA2BA445429426u, 0xB50F9DD3DF6ACEE9u, 0x3BB2E8E2C3955538u,
    0x486F694EB00EB6D4u, 0x7AD93E9CC2106B45u, 0xCD91C45D0689D92Eu, 0x4C3D2F192935D49Fu,
    0x73DFCCEE4A9E133Du, 0x422EB929EACD913Au, 0x8398827A7CBAB130u, 0x154B152DB0D73380u,
    0xA6824CDF5F855B08u, 0x790CE2151E56C557u, 0x1C4630E5DE5FBD1Du, 0x4910EE8566E154EBu,
    0x031E86B32611682Au, 0x28CF2C184ABC56EFu, 0xCC92ECD989445CE8u, 0xAD7E48E09426B2DFu,
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
