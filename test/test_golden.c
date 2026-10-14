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
    0x8B84D31841717C75u, 0x2147C106F7990ABFu, 0x06349433682961D0u, 0x2069DD58E9903CD6u,
    0xBFE8559ED0792DD5u, 0x138F96E06D4217A3u, 0xEB0BAD1359081B9Eu, 0x2E04F7D63CA3D612u,
    0x9A2216EE28824B7Eu, 0x1BC90388CB8C1E01u, 0x169A42D8761C33CBu, 0x0841BFAB6E4A1256u,
    0xC8018695E2D6A539u, 0xBC83FBD4105E2622u, 0x522231D1B53A2C70u, 0x9038097EF4DBAEFAu,
    0xC8F5862C6C009590u, 0x46A1756BBF9284A5u, 0xE4DE28DF292A1C5Du, 0xAB75A1371851F2CAu,
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
