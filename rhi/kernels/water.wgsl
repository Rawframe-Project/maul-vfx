// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A water surface's step on the GPU: waves as the core's mvfxStepWater
// (src/water_step.c), shallow water (below) as its mvfxStepShallow.
// Waves:
//  - waterCrossSum, one workgroup, sums what the coupled emitters'
//    crossings sink (mvfxSinkWaves' sums), for the even lift;
//  - waterSink lowers each cell by the pushes over it in order, then by
//    the crossings in order, as the core's Sink does push by push, then
//    lifts it by what they sank spread evenly (Displace), the pushes'
//    lift the host's sum, the crossings' the device's;
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
// MVFX_GPU_WATER_*): whether it moves, its speed, its reach, the faces
// held and the volume poured.
const HOLDS: u32 = 0u;
const SPEED: u32 = 3u;
const REACH: u32 = 4u;
const FACES_HELD: u32 = 5u;
const POURED: u32 = 6u;

// include: shared/collider_numbers.wgsl

// include: shared/water_numbers.wgsl

// include: shared/grid_sample.wgsl

// The coupled emitters' crossings (rhi/src/sim_water.c copies their lists
// in before the step): the waves' crossings' sunk water summed (binary32
// bits) and 3 words unused, then each coupled emitter's list in its step
// order (sim/water.wgsl's: a head of CROSSING_HEAD words, how many, the
// room's capacity, each crossing's reach, depth and volume; then each
// crossing's X and Z), as long as its room.
const CROSSINGS_FIRST: u32 = 4u;
const CROSSING_HEAD: u32 = 8u;

// The list after the one at `at`.
fn nextList(at: u32) -> u32 {
    return at + CROSSING_HEAD + 2u * crossings[at + 1u];
}

// Crossing c of the list at `at`: its X and Z.
fn crossingAt(at: u32, c: u32) -> vec2<f32> {
    let w = at + CROSSING_HEAD + 2u * c;
    return vec2<f32>(bitcast<f32>(crossings[w]), bitcast<f32>(crossings[w + 1u]));
}

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
@group(0) @binding(2) var<storage, read_write> statistics: array<atomic<u32>>;
// A shallow surface's terrain's grid (its effect's, or the host's).
@group(0) @binding(3) var<storage, read> grids: array<u32>;
@group(0) @binding(4) var<storage, read_write> crossings: array<u32>;
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

// What a push or crossing at (X, Z) of radius and depth sinks cell (x,
// z) by (mvfxSinkWaves' cell): depth (1 - r^2)^2 within its radius.
fn sunk(x: i32, z: i32, at: vec2<f32>, radius: f32, depth: f32) -> f32 {
    let h = water.sink.x;
    let n = water.size.xy;
    let cx = at.x / h + 0.5 * f32(n.x) - 0.5;
    let cz = at.y / h + 0.5 * f32(n.y) - 0.5;
    let reach = radius / h;
    let xs = span(cx, reach, n.x);
    let zs = span(cz, reach, n.y);
    if (x < xs.x || x > xs.y || z < zs.x || z > zs.y) {
        return 0.0;
    }
    let dx = (f32(x) - cx) / reach;
    let dz = (f32(z) - cz) / reach;
    let s = 1.0 - (dx * dx + dz * dz);
    return select(0.0, depth * s * s, s > 0.0);
}

// What a crossing sinks over all its cells (mvfxSinkWaves' sum).
fn sankBy(at: vec2<f32>, radius: f32, depth: f32) -> f32 {
    let h = water.sink.x;
    let n = water.size.xy;
    let xs = span(at.x / h + 0.5 * f32(n.x) - 0.5, radius / h, n.x);
    let zs = span(at.y / h + 0.5 * f32(n.y) - 0.5, radius / h, n.y);
    var sum = 0.0;
    for (var z = zs.x; z <= zs.y; z++) {
        for (var x = xs.x; x <= xs.y; x++) {
            sum += sunk(x, z, at, radius, depth);
        }
    }
    return sum;
}

var<workgroup> crossSums: array<f32, 256>;

