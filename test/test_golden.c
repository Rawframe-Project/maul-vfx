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
    0x3F117F4869FF5678u, 0xCDE892691272E61Fu, 0x7D7329CB9BD36C37u, 0xAAF829632774AEC6u,
    0x7C2EEBA4799D7AD4u, 0x2BFAD386C99FD744u, 0xE277B6872432B880u, 0x9D883CE5364D2F37u,
    0xCEC3B3B40626F89Bu, 0x3F7D4B3AD811E957u, 0xD70CB46D782F794Fu, 0x98A780AD43EB914Du,
    0xF1E7C4462CAB92B8u, 0x5CFF859171BF19E3u, 0xC18C2B65BB92C7B2u, 0x4CB2E5BF01B89C16u,
    0xEBEA46B01B89F90Eu, 0x409AB0E5F962F6C7u, 0xB99398C4134C4159u, 0x3A33960052CA3F63u,
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
