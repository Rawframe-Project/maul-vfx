// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's field sampled at a host's points, as the core's
// mvfxInstance_SampleGas (src/gas_read.c's SampleAt): an invocation a
// point, mapped into the box's frame, the field trilinear there, held
// within its array's points (a velocity's component its faces'; 0 across
// an axis of one cell); outside the box the query's value (0, a
// surface's distance its reach), a 2D gas's reaching along its Z without
// end. Points are in the world less the query's origin, so binary32
// holds them near it.

// include: shared/gas_numbers.wgsl

// include: shared/gas_lattice.wgsl

const ROW: u32 = 2097152u;

// A query: its points, the field (a scalar's index, or 3 and up a
// velocity's axis), the buffer current, the points' first word; the map
// into the box's frame, a turn and then an offset (the points scaled by
// into.w first); the samples' first word, and the value outside the box's
// bits.
struct Sample {
    head: vec4<u32>,
    rotation: vec4<f32>,
    into: vec4<f32>,
    tail: vec4<u32>,
}

@group(0) @binding(0) var<uniform> gas: Gas;
@group(0) @binding(1) var<storage, read> fields: array<f32>;
@group(0) @binding(2) var<uniform> query: Sample;
@group(0) @binding(3) var<storage, read_write> out: array<u32>;
@group(0) @binding(4) var<storage, read> points: array<f32>;

// include: shared/gas_sample.wgsl

fn rotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// The field at a place of the box's frame.
fn sampled(local: vec3<f32>) -> f32 {
    let n = sizeOf();
    var p: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        p[k] = local[k] / gas.lattice.x + 0.5 * f32(n[k]);
        if (n[k] > 1u && !(p[k] >= 0.0 && p[k] <= f32(n[k]))) {
            return bitcast<f32>(query.tail.y);
        }
    }
    let field = query.head.y;
    let b = query.head.z;
    if (field < 3u) {
        return sampleAt(gas.cells[b][field], 3u, p);
    }
    let k = field - 3u;
    if (n[k] == 1u) {
        return 0.0;
    }
    return sampleAt(gas.faces[b][k], k, p);
}

@compute @workgroup_size(64, 1, 1)
fn gasSample(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x + id.y * ROW;
    if (i >= query.head.x) {
        return;
    }
    let at = query.head.w + 3u * i;
    let p = vec3<f32>(points[at], points[at + 1u], points[at + 2u]);
    let local = rotate(query.rotation, p * query.into.w) + query.into.xyz;
    out[query.tail.x + i] = bitcast<u32>(sampled(local));
}
