// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas read out: a field read as float32 the float reader's values, as
// float16 their halves; unorm8 and snorm16 over a range, clamped and
// rounded as layouts round; a box of cells at a host's pitch, the bytes
// between rows untouched; a velocity's component its faces' mean; the
// reads' refusals; a field sampled at points in the world, the cells'
// values at their centers, a linear field between them, 0 outside the
// box, through a turned and moved instance.

#include "half_reference.h"
#include "test_harness.h"
#include "test_hooks.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// Opens an instance of an effect of a 2D gas of 8 by 6 cells of 0.1,
// placed and turned, its density a + 2x - 3y at cell (x, y) and its X
// faces' velocity their index along X.
static void Open(const double origin[3], const float rotation[4])
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 8;
    gas.size[1] = 6;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    memcpy(instanceDef.origin, origin, sizeof instanceDef.origin);
    memcpy(instanceDef.rotation, rotation, sizeof instanceDef.rotation);
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    float density[48];
    for (int32_t i = 0; i < 48; ++i)
    {
        density[i] = 0.25f + 2.0f * (float)(i % 8) - 3.0f * (float)(i / 8);
    }
    float faces[9 * 6];
    for (int32_t f = 0; f < 9 * 6; ++f)
    {
        faces[f] = (float)(f % 9);
    }
    CHECK(mvfxTestWriteGasScalar(s_context, s_instance, 0, mvfx_gasDensity, density, 48) &&
              mvfxTestWriteGasFaces(s_context, s_instance, 0, 0, faces, 9 * 6),
          "the fields set");
}

static const double s_origin[3] = {0, 0, 0};
static const float s_unturned[4] = {0, 0, 0, 1};

// The whole box as a read of a field in a format, packed.
static mvfxGasRead Whole(mvfxGasField field, mvfxFormat format)
{
    return (mvfxGasRead){
        .field = field, .format = format, .size = {8, 6, 1}, .low = -20.0f, .high = 20.0f};
}

static void TestFormats(void)
{
    // The density read whole: as float32, the float reader's values; as
    // float16, their halves; as unorm8 over -20 to 20, (v + 20) / 40 of
    // 255 rounded half up; as snorm16, v / 20 of 32,767 rounded half away
    // from 0.
    Open(s_origin, s_unturned);
    static float floats[48];
    static float wide[48];
    static uint16_t halves[48];
    static uint8_t bytes[48];
    static int16_t shorts[48];
    CHECK(mvfxInstance_ReadGas(s_context, s_instance, 0, mvfx_gasDensity, floats, 48) ==
              mvfx_success,
          "floats");
    mvfxGasRead read = Whole(mvfx_gasDensity, mvfx_formatFloat32);
    bool right = mvfxInstance_ReadGasAs(s_context, s_instance, 0, &read, wide, sizeof wide) ==
                     mvfx_success &&
                 memcmp(wide, floats, sizeof wide) == 0;
    read.format = mvfx_formatFloat16;
    right = right && mvfxInstance_ReadGasAs(s_context, s_instance, 0, &read, halves,
                                            sizeof halves) == mvfx_success;
    read.format = mvfx_formatUnorm8;
    right = right && mvfxInstance_ReadGasAs(s_context, s_instance, 0, &read, bytes, sizeof bytes) ==
                         mvfx_success;
    read.format = mvfx_formatSnorm16;
    right = right && mvfxInstance_ReadGasAs(s_context, s_instance, 0, &read, shorts,
                                            sizeof shorts) == mvfx_success;
    for (int32_t i = 0; i < 48; ++i)
    {
        float v = floats[i];
        float unorm = fminf(fmaxf((v + 20.0f) / 40.0f, 0.0f), 1.0f);
        float snorm = fminf(fmaxf(2.0f * (v + 20.0f) / 40.0f - 1.0f, -1.0f), 1.0f);
        right = right && halves[i] == ReferenceHalf(v) &&
                bytes[i] == (uint8_t)floorf(unorm * 255.0f + 0.5f) &&
                shorts[i] == (int16_t)copysignf(floorf(fabsf(snorm * 32767.0f) + 0.5f), snorm);
    }
    CHECK(right, "a gas read in formats");
    mvfxDestroyContext(s_context);
}

