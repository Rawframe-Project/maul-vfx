// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A water surface sampled at a host's points, as the core's
// mvfxInstance_SampleWater (src/water_api.c's SampleOne): an invocation a
// point, mapped into the surface's frame, its surface along the frame's
// Y over it (heights, velocities and normals bilinear between the cells'
// centers, held at the outer cells' within the grid's edge; off the grid
// the rest plane's, still), mapped back. Points and samples are in the
// world less the query's origin, so binary32 holds them near it.

// include: shared/collider_numbers.wgsl

// include: shared/water_numbers.wgsl

const ROW: u32 = 2097152u;
// A sample's words (MVFX_GPU_WATER_SAMPLE_WORDS): the surface's place,
// its normal, its velocity, and whether the point is over the grid.
const SAMPLE_WORDS: u32 = 10u;

// A query: its points, the buffer current, the points' first word and the
// samples'; the map into the surface's frame, a turn and then an offset
// (the points scaled by into.w first); the offset back after the turn
// back and the scale undone.
struct Sample {
    head: vec4<u32>,
    rotation: vec4<f32>,
    into: vec4<f32>,
    back: vec4<f32>,
}

@group(0) @binding(0) var<uniform> water: Water;
@group(0) @binding(1) var<storage, read> fields: array<f32>;
@group(0) @binding(2) var<uniform> query: Sample;
@group(0) @binding(3) var<storage, read_write> out: array<u32>;
@group(0) @binding(4) var<storage, read> points: array<f32>;

// The buffer the fields are read from.
fn current() -> u32 {
    return query.head.y;
}

// include: shared/water_cells.wgsl

fn rotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// The cell below a point of the frame (x, z) and its fractions toward the
// next (mvfxWaterCorners); false off the grid.
fn corners(x: f32, z: f32, lo: ptr<function, vec2<u32>>, t: ptr<function, vec2<f32>>) -> bool {
    let n = water.size.xy;
    let h = water.sink.x;
    let p = vec2<f32>(x / h + 0.5 * f32(n.x), z / h + 0.5 * f32(n.y));
    if (!(p.x >= 0.0 && p.x <= f32(n.x) && p.y >= 0.0 && p.y <= f32(n.y))) {
        return false;
    }
    for (var k = 0u; k < 2u; k++) {
        let c = min(max(p[k] - 0.5, 0.0), f32(n[k] - 1u));
        let i = u32(c);
        (*lo)[k] = select(n[k] - 2u, i, i < n[k] - 1u);
        (*t)[k] = c - f32((*lo)[k]);
    }
    return true;
}

// A corner's bilinear share (mvfxWaterShare).
fn share(t: vec2<f32>, corner: u32) -> f32 {
    let a = select(1.0 - t.x, t.x, (corner & 1u) != 0u);
    let b = select(1.0 - t.y, t.y, (corner >> 1u) != 0u);
    return a * b;
}

@compute @workgroup_size(64, 1, 1)
fn waterSample(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x + id.y * ROW;
    if (i >= query.head.x) {
        return;
    }
    let at = query.head.z + 3u * i;
    let p = vec3<f32>(points[at], points[at + 1u], points[at + 2u]);
    let local = rotate(query.rotation, p * query.into.w) + query.into.xyz;
    var height = 0.0;
    var velocity = vec3<f32>(0.0);
    var normal = vec3<f32>(0.0, 1.0, 0.0);
    var lo = vec2<u32>(0u);
    var t = vec2<f32>(0.0);
    let inside = corners(local.x, local.z, &lo, &t);
    if (inside) {
        // The normals' blend, its length taken once after the turn back.
        normal = vec3<f32>(0.0);
        for (var corner = 0u; corner < 4u; corner++) {
            let cx = lo.x + (corner & 1u);
            let cz = lo.y + (corner >> 1u);
            let w = share(t, corner);
            height += w * surfaceAt(cx, cz);
            velocity += w * velocityAt(cx, cz);
            normal += w * normalAt(cx, cz);
        }
    }
    // Back into the world: the turn's inverse, the scale undone.
    let back = vec4<f32>(-query.rotation.xyz, query.rotation.w);
    let surface = rotate(back, vec3<f32>(local.x, height, local.z)) / query.into.w + query.back.xyz;
    let turned = rotate(back, normal) / query.into.w;
    let length = sqrt(turned.x * turned.x + turned.y * turned.y + turned.z * turned.z);
    let moved = rotate(back, velocity) / query.into.w;
    let words = query.head.w + SAMPLE_WORDS * i;
    for (var k = 0u; k < 3u; k++) {
        out[words + k] = bitcast<u32>(surface[k]);
        out[words + 3u + k] = bitcast<u32>(turned[k] / length);
        out[words + 6u + k] = bitcast<u32>(moved[k]);
    }
    out[words + 9u] = select(0u, 1u, inside);
}
