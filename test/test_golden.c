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
    0xA65D14D157BF01DDu, 0x6E7F627C43D2F214u, 0x7F2DAC7FCEF7F725u, 0x7728606B3E14FBD3u,
    0xC207142B3C879C15u, 0x08A309E1E1E4971Cu, 0xCBF0C233133A58D5u, 0x11677A3A54625FC8u,
    0x49A4AC5F2E7EDA56u, 0x1D902B6D00C5946Fu, 0x82658A5A32259118u, 0x1158387949969331u,
    0xB7614D14DB2BF7C7u, 0xB3A47E1A662D9C1Eu, 0xC4B0C886F5C4F737u, 0x55DDA6583CADD12Cu,
    0x07FB50BB36677686u, 0x14B032EA85E745CEu, 0x8EA765C9DCC5F2D0u, 0x2945E32D082E8F56u,
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
