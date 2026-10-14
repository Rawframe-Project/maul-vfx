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
    0xC77C708C5AD7817Eu, 0xEE0EF8716DAB1F5Cu, 0x7EED00E1B0B2F5E0u, 0x4470A963421EA048u,
    0x5CAF0563A4EBCA53u, 0x4C2DF486F6ADBA16u, 0xED9BC76AFA223D6Cu, 0xF57C74BCCFF65E43u,
    0xAA8CF8A6EFB7AED9u, 0xA5429BAAF10DF403u, 0x24C28197AE3C0A80u, 0x1F44C6DC37F8F2E6u,
    0xDBF39BCABA353042u, 0xCBCBCE2CC9CF6F85u, 0xBFAF71D238B197D7u, 0x449D32A2EAE85F43u,
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