static void TestBox(void)
{
    // Cells (2, 1) to (5, 3) of the density as float32, a row 32 bytes
    // apart: each row's four values, the 16 bytes after them untouched.
    Open(s_origin, s_unturned);
    static unsigned char out[3 * 32];
    memset(out, 0xAB, sizeof out);
    const mvfxGasRead read = {.field = mvfx_gasDensity,
                              .format = mvfx_formatFloat32,
                              .first = {2, 1, 0},
                              .size = {4, 3, 1},
                              .rowBytes = 32};
    bool right =
        mvfxInstance_ReadGasAs(s_context, s_instance, 0, &read, out, 2 * 32 + 16) == mvfx_success;
    for (int32_t y = 0; y < 3; ++y)
    {
        for (int32_t x = 0; x < 4; ++x)
        {
            float value = 0.0f;
            memcpy(&value, out + y * 32 + x * 4, 4);
            right = right && value == 0.25f + 2.0f * (float)(x + 2) - 3.0f * (float)(y + 1);
        }
        for (int32_t b = 16; b < 32; ++b)
        {
            right = right && out[y * 32 + b] == 0xAB;
        }
    }
    CHECK(right, "a box of cells at a pitch");
    mvfxDestroyContext(s_context);
}

static void TestVelocity(void)
{
    // The X velocity read whole: each cell's the mean of its faces, x +
    // 0.5; the Z velocity of a 2D gas, 0.
    Open(s_origin, s_unturned);
    static float u[48];
    static float w[48];
    mvfxGasRead read = Whole(mvfx_gasVelocityX, mvfx_formatFloat32);
    bool right =
        mvfxInstance_ReadGasAs(s_context, s_instance, 0, &read, u, sizeof u) == mvfx_success;
    read.field = mvfx_gasVelocityZ;
    right = right &&
            mvfxInstance_ReadGasAs(s_context, s_instance, 0, &read, w, sizeof w) == mvfx_success;
    for (int32_t i = 0; i < 48; ++i)
    {
        right = right && u[i] == (float)(i % 8) + 0.5f && w[i] == 0.0f;
    }
    CHECK(right, "a velocity read at the centers");
    mvfxDestroyContext(s_context);
}

// Whether every byte of a buffer is value.
static bool Filled(const void* buffer, size_t size, unsigned char value)
{
    const unsigned char* bytes = buffer;
    for (size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != value)
        {
            return false;
        }
    }
    return true;
}

