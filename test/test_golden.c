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
    0x3FF4DAF82CEBF6CEu, 0x0B47884C2DBED0F3u, 0x7FC47C889D8B3C75u, 0x6C6C3A2E23F3B078u,
    0xC77C708C5AD7817Eu, 0xEE0EF8716DAB1F5Cu, 0x7EED00E1B0B2F5E0u, 0x507AA71809B86F35u,
    0x9E7E2C306623A5B1u, 0x08B660908ABF94A1u, 0x2A40EBA37143BD12u, 0x8411A6B0571E4EF3u,
    0x039F64FA859326E0u, 0xD8387E49F00D59E7u, 0x55491CB6071E32FBu, 0x6FB705B8E6373979u,
    0x5ED6075454FD55F3u, 0x948C0B62FE251BE9u, 0xB72E66E25B90AD3Du, 0x7DE969DB8D9BA8B6u,
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