@compute @workgroup_size(256, 1, 1)
fn waterCrossSum(@builtin(local_invocation_index) lane: u32) {
    var sum = 0.0;
    var list = CROSSINGS_FIRST;
    for (var k = 0u; k < water.faces.z; k++) {
        let count = crossings[list];
        let reach = bitcast<f32>(crossings[list + 2u]);
        let depth = bitcast<f32>(crossings[list + 3u]);
        for (var c = lane; c < count; c += SETTLE_GROUP) {
            sum += sankBy(crossingAt(list, c), reach, depth);
        }
        list = nextList(list);
    }
    crossSums[lane] = sum;
    workgroupBarrier();
    for (var half = SETTLE_GROUP / 2u; half > 0u; half >>= 1u) {
        if (lane < half) {
            crossSums[lane] += crossSums[lane + half];
        }
        workgroupBarrier();
    }
    if (lane == 0u) {
        crossings[0] = bitcast<u32>(crossSums[0]);
    }
}

@compute @workgroup_size(64, 1, 1)
fn waterSink(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    if (i >= water.size.z) {
        return;
    }
    let x = i32(i % water.size.x);
    let z = i32(i / water.size.x);
    var u = height(root.now, i);
    for (var p = 0u; p < water.head.x; p++) {
        let push = water.pushes[p];
        u -= sunk(x, z, push.xy, push.z, push.w);
    }
    // Then the crossings, emitter by emitter, each in order.
    var list = CROSSINGS_FIRST;
    for (var k = 0u; k < water.faces.z; k++) {
        let reach = bitcast<f32>(crossings[list + 2u]);
        let depth = bitcast<f32>(crossings[list + 3u]);
        for (var c = 0u; c < crossings[list]; c++) {
            u -= sunk(x, z, crossingAt(list, c), reach, depth);
        }
        list = nextList(list);
    }
    // The crossings' share of the lift, their sum written when any emitter
    // is coupled.
    let crossed = select(0.0, bitcast<f32>(crossings[0]) / f32(water.size.z), water.faces.z > 0u);
    let lift = water.sink.y + crossed;
    fields[root.now * water.size.z + i] = u + lift;
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
        _ = atomicExchange(&statistics[HOLDS], select(0u, 1u, moves));
        _ = atomicExchange(&statistics[REACH], bitcast<u32>(max(-lows[0], highs[0])));
    }
}

// Shallow water (src/water_shallow.c and src/water_ground.c): depths now
// and next, X faces now and next, Z faces now and next, the givers'
// scales and the ground, in that order in the fields.
//  - shallowStart, a cell each: its ground under the terrain, the first
//    fill, each source over it in order;
//  - shallowSpill, one workgroup: the pushes in order, each taking water
//    within its radius into its ring (or the ring's into its middle),
//    then the coupled emitters' crossings in order, each such a spill
//    and a pour of its volume;
//  - shallowFaces, shallowGiving and shallowDepths, a substep's three
//    passes: each face carried, slowed and accelerated; each cell's
//    outflows' scale; each cell's depth from its four faces' fluxes;
//  - shallowSettle, one workgroup: the reach, whether any face moves and
//    the fastest speed into the statistics record.

const DRY: f32 = 1e-6;
const ON_EDGE: f32 = 1e-3;

fn faceCount(axis: u32) -> u32 {
    return select(water.faces.y, water.faces.x, axis == 0u);
}

// The first float of an axis's faces in buffer b.
fn facesAt(axis: u32, b: u32) -> u32 {
    let first = 2u * water.size.z + select(2u * water.faces.x, 0u, axis == 0u);
    return first + b * faceCount(axis);
}

fn givingAt() -> u32 {
    return 2u * (water.size.z + water.faces.x + water.faces.y);
}

fn groundAt() -> u32 {
    return givingAt() + water.size.z;
}

