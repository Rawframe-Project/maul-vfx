// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A waves surface's step on the GPU, as the core's mvfxStepWater
// (src/water_step.c):
//  - waterSink lowers each cell by the pushes over it in order, as the
//    core's Sink does push by push, then lifts it by what they sank
//    spread evenly (Displace), the lift the host's sum;
//  - waterSubstep is a substep whole: the core's rate and height passes
//    fuse per cell (a cell's height reads only its own new rate), and an
//    open wall's cell then takes Mur's edge from its inward neighbor's
//    new height, as OpenWalls leaves it;
//  - waterSettle, one workgroup, reduces the lowest and highest height
//    and the fastest rate into the statistics record (whether it moves,
//    its reach), as Settle.
// Heights and rates each in two buffers, the root block's word 0 the one
// current. A surface has at most 1,048,576 cells, 16,384 workgroups of
// 64: a dispatch's one row.

const STILL: f32 = 1e-6;
const SETTLE_GROUP: u32 = 256u;
const WALL_MIN_X: u32 = 1u;
const WALL_MAX_X: u32 = 2u;
const WALL_MIN_Z: u32 = 4u;
const WALL_MAX_Z: u32 = 8u;
// The statistics record's words (rhi/include/maul-vfx-rhi/sim.h's
// MVFX_GPU_WATER_*): whether it moves, its reach.
const HOLDS: u32 = 0u;
const REACH: u32 = 4u;

// include: shared/water_numbers.wgsl

// The buffer current; the rest unused.
struct Root {
    now: u32,
    unused0: u32,
    unused1: u32,
    unused2: u32,
}

@group(0) @binding(0) var<uniform> water: Water;
// Heights now and next, then rates now and next: four arrays of the cells.
@group(0) @binding(1) var<storage, read_write> fields: array<f32>;
@group(0) @binding(2) var<storage, read_write> statistics: array<u32>;
var<immediate> root: Root;

fn height(b: u32, i: u32) -> f32 {
    return fields[b * water.size.z + i];
}

fn rate(b: u32, i: u32) -> f32 {
    return fields[(2u + b) * water.size.z + i];
}

// mvfxWaterSpan: the cells along an axis of n within reach of center.
fn span(center: f32, reach: f32, n: u32) -> vec2<i32> {
    let first = i32(min(max(ceil(center - reach), 0.0), f32(n)));
    let last = i32(min(max(floor(center + reach), -1.0), f32(n) - 1.0));
    return vec2<i32>(first, last);
}

@compute @workgroup_size(64, 1, 1)
fn waterSink(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    if (i >= water.size.z) {
        return;
    }
    let n0 = water.size.x;
    let x = i32(i % n0);
    let z = i32(i / n0);
    let h = water.sink.x;
    var u = height(root.now, i);
    for (var p = 0u; p < water.head.x; p++) {
        let push = water.pushes[p];
        let cx = push.x / h + 0.5 * f32(n0) - 0.5;
        let cz = push.y / h + 0.5 * f32(water.size.y) - 0.5;
        let reach = push.z / h;
        let xs = span(cx, reach, n0);
        let zs = span(cz, reach, water.size.y);
        if (x < xs.x || x > xs.y || z < zs.x || z > zs.y) {
            continue;
        }
        let dx = (f32(x) - cx) / reach;
        let dz = (f32(z) - cz) / reach;
        let s = 1.0 - (dx * dx + dz * dz);
        u -= select(0.0, push.w * s * s, s > 0.0);
    }
    fields[root.now * water.size.z + i] = u + water.sink.y;
}

// Whether cell i lies on an open wall.
fn onOpenWall(i: u32) -> bool {
    let n0 = water.size.x;
    let x = i % n0;
    let z = i / n0;
    let closed = water.size.w;
    return (x == 0u && (closed & WALL_MIN_X) == 0u) ||
           (x == n0 - 1u && (closed & WALL_MAX_X) == 0u) ||
           (z == 0u && (closed & WALL_MIN_Z) == 0u) ||
           (z == water.size.y - 1u && (closed & WALL_MAX_Z) == 0u);
}

