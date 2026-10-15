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
    0x90EADD681CA7314Fu, 0xD70273C7633E7FD2u, 0xED173F86850B257Fu, 0x733776D54F78E869u,
    0x00A02D563AD1AC6Bu, 0xB8DC38C5DBD728F7u, 0xDDC8F3A105C8706Bu, 0xE98C8DB855EC2AE3u,
    0x7BB1EDAB34450DA1u, 0x016E6DB80DB64EF3u, 0xF05BF0DDC0E2060Du, 0x702A8CC62DCD31A7u,
    0x759187629B391CECu, 0x28A5BBF35F8FF5EEu, 0xFB04548CF51891E2u, 0x32CB2C9F09EE7762u,
    0x852A1E7A32168BECu, 0x417683A1BF244D3Au, 0x5608F0BA1A7D024Au, 0x1B80FDEBD5704DF2u,
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