// The height along a vertical, from start along along in the field's
// frame, where it meets the field's height under the point at at; false
// where that point is outside the field's box (water_ground.c's Meet).
fn meet(start: vec3<f32>, along: vec3<f32>, at: f32, height: ptr<function, f32>) -> bool {
    let t = water.terrain;
    let point = vec2<f32>(start.x + at * along.x, start.z + at * along.z);
    let u = vec3<f32>(point.x / (2.0 * t.halfExtents.x) + 0.5, 0.5,
                      point.y / (2.0 * t.halfExtents.z) + 0.5);
    var value = vec3<f32>(0.0);
    if (!sampleGrid(t.grid, t.gridModes.x, 1u, u, BORDER_NONE, &value)) {
        return false;
    }
    *height = (value.x * t.scales.x - start.y) / along.y;
    return true;
}

// The ground under a point (x, z) of the surface's frame, as HeightAt:
// the vertical's meeting with the field, Aitken's step under a lean, the
// floor where it meets the field outside its box or runs along it.
fn groundUnder(x: f32, z: f32) -> f32 {
    let t = water.terrain;
    let back = vec4<f32>(-t.rotation.xyz, t.rotation.w);
    let start = rotate(back, vec3<f32>(x - t.center.x, -t.center.y, z - t.center.z));
    let along = rotate(back, vec3<f32>(0.0, 1.0, 0.0));
    var first = 0.0;
    if (abs(along.y) < ON_EDGE || !meet(start, along, 0.0, &first)) {
        return 0.0;
    }
    var second = first;
    if ((along.x == 0.0 && along.z == 0.0) || first == 0.0 || !meet(start, along, first, &second)) {
        return first;
    }
    let drift = (second - first) / first;
    return select(second, first / (1.0 - drift), drift < 1.0 - ON_EDGE);
}

// Whether source k covers cell (x, z): within its span (mvfxWaterSpan
// about its center) and, for a sphere, its disk.
fn covers(k: u32, x: u32, z: u32) -> bool {
    let n = water.size.xy;
    let h = water.sink.x;
    let footprint = water.sources[2u * k];
    let shape = water.sources[2u * k + 1u];
    let xs = span(footprint.x / h + 0.5 * f32(n.x) - 0.5, footprint.z / h, n.x);
    let zs = span(footprint.y / h + 0.5 * f32(n.y) - 0.5, footprint.w / h, n.y);
    if (i32(x) < xs.x || i32(x) > xs.y || i32(z) < zs.x || i32(z) > zs.y) {
        return false;
    }
    let dx = (f32(x) + 0.5 - 0.5 * f32(n.x)) * h - footprint.x;
    let dz = (f32(z) + 0.5 - 0.5 * f32(n.y)) * h - footprint.y;
    return shape.x != 0.0 || dx * dx + dz * dz <= shape.z * shape.z;
}

@compute @workgroup_size(64, 1, 1)
fn shallowStart(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    if (i >= water.size.z) {
        return;
    }
    let n = water.size.xy;
    let x = i % n.x;
    let z = i / n.x;
    let h = water.sink.x;
    var ground = 0.0;
    if ((water.head.w & FLAG_TERRAIN) != 0u) {
        ground = groundUnder((f32(x) + 0.5 - 0.5 * f32(n.x)) * h,
                             (f32(z) + 0.5 - 0.5 * f32(n.y)) * h);
    }
    fields[groundAt() + i] = ground;
    let at = root.now * water.size.z + i;
    var d = fields[at];
    if ((water.head.w & FLAG_FILLS) != 0u) {
        d = max(water.factors.z - ground, 0.0);
    }
    for (var k = 0u; k < water.head.z; k++) {
        d += select(0.0, water.sources[2u * k + 1u].y, covers(k, x, z));
    }
    fields[at] = d;
}

var<workgroup> spillSums: array<f32, 256>;

// A push's weight at r radii from its center: within the radius
// (1 - r^2)^2, which it sinks by; in the ring to twice it
// (1 - (2 r - 3)^2)^2, which it spills into (water_ground.c's Weight).
fn spillWeight(r: f32, inner: ptr<function, bool>) -> f32 {
    *inner = r < 1.0;
    let s = select(1.0 - (2.0 * r - 3.0) * (2.0 * r - 3.0), 1.0 - r * r, *inner);
    return select(0.0, s * s, r < 2.0 && s > 0.0);
}

