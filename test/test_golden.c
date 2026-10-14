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
    0xC56770ECA2A83155u, 0x5AA01EBBEBCDAEB8u, 0xBF5B21A95389A630u, 0x028F4202A5C33054u,
    0x720D66A1643D334Au, 0xE93588C67195B9B5u, 0x165B0148346DA7B0u, 0x574F2DDD7B88C86Du,
    0xFBFCF198D97490EDu, 0x751E1DE143794E2Bu, 0xB27C6BD6F20C4008u, 0x1A9D8B709F03BEF1u,
    0x40F4DCAE55237011u, 0x43983991CEED6988u, 0x4D0FF2D987CBCA7Eu, 0x450BBDB46DA3341Cu,
    0x5477E50075DB93C2u, 0x0713CB5D02CFC981u, 0x460A38F8A85F2A11u, 0x3C7BDE5BEE762674u,
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
