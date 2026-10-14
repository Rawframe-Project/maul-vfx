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
    0xC56770ECA2A83155u, 0x8B7ACF36D714DF00u, 0x55413321796EA53Fu, 0x5E50531FC813E139u,
    0x3997BDA827F10D6Fu, 0x5440E40D830D8E7Du, 0x165B0148346DA7B0u, 0x1A694E3333A2A8D7u,
    0x746EFE6A98FE2B24u, 0x09FE1EC85B0E1187u, 0xB27C6BD6F20C4008u, 0x643A7C2F5ADD5148u,
    0x1AD6726E8DB6526Au, 0x69BDF6C1F9F75DF9u, 0x9142DDBF0DCDC074u, 0x7697215D87204DEBu,
    0xE2602896175EF1F3u, 0xD56604D083D090F6u, 0x2F5E38FF9A18420Cu, 0x3C7BDE5BEE762674u,
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