@compute @workgroup_size(256, 1, 1)
fn shallowSpill(@builtin(local_invocation_index) lane: u32) {
    let n = water.size.xy;
    let h = water.sink.x;
    let base = root.now * water.size.z;
    for (var p = 0u; p < water.head.x; p++) {
        let push = water.pushes[p];
        let c = vec2<f32>(push.x / h + 0.5 * f32(n.x) - 0.5, push.y / h + 0.5 * f32(n.y) - 0.5);
        let reach = push.z / h;
        let sinks = push.w > 0.0;
        let xs = span(c.x, 2.0 * reach, n.x);
        let zs = span(c.y, 2.0 * reach, n.y);
        let w = u32(max(xs.y - xs.x + 1, 0));
        let count = w * u32(max(zs.y - zs.x + 1, 0));
        // What the push takes, summed over the workgroup.
        var sum = 0.0;
        for (var k = lane; k < count; k += SETTLE_GROUP) {
            let x = xs.x + i32(k % w);
            let z = zs.x + i32(k / w);
            let dx = f32(x) - c.x;
            let dz = f32(z) - c.y;
            var inner = false;
            let wt = spillWeight(sqrt(dx * dx + dz * dz) / reach, &inner);
            let d = fields[base + u32(z) * n.x + u32(x)];
            sum += select(0.0, min(abs(push.w) * wt, d), inner == sinks);
        }
        spillSums[lane] = sum;
        workgroupBarrier();
        for (var half = SETTLE_GROUP / 2u; half > 0u; half >>= 1u) {
            if (lane < half) {
                spillSums[lane] += spillSums[lane + half];
            }
            workgroupBarrier();
        }
        let taken = spillSums[0];
        let sums = water.spills[p];
        workgroupBarrier();
        // Nothing to take, or nowhere on the grid to land: no change.
        if (sums.x > 0.0 && sums.y > 0.0 && taken > 0.0) {
            for (var k = lane; k < count; k += SETTLE_GROUP) {
                let x = xs.x + i32(k % w);
                let z = zs.x + i32(k / w);
                let dx = f32(x) - c.x;
                let dz = f32(z) - c.y;
                var inner = false;
                let wt = spillWeight(sqrt(dx * dx + dz * dz) / reach, &inner);
                let at = base + u32(z) * n.x + u32(x);
                let d = fields[at];
                let lands = inner != sinks;
                let share = wt / select(sums.y, sums.x, inner);
                let lost = select(0.0, min(abs(push.w) * wt, d), inner == sinks);
                fields[at] = d + select(-lost, taken * share, lands);
            }
        }
        storageBarrier();
        workgroupBarrier();
    }
    // Then the coupled emitters' crossings in order, each a spill (its
    // weight sums the device's: its place is) and then a pour.
    var list = CROSSINGS_FIRST;
    // The volume poured, in the world's lengths, summed in order by the
    // lane that pours.
    var poured = 0.0;
    for (var k = 0u; k < water.faces.z; k++) {
        let count = uniformCount(lane, list);
        let reach = bitcast<f32>(crossings[list + 2u]) / h;
        let depth = bitcast<f32>(crossings[list + 3u]);
        let volume = bitcast<f32>(crossings[list + 4u]);
        for (var c = 0u; c < count; c++) {
            let on = crossingAt(list, c);
            spillCrossing(lane, on, reach, depth);
            // The spill's cells written before the pour reads four of them.
            storageBarrier();
            workgroupBarrier();
            if (lane == 0u) {
                pourAt(on, volume);
                poured += volume * water.sink.w;
            }
            storageBarrier();
            workgroupBarrier();
        }
        list = nextList(list);
    }
    if (lane == 0u) {
        _ = atomicExchange(&statistics[POURED], bitcast<u32>(poured));
    }
}

var<workgroup> listCount: u32;
var<workgroup> crossingSums: array<vec4<f32>, 256>;

