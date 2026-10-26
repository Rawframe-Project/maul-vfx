// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A record field's formats for what no public path hands them: every
// value a record shows is finite, so infinities and NaNs reach the
// conversions only here.

#include "layout.h"
#include "test_harness.h"

#include <math.h>
#include <string.h>

// Writes values as a field of a format and components at offset 0 of out.
static void Write(mvfxFormat format, int32_t components, const float values[4],
                  unsigned char out[16])
{
    const mvfxLayoutField field = {
        .field = mvfx_recordColor, .format = format, .components = (uint8_t)components};
    static const uint32_t words[4] = {0};
    memset(out, 0xEE, 16);
    mvfxWriteField(&field, values, words, out);
}

static void TestNotFinite(void)
{
    unsigned char b[16];
    uint16_t h[4];
    const float odd[4] = {INFINITY, -INFINITY, NAN, -NAN};
    Write(mvfx_formatFloat16, 4, odd, b);
    memcpy(h, b, 8);
    CHECK(h[0] == 0x7C00u && h[1] == 0xFC00u, "binary16: each infinity as itself");
    CHECK(h[2] == 0x7E00u && h[3] == 0xFE00u, "binary16: a NaN as the quiet NaN of its sign");
    Write(mvfx_formatUnorm8, 4, odd, b);
    CHECK(b[0] == 255 && b[1] == 0 && b[2] == 0 && b[3] == 0, "unorm8: NaN to 0");
    Write(mvfx_formatSnorm8, 4, odd, b);
    CHECK((int8_t)b[0] == 127 && (int8_t)b[1] == -127 && b[2] == 0 && b[3] == 0,
          "snorm8: NaN to 0");
    Write(mvfx_formatUnorm16, 4, odd, b);
    memcpy(h, b, 8);
    CHECK(h[0] == 65535 && h[1] == 0 && h[2] == 0 && h[3] == 0, "unorm16: NaN to 0");
    Write(mvfx_formatSnorm16, 4, odd, b);
    memcpy(h, b, 8);
    CHECK((int16_t)h[0] == 32767 && (int16_t)h[1] == -32767 && h[2] == 0 && h[3] == 0,
          "snorm16: NaN to 0");
}

int main(void)
{
    TestNotFinite();
    return s_failures == 0 ? 0 : 1;
}
