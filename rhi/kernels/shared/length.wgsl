// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A vector's length, without bindings, spliced where a kernel names it
// (sim/units.wgsl, sim/aero.wgsl and gas.wgsl's confinement).

// v's length, as the core's mvfxLengthOf: measured scaled by 2^-64 where
// its largest component reaches 2^63, so its squares' sum never
// overflows, and every shorter vector's as sqrt of that sum.
fn lengthOf(v: vec3<f32>) -> f32 {
    let most = max(abs(v.x), max(abs(v.y), abs(v.z)));
    if (!(most >= 0x1p63f)) {
        return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }
    let u = v * 0x1p-64f;
    return sqrt(u.x * u.x + u.y * u.y + u.z * u.z) * 0x1p64f;
}