// A list's count, uniform over the workgroup.
fn uniformCount(lane: u32, at: u32) -> u32 {
    if (lane == 0u) {
        listCount = crossings[at];
    }
    return workgroupUniformLoad(&listCount);
}

// SpillAt for a crossing at `on` (X and Z of the surface's frame), its
// reach in cells and depth: the inner and ring weights and what it takes
// summed over the workgroup, then the water moved when there is any to
// take and somewhere on the grid to land.
fn spillCrossing(lane: u32, on: vec2<f32>, reach: f32, depth: f32) {
    let n = water.size.xy;
    let h = water.sink.x;
    let base = root.now * water.size.z;
    let c = vec2<f32>(on.x / h + 0.5 * f32(n.x) - 0.5, on.y / h + 0.5 * f32(n.y) - 0.5);
    let sinks = depth > 0.0;
    let xs = span(c.x, 2.0 * reach, n.x);
    let zs = span(c.y, 2.0 * reach, n.y);
    let w = u32(max(xs.y - xs.x + 1, 0));
    let count = w * u32(max(zs.y - zs.x + 1, 0));
    var sum = vec4<f32>(0.0);
    for (var k = lane; k < count; k += SETTLE_GROUP) {
        let x = xs.x + i32(k % w);
        let z = zs.x + i32(k / w);
        let dx = f32(x) - c.x;
        let dz = f32(z) - c.y;
        var inner = false;
        let wt = spillWeight(sqrt(dx * dx + dz * dz) / reach, &inner);
        let d = fields[base + u32(z) * n.x + u32(x)];
        sum += vec4<f32>(select(0.0, wt, inner), select(wt, 0.0, inner),
                         select(0.0, min(abs(depth) * wt, d), inner == sinks), 0.0);
    }
    crossingSums[lane] = sum;
    workgroupBarrier();
    for (var half = SETTLE_GROUP / 2u; half > 0u; half >>= 1u) {
        if (lane < half) {
            crossingSums[lane] += crossingSums[lane + half];
        }
        workgroupBarrier();
    }
    let sums = crossingSums[0];
    workgroupBarrier();
    if (!(sums.x > 0.0 && sums.y > 0.0 && sums.z > 0.0)) {
        return;
    }
    for (var k = lane; k < count; k += SETTLE_GROUP) {
        let x = xs.x + i32(k % w);
        let z = zs.x + i32(k / w);
        let dx = f32(x) - c.x;
        let dz = f32(z) - c.y;
        var inner = false;
        let wt = spillWeight(sqrt(dx * dx + dz * dz) / reach, &inner);
        let at = base + u32(z) * n.x + u32(x);
        let d = fields[at];
        let lands = inner != sinks;
        let share = wt / select(sums.y, sums.x, inner);
        let lost = select(0.0, min(abs(depth) * wt, d), inner == sinks);
        fields[at] = d + select(-lost, sums.z * share, lands);
    }
}

// PourAt: a volume added to the depths of the four cells about `on` (X
// and Z of the surface's frame), bilinear; none off the grid.
fn pourAt(on: vec2<f32>, volume: f32) {
    let n = vec2<f32>(f32(water.size.x), f32(water.size.y));
    let h = water.sink.x;
    let p = vec2<f32>(on.x / h + 0.5 * n.x, on.y / h + 0.5 * n.y);
    if (!(p.x >= 0.0 && p.x <= n.x && p.y >= 0.0 && p.y <= n.y)) {
        return;
    }
    let cells = water.size.xy;
    let c = min(max(p - 0.5, vec2<f32>(0.0)), n - 1.0);
    let below = vec2<u32>(c);
    let lo = select(cells - 2u, below, below < cells - 1u);
    let t = c - vec2<f32>(lo);
    let base = root.now * water.size.z;
    for (var corner = 0u; corner < 4u; corner++) {
        let x = lo.x + (corner & 1u);
        let z = lo.y + (corner >> 1u);
        let share = select(1.0 - t.x, t.x, (corner & 1u) != 0u) *
                    select(1.0 - t.y, t.y, (corner >> 1u) != 0u);
        let at = base + z * water.size.x + x;
        fields[at] = fields[at] + share * volume / (h * h);
    }
}

