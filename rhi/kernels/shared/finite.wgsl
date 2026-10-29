// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A float's class from its bits, spliced where a kernel names it (before
// shared/grid_sample.wgsl, shared/speed.wgsl and shared/colliders.wgsl). WGSL lets an implementation assume no NaN or
// infinity occurs, so a float test of one may be folded: lavapipe folds
// x != x to false and x - x == 0.0 to true. A test on the bits cannot be.

// Whether x is neither a NaN nor an infinity.
fn isFinite(x: f32) -> bool {
    return (bitcast<u32>(x) & 0x7FFFFFFFu) < 0x7F800000u;
}

// Whether x is a NaN.
fn isNan(x: f32) -> bool {
    return (bitcast<u32>(x) & 0x7FFFFFFFu) > 0x7F800000u;
}

// isFinite and isNan of each component.
fn areFinite(v: vec3<f32>) -> vec3<bool> {
    return (bitcast<vec3<u32>>(v) & vec3<u32>(0x7FFFFFFFu)) < vec3<u32>(0x7F800000u);
}

fn areNan(v: vec3<f32>) -> vec3<bool> {
    return (bitcast<vec3<u32>>(v) & vec3<u32>(0x7FFFFFFFu)) > vec3<u32>(0x7F800000u);
}