// An open wall cell's inward neighbor, as OpenWalls takes it: by its X
// wall first, so the corners go by theirs.
fn inwardOf(i: u32) -> u32 {
    let n0 = water.size.x;
    let x = i % n0;
    let z = i / n0;
    let closed = water.size.w;
    if (x == 0u && (closed & WALL_MIN_X) == 0u) {
        return i + 1u;
    }
    if (x == n0 - 1u && (closed & WALL_MAX_X) == 0u) {
        return i - 1u;
    }
    return select(i - n0, i + n0, z == 0u);
}

// RateRows then HeightRows for cell i: its new rate and its new height.
fn passes(i: u32) -> vec2<f32> {
    let n0 = water.size.x;
    let x = i % n0;
    let z = i / n0;
    let b = root.now;
    // Beyond an edge, the edge's own value: no slope across it.
    let u = height(b, i);
    let left = select(u, height(b, i - 1u), x > 0u);
    let right = select(u, height(b, i + 1u), x + 1u < n0);
    let down = select(u, height(b, i - n0), z > 0u);
    let up = select(u, height(b, i + n0), z + 1u < water.size.y);
    let laplacian = left + right + down + up - 4.0 * u;
    let v = (rate(b, i) + water.factors.x * laplacian) * water.factors.y;
    return vec2<f32>(v, u + water.factors.w * v);
}

// Mur's edge for open wall cell i from its inward neighbor's new height.
fn absorbed(i: u32, inwardNext: f32) -> f32 {
    let b = root.now;
    return height(b, inwardOf(i)) + water.factors.z * (inwardNext - height(b, i));
}

// Cell j's new height as OpenWalls has it when it reaches cell reader:
// OpenWalls goes by index, so an open wall cell before the reader has its
// Mur value already and any other cell the height pass's. Each absorbed
// cell's value hangs on its own inward neighbor's as it found it: a chain
// of at most two (every grid of 2 to 6 cells a side and every set of
// walls checked against the core's loop).
fn nextAt(j: u32, reader: u32) -> f32 {
    var chain: array<u32, 2>;
    var depth = 0u;
    var cell = j;
    var by = reader;
    while (depth < 2u && cell < by && onOpenWall(cell)) {
        chain[depth] = cell;
        depth++;
        by = cell;
        cell = inwardOf(cell);
    }
    var next = passes(cell).y;
    for (var k = depth; k > 0u; k--) {
        next = absorbed(chain[k - 1u], next);
    }
    return next;
}

@compute @workgroup_size(64, 1, 1)
fn waterSubstep(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    if (i >= water.size.z) {
        return;
    }
    let b = root.now;
    let c = water.size.z;
    var result = passes(i);
    if (onOpenWall(i)) {
        // Its rate what moved it.
        let u = absorbed(i, nextAt(inwardOf(i), i));
        result = vec2<f32>((u - height(b, i)) / water.factors.w, u);
    }
    fields[(3u - b) * c + i] = result.x;
    fields[(1u - b) * c + i] = result.y;
}

var<workgroup> lows: array<f32, 256>;
var<workgroup> highs: array<f32, 256>;
var<workgroup> fasts: array<f32, 256>;

// min and max are order-free, so the reduction's tree gives the core's.
@compute @workgroup_size(256, 1, 1)
fn waterSettle(@builtin(local_invocation_index) lane: u32) {
    let b = root.now;
    let first = height(b, 0u);
    var low = first;
    var high = first;
    var fast = 0.0;
    for (var i = lane; i < water.size.z; i += SETTLE_GROUP) {
        let u = height(b, i);
        low = min(low, u);
        high = max(high, u);
        fast = max(fast, abs(rate(b, i)));
    }
    lows[lane] = low;
    highs[lane] = high;
    fasts[lane] = fast;
    workgroupBarrier();
    for (var half = SETTLE_GROUP / 2u; half > 0u; half >>= 1u) {
        if (lane < half) {
            lows[lane] = min(lows[lane], lows[lane + half]);
            highs[lane] = max(highs[lane], highs[lane + half]);
            fasts[lane] = max(fasts[lane], fasts[lane + half]);
        }
        workgroupBarrier();
    }
    if (lane == 0u) {
        let moves = highs[0] - lows[0] > STILL || fasts[0] > STILL;
        statistics[HOLDS] = select(0u, 1u, moves);
        statistics[REACH] = bitcast<u32>(max(-lows[0], highs[0]));
    }
}