// The surface at cell (x, z) of buffer now: its ground and depth.
fn surfaceAt(x: i32, z: i32) -> f32 {
    let i = u32(z) * water.size.x + u32(x);
    return fields[groundAt() + i] + fields[root.now * water.size.z + i];
}

// A face of an axis: its cells about it (an X face (x, z) between
// (x - 1, z) and (x, z), a Z face between (x, z - 1) and (x, z)) and
// whether it is a wall.
struct Face {
    a: vec2<i32>,
    b: vec2<i32>,
    lowWall: bool,
    highWall: bool,
}

fn faceAt(axis: u32, x: i32, z: i32) -> Face {
    let n = vec2<i32>(water.size.xy);
    var face: Face;
    face.a = select(vec2<i32>(x, z - 1), vec2<i32>(x - 1, z), axis == 0u);
    face.b = vec2<i32>(x, z);
    face.lowWall = select(z == 0, x == 0, axis == 0u);
    face.highWall = select(z == n.y, x == n.x, axis == 0u);
    return face;
}

fn groundOf(cell: vec2<i32>) -> f32 {
    return fields[groundAt() + u32(cell.y) * water.size.x + u32(cell.x)];
}

// A face's surfaces on its two sides and its sill: at a wall the outside
// is an empty cell of the edge's ground.
fn sides(face: Face) -> vec3<f32> {
    let ground = groundOf(select(face.a, face.b, face.lowWall));
    // The cell outside a wall is never read.
    var h0 = ground;
    var h1 = ground;
    if (!face.lowWall) {
        h0 = surfaceAt(face.a.x, face.a.y);
    }
    if (!face.highWall) {
        h1 = surfaceAt(face.b.x, face.b.y);
    }
    let other = groundOf(select(face.b, face.a, face.highWall));
    let sill = select(max(ground, other), ground, face.lowWall || face.highWall);
    return vec3<f32>(h0, h1, sill);
}

// Whether face (x, z) of an axis carries water: open, its water over its
// sill above DRY; its surfaces and sill into out.
fn isWet(axis: u32, x: i32, z: i32, out: ptr<function, vec3<f32>>) -> bool {
    let face = faceAt(axis, x, z);
    let closed = water.size.w;
    let low = select(WALL_MIN_Z, WALL_MIN_X, axis == 0u);
    let high = select(WALL_MAX_Z, WALL_MAX_X, axis == 0u);
    if ((face.lowWall && (closed & low) != 0u) || (face.highWall && (closed & high) != 0u)) {
        return false;
    }
    let s = sides(face);
    *out = s;
    return max(s.x, s.y) - s.z > DRY;
}

// Face (x, z)'s velocity in buffer b, own off the face grid.
fn faceValue(axis: u32, b: u32, x: i32, z: i32, own: f32) -> f32 {
    let w = i32(water.size.x) + select(0, 1, axis == 0u);
    let rows = i32(water.size.y) + select(1, 0, axis == 0u);
    if (x < 0 || x >= w || z < 0 || z >= rows) {
        return own;
    }
    return fields[facesAt(axis, b) + u32(z * w + x)];
}

fn old(axis: u32, x: i32, z: i32, own: f32) -> f32 {
    return faceValue(axis, root.now, x, z, own);
}

// The water a face carries before the substep: its velocity times the
// water over its sill on the giver's side; 0 off the grid or dry.
fn discharge(axis: u32, x: i32, z: i32) -> f32 {
    let v = old(axis, x, z, 0.0);
    var s = vec3<f32>(0.0);
    if (v == 0.0 || !isWet(axis, x, z, &s)) {
        return 0.0;
    }
    return v * max(select(s.y, s.x, v > 0.0) - s.z, 0.0);
}

fn depthOr0(x: i32, z: i32) -> f32 {
    let n = vec2<i32>(water.size.xy);
    if (x < 0 || x >= n.x || z < 0 || z >= n.y) {
        return 0.0;
    }
    return fields[root.now * water.size.z + u32(z * n.x + x)];
}

