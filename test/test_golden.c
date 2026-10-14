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
    0xBFD60229531D38EBu, 0xC2433D39D84520FAu, 0x7E4C59FBD7B68D46u, 0xB286DA4BAF163CCFu,
    0xF6E37D5D6AB11018u, 0x7BE358180F603714u, 0x496C3572F3ACE725u, 0x822D7BFD91CD69F5u,
    0xB13485BE4F44BB73u, 0xE5EAA75604D5DC41u, 0xD890E2114304AC29u, 0xF9B7A2DF11A2F8FBu,
    0x74F2192CA87AF5C5u, 0x26F06103E8EA7C44u, 0xA49DA7003F1B5B62u, 0x3600BD0DF5747EA4u,
    0xADBB2B3E9F1BB605u, 0x6629DB376ADA8F79u, 0xBF2CAF0743113946u, 0xE7220A5B41FB9015u,
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
