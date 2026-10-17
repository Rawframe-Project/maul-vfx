// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A test's depth image rendered into a depth texture: a triangle covering
// the target, each fragment writing its pixel's device depth from a
// buffer (the image's width in its first word's bits, then the pixels by
// rows from the top), as binary32 depth stores them exactly.

@group(0) @binding(0) var<storage, read> image: array<f32>;

@vertex
fn cover(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
    let x = f32((i << 1u) & 2u) * 2.0 - 1.0;
    let y = f32(i & 2u) * 2.0 - 1.0;
    return vec4<f32>(x, y, 0.0, 1.0);
}

struct Depth {
    @builtin(frag_depth) depth: f32,
}

@fragment
fn fill(@builtin(position) p: vec4<f32>) -> Depth {
    let width = bitcast<u32>(image[0]);
    return Depth(image[1u + u32(p.y) * width + u32(p.x)]);
}