// Face (x, z) carried with the flow, as Carried (Stelling and
// Duinmeijer's upwind form): drawn toward each upstream neighbor by the
// discharge toward it over the face's depth, the pulls held to 1.
fn carried(axis: u32, x: i32, z: i32) -> f32 {
    let along = select(vec2<i32>(0, 1), vec2<i32>(1, 0), axis == 0u);
    let across = along.yx;
    let other = 1u - axis;
    let u = old(axis, x, z, 0.0);
    let lx = x - along.x;
    let lz = z - along.y;
    let back = 0.5 * (discharge(axis, lx, lz) + discharge(axis, x, z));
    let ahead = 0.5 * (discharge(axis, x, z) + discharge(axis, x + along.x, z + along.y));
    let below = 0.5 * (discharge(other, lx, lz) + discharge(other, x, z));
    let above = 0.5 * (discharge(other, lx + across.x, lz + across.y) +
                       discharge(other, x + across.x, z + across.y));
    let pull = vec4<f32>(max(back, 0.0), max(-ahead, 0.0), max(below, 0.0), max(-above, 0.0));
    let upstream = vec4<f32>(old(axis, x - along.x, z - along.y, u),
                         old(axis, x + along.x, z + along.y, u),
                         old(axis, x - across.x, z - across.y, u),
                         old(axis, x + across.x, z + across.y, u));
    let depth = 0.5 * (depthOr0(lx, lz) + depthOr0(x, z));
    let k = water.factors.w / (max(depth, DRY) * water.sink.x);
    let total = k * (pull.x + pull.y + pull.z + pull.w);
    let held = select(k, k / total, total > 1.0);
    var moved = 0.0;
    for (var j = 0u; j < 4u; j++) {
        moved += pull[j] * (upstream[j] - u);
    }
    return u + held * moved;
}

// Face (x, z)'s next velocity, as NextVelocity: carried, slowed,
// accelerated by the fall across it, held within 0.5 l / dt; 0 dry or
// closed, an open wall's only outward. Whether the bound held it.
fn nextVelocity(axis: u32, x: i32, z: i32, held: ptr<function, bool>) -> f32 {
    let face = faceAt(axis, x, z);
    var s = vec3<f32>(0.0);
    *held = false;
    if (!isWet(axis, x, z, &s)) {
        return 0.0;
    }
    let l = water.sink.x;
    let dt = water.factors.w;
    var v = carried(axis, x, z) * water.factors.y + dt * water.factors.x * (s.x - s.y) / l;
    let most = 0.5 * l / dt;
    *held = abs(v) > most;
    v = min(max(v, -most), most);
    return select(select(v, max(v, 0.0), face.highWall), min(v, 0.0), face.lowWall);
}

@compute @workgroup_size(64, 1, 1)
fn shallowFaces(@builtin(global_invocation_id) id: vec3<u32>) {
    let j = id.x;
    if (j >= water.faces.x + water.faces.y) {
        return;
    }
    let axis = select(1u, 0u, j < water.faces.x);
    let k = select(j - water.faces.x, j, axis == 0u);
    let w = water.size.x + select(0u, 1u, axis == 0u);
    var held = false;
    fields[facesAt(axis, 1u - root.now) + k] = nextVelocity(axis, i32(k % w), i32(k / w), &held);
    // The faces held, counted over the step (MVFX_GPU_WATER_FACES_HELD).
    if (held) {
        atomicAdd(&statistics[FACES_HELD], 1u);
    }
}

// The water a face carries a second, by the substep's new velocity, before
// its giver's scale; toward +X (+Z) above 0; its giver, -1 for none.
fn faceFlux(axis: u32, x: i32, z: i32, giver: ptr<function, i32>) -> f32 {
    let face = faceAt(axis, x, z);
    let v = faceValue(axis, 1u - root.now, x, z, 0.0);
    let s = sides(face);
    let fromLow = v > 0.0;
    let outside = select(face.highWall, face.lowWall, fromLow);
    let cell = select(face.b, face.a, fromLow);
    *giver = select(cell.y * i32(water.size.x) + cell.x, -1, outside || v == 0.0);
    let over = max(select(s.y, s.x, fromLow) - s.z, 0.0);
    return select(v * over * water.sink.x, 0.0, *giver < 0);
}