static void TestRefusals(void)
{
    // A uint32 format; a box past the gas; a range not above its low for a
    // normalized format; a row pitch below the packed row; too few bytes;
    // NULLs; a sample's point NaN or infinite, a negative count, NULL
    // points.
    Open(s_origin, s_unturned);
    static float out[64];
    bool right = true;
    for (int32_t c = 0; c < 6; ++c)
    {
        mvfxGasRead read = Whole(mvfx_gasDensity, c == 0 ? mvfx_formatUint32 : mvfx_formatUnorm8);
        read.size[0] = c == 1 ? 9 : 8;
        read.high = c == 2 ? read.low : read.high;
        read.rowBytes = c == 3 ? 7 : 0;
        int64_t bytes = c == 4 ? 47 : (int64_t)sizeof out;
        const mvfxGasRead* given = c == 5 ? NULL : &read;
        right = right && mvfxInstance_ReadGasAs(s_context, s_instance, 0, given, out, bytes) ==
                             mvfx_errorInvalid;
    }
    const double bad[3] = {0, (double)NAN, 0};
    right = right && mvfxInstance_SampleGas(s_context, s_instance, 0, mvfx_gasDensity, bad, 1,
                                            out) == mvfx_errorInvalid;
    right = right && mvfxInstance_SampleGas(s_context, s_instance, 0, mvfx_gasDensity, bad, -1,
                                            out) == mvfx_errorInvalid;
    right = right && mvfxInstance_SampleGas(s_context, s_instance, 0, mvfx_gasDensity, bad,
                                            INT64_MAX / 3 + 1, out) == mvfx_errorInvalid;
    const double far[3] = {(double)INFINITY, 0, 0};
    right = right && mvfxInstance_SampleGas(s_context, s_instance, 0, mvfx_gasDensity, far, 1,
                                            out) == mvfx_errorInvalid;
    right = right && mvfxInstance_SampleGas(s_context, s_instance, 0, mvfx_gasDensity, NULL, 1,
                                            out) == mvfx_errorInvalid;
    right = right && mvfxInstance_SampleGas(s_context, s_instance, 1, mvfx_gasDensity, bad, 0,
                                            out) == mvfx_errorInvalid;
    CHECK(right, "the reads' refusals");
    // What a refusal leaves: the span the caller gives zeroed, the rest of
    // its buffer untouched.
    const mvfxInstanceId stale = {0};
    memset(out, 0xEE, sizeof out);
    CHECK(mvfxInstance_ReadGas(NULL, s_instance, 0, mvfx_gasDensity, out, 48) ==
                  mvfx_errorInvalid &&
              Filled(out, 48 * sizeof out[0], 0) && Filled(&out[48], 16 * sizeof out[0], 0xEE),
          "a cell read with no context: its 48 floats zeroed");
    memset(out, 0xEE, sizeof out);
    mvfxGasRead whole = Whole(mvfx_gasDensity, mvfx_formatUnorm8);
    CHECK(mvfxInstance_ReadGasAs(s_context, stale, 0, &whole, out, 40) == mvfx_errorStale &&
              Filled(out, 40, 0) && Filled((unsigned char*)out + 40, sizeof out - 40, 0xEE),
          "a read of a stale instance: its 40 bytes zeroed");
    memset(out, 0xEE, sizeof out);
    CHECK(mvfxInstance_SampleGas(s_context, s_instance, 0, mvfx_gasDensity, bad, 1, out) ==
                  mvfx_errorInvalid &&
              out[0] == 0.0f && Filled(&out[1], sizeof out - sizeof out[0], 0xEE),
          "a NaN point: its one sample zeroed");
    mvfxDestroyContext(s_context);
}

static void TestSample(void)
{
    // The instance at (5, 1, 0), turned a quarter about Z, so the box's X
    // is the world's Y: the density at a cell's center in the world is the
    // cell's; between centers the linear field's; outside the box 0; the X
    // velocity at a cell's center its faces' mean.
    const double origin[3] = {5, 1, 0};
    const float quarter[4] = {0, 0, 0.70710678f, 0.70710678f};
    Open(origin, quarter);
    double points[5 * 3];
    // A box point (bx, by) is the world's (5 - by, 1 + bx).
    const float box[4][2] = {{-0.35f, -0.25f}, {0.15f, 0.05f}, {0.0f, 0.1f}, {0.5f, 0.0f}};
    for (int32_t i = 0; i < 4; ++i)
    {
        points[3 * i] = 5.0 - (double)box[i][1];
        points[3 * i + 1] = 1.0 + (double)box[i][0];
        points[3 * i + 2] = 0.0;
    }
    float density[4];
    float u[4];
    CHECK(mvfxInstance_SampleGas(s_context, s_instance, 0, mvfx_gasDensity, points, 4, density) ==
              mvfx_success,
          "sampled");
    CHECK(mvfxInstance_SampleGas(s_context, s_instance, 0, mvfx_gasVelocityX, points, 4, u) ==
              mvfx_success,
          "sampled velocity");
    // Cells at box (bx, by): x = bx / 0.1 + 3.5, y = by / 0.1 + 2.5.
    bool right = true;
    for (int32_t i = 0; i < 3; ++i)
    {
        float x = box[i][0] / 0.1f + 3.5f;
        float y = box[i][1] / 0.1f + 2.5f;
        right = right && fabsf(density[i] - (0.25f + 2.0f * x - 3.0f * y)) <= 1e-4f &&
                fabsf(u[i] - (x + 0.5f)) <= 1e-4f;
    }
    right = right && density[3] == 0.0f && u[3] == 0.0f;
    CHECK(right, "a gas sampled at points");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestFormats();
    TestBox();
    TestVelocity();
    TestRefusals();
    TestSample();
    return s_failures;
}
