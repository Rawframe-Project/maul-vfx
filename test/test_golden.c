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
    0x2F267577CF513F8Cu, 0xB3060E6BF8BD7270u, 0x5F833A22EBBD1F8Du, 0x2DE90F2D1894C3CDu,
    0x75DE7E89320179D6u, 0x5FC8BAF712E2C370u, 0xB360D7FD92241815u, 0x6507E767A794C822u,
    0xF603C66C290154C6u, 0x82957F142891ABEBu, 0x6264288B9CE84F98u, 0xE9A7423A8240C800u,
    0xAFF01895DC432987u, 0xD4050B42DF5F0E37u, 0x4383D34094D77628u, 0xB4FD24D2F0643E91u,
    0xFD963208C8D40721u, 0x547000EC231BC4AEu, 0x83D9B099DBFC53A7u, 0x59964089870745CFu,
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