// Cell (x, z)'s four faces' fluxes, out of it above 0 (-X, +X, -Z, +Z).
fn cellFluxes(x: i32, z: i32, out: ptr<function, vec4<f32>>, givers: ptr<function, vec4<i32>>) {
    var g = -1;
    (*out).x = -faceFlux(0u, x, z, &g);
    (*givers).x = g;
    (*out).y = faceFlux(0u, x + 1, z, &g);
    (*givers).y = g;
    (*out).z = -faceFlux(1u, x, z, &g);
    (*givers).z = g;
    (*out).w = faceFlux(1u, x, z + 1, &g);
    (*givers).w = g;
}

@compute @workgroup_size(64, 1, 1)
fn shallowGiving(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    if (i >= water.size.z) {
        return;
    }
    let n0 = water.size.x;
    var out = vec4<f32>(0.0);
    var givers = vec4<i32>(-1);
    cellFluxes(i32(i % n0), i32(i / n0), &out, &givers);
    var sum = 0.0;
    for (var k = 0u; k < 4u; k++) {
        sum += select(0.0, out[k], givers[k] == i32(i));
    }
    let l = water.sink.x;
    let most = fields[root.now * water.size.z + i] * l * l / water.factors.w;
    fields[givingAt() + i] = select(1.0, most / sum, sum > most);
}

@compute @workgroup_size(64, 1, 1)
fn shallowDepths(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    if (i >= water.size.z) {
        return;
    }
    let n0 = water.size.x;
    var out = vec4<f32>(0.0);
    var givers = vec4<i32>(-1);
    cellFluxes(i32(i % n0), i32(i / n0), &out, &givers);
    var lost = 0.0;
    for (var k = 0u; k < 4u; k++) {
        lost += select(out[k] * fields[givingAt() + u32(max(givers[k], 0))], 0.0, givers[k] < 0);
    }
    let l = water.sink.x;
    let d = fields[root.now * water.size.z + i];
    fields[(1u - root.now) * water.size.z + i] = max(d - water.factors.w * lost / (l * l), 0.0);
}

var<workgroup> deepests: array<f32, 256>;

@compute @workgroup_size(256, 1, 1)
fn shallowSettle(@builtin(local_invocation_index) lane: u32) {
    let b = root.now;
    var deep = 0.0;
    var reach = 0.0;
    for (var i = lane; i < water.size.z; i += SETTLE_GROUP) {
        let d = fields[b * water.size.z + i];
        deep = max(deep, d);
        reach = max(reach, abs(fields[groundAt() + i] + d));
    }
    var fast = 0.0;
    for (var axis = 0u; axis < 2u; axis++) {
        let first = facesAt(axis, b);
        for (var j = lane; j < faceCount(axis); j += SETTLE_GROUP) {
            fast = max(fast, abs(fields[first + j]));
        }
    }
    deepests[lane] = deep;
    highs[lane] = reach;
    fasts[lane] = fast;
    workgroupBarrier();
    for (var half = SETTLE_GROUP / 2u; half > 0u; half >>= 1u) {
        if (lane < half) {
            deepests[lane] = max(deepests[lane], deepests[lane + half]);
            highs[lane] = max(highs[lane], highs[lane + half]);
            fasts[lane] = max(fasts[lane], fasts[lane + half]);
        }
        workgroupBarrier();
    }
    if (lane == 0u) {
        _ = atomicExchange(&statistics[HOLDS], select(0u, 1u, fasts[0] > STILL));
        _ = atomicExchange(&statistics[SPEED],
                           bitcast<u32>(fasts[0] + sqrt(water.factors.x * deepests[0])));
        _ = atomicExchange(&statistics[REACH], bitcast<u32>(highs[0]));
    }
}
