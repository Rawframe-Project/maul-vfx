// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The kernels' exact arithmetic (rhi/kernels/shared/exact.wgsl) on the
// device, for test_rhi_kernels to hold against the core's: each element
// four words, two in and two out.

// include: exact.wgsl

const RECIPROCALS: u32 = 0u;
const QUOTIENTS: u32 = 1u;
const CONVERSIONS: u32 = 2u;
const LIFE_TICKS: u32 = 3u;

// What is checked; the first significand of reciprocals; the elements;
// the workgroups dispatched.
struct Root {
    mode: u32,
    first: u32,
    count: u32,
    groups: u32,
}

var<immediate> root: Root;

@group(0) @binding(0) var<storage, read_write> words: array<u32>;

// Reciprocals: 1 over each significand of [1, 2) from first, its bits
// out. Quotients: in a and b, out a / b. Conversions: in a 64-bit count,
// out its binary32. Life ticks: in an inverse life, out the ticks it
// lives (two words).
@compute @workgroup_size(64, 1, 1)
fn check(@builtin(global_invocation_id) id: vec3<u32>) {
    for (var e = id.x; e < root.count; e += root.groups * 64u) {
        let w = 4u * e;
        switch (root.mode) {
            case RECIPROCALS: {
                let x = bitcast<f32>(0x3F800000u | (root.first + e));
                words[w + 2u] = bitcast<u32>(divide(1.0, x));
            }
            case QUOTIENTS: {
                words[w + 2u] = bitcast<u32>(divide(bitcast<f32>(words[w]),
                                                    bitcast<f32>(words[w + 1u])));
            }
            case CONVERSIONS: {
                words[w + 2u] = bitcast<u32>(nearestFloat(words[w], words[w + 1u]));
            }
            case LIFE_TICKS, default: {
                let ticks = lifeTicks(bitcast<f32>(words[w]));
                words[w + 2u] = ticks.x;
                words[w + 3u] = ticks.y;
            }
        }
    }
}
