// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A sheet's triangle's normal and its share of its relative speed,
// without bindings, after shared/exact.wgsl: sim/aero.wgsl takes them,
// and test/kernels/exact_check.wgsl holds them against the core's.

// A triangle's edges' cross product, as the core's mvfxCrossEdges: both
// edges scaled by 2^-64 when a component of either reaches 2^63 (w 1
// then, else 0), so no product overflows.
fn crossEdges(e1: vec3<f32>, e2: vec3<f32>) -> vec4<f32> {
    let most = max(max(abs(e1.x), max(abs(e1.y), abs(e1.z))),
                   max(abs(e2.x), max(abs(e2.y), abs(e2.z))));
    let scaled = most >= 0x1p63f;
    let s = select(1.0, 0x1p-64f, scaled);
    let a = e1 * s;
    let b = e2 * s;
    return vec4<f32>(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                     a[0] * b[1] - a[1] * b[0], select(0.0, 1.0, scaled));
}

// The share of a relative speed a triangle takes over a substep of h
// seconds at a coefficient k, as the core's mvfxAirShare: k A speed h over
// its three points, at most a sixth, A half (times 2^128 when scaled);
// none for a zero coefficient, area or speed. Its factors' exponents decide a
// share past a sixth before the product is formed: WGSL lets a device
// assume no infinity occurs. Below that, the significands' product in
// integers (exact.wgsl's product), so no compiler reorders it, scaled by
// 2^E from its bits: 0 where that falls below binary32's least normal,
// whose share a device may flush.
fn airShare(k: f32, half: f32, scaled: bool, speed: f32, h: f32) -> f32 {
    if (!(k > 0.0) || !(half > 0.0) || !(speed > 0.0)) {
        return 0.0;
    }
    let fk = frexp(k);
    let fa = frexp(half);
    let fs = frexp(speed);
    let fh = frexp(h);
    let ea = fa.exp + select(0, 128, scaled);
    let e = fk.exp + ea + fs.exp + fh.exp;
    if (e >= 3) {
        return 1.0 / 6.0;
    }
    let m = bitcast<u32>(product(product(product(fk.fract, fa.fract), fs.fract), fh.fract));
    let biased = i32(m >> 23u) + e;
    var whole = 0.0;
    if (biased >= 1) {
        whole = bitcast<f32>((u32(biased) << 23u) | (m & 0x007FFFFFu));
    }
    let share = quotient(whole, 3.0);
    return select(1.0 / 6.0, share, share < 1.0 / 6.0);
}
