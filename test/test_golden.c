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
    0x7B939EF42C8AE44Fu, 0x511CC0CA96D5F73Cu, 0xFBB7A08AC5A08E6Cu, 0xA8116575BA2FDB19u,
    0x47A570D0A64CC14Bu, 0x186C91D10441EC05u, 0x1CB0819EA637E168u, 0x8670B0A97720B020u,
    0x35E71AF229305609u, 0xC1F682D5AC6CDE25u, 0x3FA5F9BBFAEADAA1u, 0xF5864D730A6779E2u,
    0xB49FD85A1EE1CF2Au, 0xA378835EF19DFC6Du, 0xAEA9A59F742B039Fu, 0x6A4C5EF3C93D9D7Bu,
    0xA71D0B6DE71432A4u, 0x9CA36DB77DF052BAu, 0xA0AB1D4B45A1F017u, 0xDF8AE803193703B4u,
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
