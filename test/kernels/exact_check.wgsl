// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The kernels' exact arithmetic (rhi/kernels/shared/exact.wgsl) on the
// device, for test_rhi_kernels to hold against the core's: each element
// four words, two in and two out (eight for remainders apart).

// include: shared/exact.wgsl
// include: shared/drag.wgsl
// include: shared/finite.wgsl
// include: shared/speed.wgsl
// include: shared/sincos.wgsl

const RECIPROCALS: u32 = 0u;
const QUOTIENTS: u32 = 1u;
const CONVERSIONS: u32 = 2u;
const LIFE_TICKS: u32 = 3u;
const SUMS: u32 = 4u;
const REMAINDERS: u32 = 5u;
const RATIOS: u32 = 6u;
const REMAINDERS_APART: u32 = 7u;
const DRAG_FACTORS: u32 = 8u;
const HELD_SPEEDS: u32 = 9u;
const CLASSES: u32 = 10u;
const SINES: u32 = 11u;

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
// lives (two words), the search started as many values a count rounds to
// from its estimate as the second in-word says (signed). Sums: in two 32-bit halves of 64-bit numbers a (low
// words) and b (high words) as a = (w0, w2), b = (w1, w3); out a + b in
// w0 and w2, a - b in w1 and w3. Remainders: in x (w0, w1) and d (w2,
// w3), out x % d in w0 and w1. Remainders apart: eight words an element,
// in x (w0, w1) and d (w2, w3), out x % d in w4 and w5, w6 and w7 never
// written. Ratios: in n (w0, w1) and d (w2, w3), out n / d as binary32
// in w0. Drag factors (shared/drag.wgsl): in a drag (w0) and seconds
// (w1), out the decay in w0, the carry in w2 and the push in w3. Held
// speeds (shared/speed.wgsl): eight words an element, in a velocity (w0
// to w2) and a bound (w3), out the held velocity in w4 to w6. Classes
// (shared/finite.wgsl): in a float (w0), out whether it is finite (w1)
// and whether it is a NaN (w2), 1 or 0, and in w3 areFinite (bits 0 to
// 2) and areNan (bits 3 to 5) of a vector of ones with the float in
// component e % 3. Sines (shared/sincos.wgsl): in an angle (w0), out its
// sine (w1) and cosine (w2).
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
            case LIFE_TICKS: {
                // The search started its in-word's values a count rounds
                // to away from its estimate, as a coarser division would.
                let inverse = bitcast<f32>(words[w]);
                var start = max(ceil(1048576.0 / inverse), 1.0);
                let away = bitcast<i32>(words[w + 1u]);
                for (var k = 0; k < abs(away); k++) {
                    start = select(nextCount(start), max(previousCount(start), 1.0), away < 0);
                }
                let ticks = lifeTicksFrom(inverse, start);
                words[w + 2u] = ticks.x;
                words[w + 3u] = ticks.y;
            }
            case REMAINDERS: {
                let r = remainder64(vec2<u32>(words[w], words[w + 1u]),
                                    vec2<u32>(words[w + 2u], words[w + 3u]));
                words[w] = r.x;
                words[w + 1u] = r.y;
            }
            case REMAINDERS_APART: {
                let a = 2u * w;
                let r = remainder64(vec2<u32>(words[a], words[a + 1u]),
                                    vec2<u32>(words[a + 2u], words[a + 3u]));
                words[a + 4u] = r.x;
                words[a + 5u] = r.y;
            }
            case RATIOS: {
                words[w] = bitcast<u32>(ratio(vec2<u32>(words[w], words[w + 1u]),
                                              vec2<u32>(words[w + 2u], words[w + 3u])));
            }
            case CLASSES: {
                let x = bitcast<f32>(words[w]);
                words[w + 1u] = select(0u, 1u, isFinite(x));
                words[w + 2u] = select(0u, 1u, isNan(x));
                var v = vec3<f32>(1.0);
                v[e % 3u] = x;
                let finite = select(vec3<u32>(0u), vec3<u32>(1u, 2u, 4u), areFinite(v));
                let nan = select(vec3<u32>(0u), vec3<u32>(8u, 16u, 32u), areNan(v));
                words[w + 3u] = finite.x | finite.y | finite.z | nan.x | nan.y | nan.z;
            }
            case SINES: {
                let sc = sinCos(bitcast<f32>(words[w]));
                words[w + 1u] = bitcast<u32>(sc.x);
                words[w + 2u] = bitcast<u32>(sc.y);
            }
            case HELD_SPEEDS: {
                let a = 2u * w;
                let v = vec3<f32>(bitcast<f32>(words[a]), bitcast<f32>(words[a + 1u]),
                                  bitcast<f32>(words[a + 2u]));
                let held = heldSpeed(v, bitcast<f32>(words[a + 3u]));
                words[a + 4u] = bitcast<u32>(held.x);
                words[a + 5u] = bitcast<u32>(held.y);
                words[a + 6u] = bitcast<u32>(held.z);
            }
            case DRAG_FACTORS: {
                let f = factors(bitcast<f32>(words[w]), bitcast<f32>(words[w + 1u]));
                words[w] = bitcast<u32>(f.x);
                words[w + 2u] = bitcast<u32>(f.y);
                words[w + 3u] = bitcast<u32>(f.z);
            }
            case SUMS, default: {
                let a = vec2<u32>(words[w], words[w + 2u]);
                let b = vec2<u32>(words[w + 1u], words[w + 3u]);
                let sum = add64(a, b);
                let difference = subtract64(a, b);
                words[w] = sum.x;
                words[w + 2u] = sum.y;
                words[w + 1u] = difference.x;
                words[w + 3u] = difference.y;
            }
        }
    }
}
