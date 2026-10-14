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
    0xC56770ECA2A83155u, 0x30E320576F59083Fu, 0x59082CCC199892B7u, 0x6EC32D4C4BEEE433u,
    0x1AA64883139C43F4u, 0xA90D85C6870D57B9u, 0x1892D9AA479ADEFBu, 0x574F2DDD7B88C86Du,
    0x15704809371C204Au, 0xA0CDB08CAB570A6Fu, 0x1ABC1C445782ACF7u, 0x1A9D8B709F03BEF1u,
    0x8E7B3F55E4954000u, 0x43983991CEED6988u, 0x492210976848D069u, 0x1FFB38626277C922u,
    0x6243509B917E17D9u, 0x96CCBCCF9BF98284u, 0xC5E4203CFDA30D02u, 0xFC06534E3152C0B9u,
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
