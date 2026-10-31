// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's neighbor grid, as the core's (src/neighbor.c), before its
// step's update moves its particles:
//  - gridKeys gives each particle alive at the step's start that outlives
//    the step its cell's key (pcg4d of the binary64 floor of each
//    coordinate times the inverse cell, masked; the ended an all-ones key
//    that sorts last and matches none), its index, and the blocks' counts
//    of the lowest digit;
//  - sort.wgsl's four radix passes order them by key, stable, so a key's
//    particles keep the pool's order as the core's counting sort keeps
//    them;
//  - gridValues copies their places and velocities in that order;
//  - neighbors walks the 27 cells about each particle in the core's order,
//    a key's entries a range of the sorted keys found by binary search,
//    and gives it its neighbors' count and the offsets to their mean place
//    and velocity, the core's operations in its order, its cap counted.
// All of it at every step, as the core's step finds neighbors whether or
// not its births all fit.

// include: shared/grid_cells.wgsl

// include: shared/exact.wgsl

// include: shared/curve.wgsl

const NONE: u32 = 0xFFFFFFFFu;
const CHUNK: u32 = 256u;
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;

// What an emitter does with a grid (rhi/src/numbers.h).
const GRID_BUILDS: u32 = 1u;
const GRID_FINDS: u32 = 2u;
const GRID_OWN: u32 = 4u;
const GRID_STEERS: u32 = 8u;

// The statistics record's word counting the caps since the instance's
// last clear (MVFX_GPU_NEIGHBORS_DROPPED).
const STATISTICS_CAPPED: u32 = 46u;

struct Grid {
    // The particle buffer's capacity, the source's (0 for one of no room),
    // the workgroups of the block passes and of the others (from the
    // numbers: num_workgroups takes a constant buffer of its own on D3D12).
    head: vec4<u32>,
    // The keys' mask, what the emitter does, the most a query counts, the
    // source's mask.
    grid: vec4<u32>,
    // The inverse of its cells, of the source's, the radius squared,
    // unused.
    cells: vec4<f32>,
    // Where a particle's place, velocity, death (two words) and neighbor
    // count are; its neighbor center and velocity, unused.
    words: vec4<u32>,
    found: vec4<u32>,
    // The clock at the step's end (two words), unused.
    clock: vec4<u32>,
    // The anchor's shift over the step, unused: the core's step moves the
    // places of a world-space emitter by it before anything reads them.
    shift: vec4<f32>,
    // Steering and the pair force (grid/steer.wgsl's): what it does, the
    // path's points and segments, the strength's table; the cruise speed,
    // the most force, the plan's seconds, the view's cosine; the rules'
    // weights and the strength's value; their radii squared and the pair
    // force's; the seek and flee points with their weights; their radii,
    // the path's weight and radius; the lookahead, the perimeter, the pair
    // force's inverse radius, whether the path is closed; the path's
    // points.
    steer: vec4<u32>,
    steerScale: vec4<f32>,
    rules: vec4<f32>,
    ruleSquares: vec4<f32>,
    seek: vec4<f32>,
    flee: vec4<f32>,
    targets: vec4<f32>,
    path: vec4<f32>,
    points: array<vec4<f32>, 32>,
}

@group(0) @binding(0) var<uniform> params: Grid;
// The particles at the step's start (the buffer update reads).
@group(0) @binding(1) var<storage, read_write> particles: array<u32>;
@group(0) @binding(2) var<storage, read_write> statistics: array<atomic<u32>>;
// Keys, then indices, twice over the capacity's blocks (the sort's), then
// the places and velocities in the grid's order, six arrays of the
// capacity.
@group(0) @binding(3) var<storage, read_write> gridWords: array<u32>;
// Two sets of digit counts, 256 a block each, digit-major (the sort's).
@group(0) @binding(4) var<storage, read_write> digits: array<u32>;
// The particles the grid holds (the sort's statistics word).
@group(0) @binding(5) var<storage, read_write> gridCount: array<u32>;
// Another emitter's grid and count, for one whose source is another (a
// word of the simulation's otherwise).
@group(0) @binding(6) var<storage, read> sourceWords: array<u32>;
@group(0) @binding(7) var<storage, read> sourceCount: array<u32>;
// The step's baked tables (the pair force's strength).
@group(0) @binding(8) var<storage, read> grids: array<u32>;

var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> held: u32;

// The words of one of the sort's arrays over a capacity.
fn spanOf(capacity: u32) -> u32 {
    return (capacity + BLOCK - 1u) / BLOCK * BLOCK;
}

fn floatAt(first: u32, i: u32) -> f32 {
    return bitcast<f32>(particles[first * params.head.x + i]);
}

// Particle i's place moved by the anchor's shift, as the core's step
// moves it first (each coordinate one binary32 sum).
fn placeOf(i: u32) -> vec3<f32> {
    let p = params.words.x;
    return vec3<f32>(floatAt(p, i), floatAt(p + 1u, i), floatAt(p + 2u, i)) + params.shift.xyz;
}

// Whether particle i outlives the step: its death past the step's end.
fn outlives(i: u32) -> bool {
    let c = params.head.x;
    let low = particles[params.words.z * c + i];
    let high = particles[(params.words.z + 1u) * c + i];
    return high > params.clock.y || (high == params.clock.y && low > params.clock.x);
}

// Grid keys: each particle's key and index in the first set, a block a
// workgroup at a time, and each block's counts of the lowest digit.
@compute @workgroup_size(256, 1, 1)
fn gridKeys(@builtin(local_invocation_index) local: u32,
            @builtin(workgroup_id) group: vec3<u32>) {
    if (local == 0u) {
        held = atomicLoad(&statistics[0]);
    }
    let n = workgroupUniformLoad(&held);
    let blocks = spanOf(params.head.x) / BLOCK;
    let span = blocks * BLOCK;
    let inverse = params.cells.x;
    for (var b = group.x; b < (n + BLOCK - 1u) / BLOCK; b += params.head.z) {
        _ = atomicExchange(&tally[local], 0u);
        workgroupBarrier();
        for (var k = 0u; k < ITEMS; k++) {
            let i = b * BLOCK + k * CHUNK + local;
            if (i < n) {
                var key = NONE;
                if (outlives(i)) {
                    let at = placeOf(i);
                    key = keyOf(vec3<i32>(cellOf(at.x, inverse), cellOf(at.y, inverse),
                                          cellOf(at.z, inverse)),
                                params.grid.x);
                }
                gridWords[i] = key;
                gridWords[span + i] = i;
                atomicAdd(&tally[key & 0xFFu], 1u);
            }
        }
        workgroupBarrier();
        digits[local * blocks + b] = atomicLoad(&tally[local]);
        workgroupBarrier();
    }
    if (group.x == 0u && local == 0u) {
        gridCount[0] = n;
    }
}

// Grid values: the places (shifted) and velocities of the grid's
// particles in its order.
@compute @workgroup_size(64, 1, 1)
fn gridValues(@builtin(global_invocation_id) id: vec3<u32>) {
    let c = params.head.x;
    let span = spanOf(c);
    let n = gridCount[0];
    for (var j = id.x; j < n; j += params.head.w * 64u) {
        let i = gridWords[span + j];
        let at = placeOf(i);
        for (var k = 0u; k < 3u; k++) {
            gridWords[4u * span + k * c + j] = bitcast<u32>(at[k]);
            gridWords[4u * span + (3u + k) * c + j] = particles[(params.words.y + k) * c + i];
        }
    }
}

// The grid a query reads: the emitter's own, or its source's.
fn own() -> bool {
    return (params.grid.y & GRID_OWN) != 0u;
}

fn sourceWord(w: u32) -> u32 {
    if (own()) {
        return gridWords[w];
    }
    return sourceWords[w];
}

// The particles the source's grid holds: none for a source of no room,
// whose grid is a word of the simulation's.
fn sourceHeld() -> u32 {
    if (params.head.y == 0u) {
        return 0u;
    }
    if (own()) {
        return gridCount[0];
    }
    return sourceCount[0];
}

// The first of the source's sorted keys at or past key (past when after).
fn firstKey(key: u32, n: u32, after: bool) -> u32 {
    var a = 0u;
    var b = n;
    while (a < b) {
        let m = (a + b) / 2u;
        let k = sourceWord(m);
        if (k < key || (after && k == key)) {
            a = m + 1u;
        } else {
            b = m;
        }
    }
    return a;
}

// What a query found: its neighbors' count and sums, whether it stopped
// at the cap; for one that steers, the flocking rules' counts and sums and
// the pair force's pulls (the core's mvfxNeighborSums).
struct Sums {
    count: u32,
    place: vec3<f32>,
    velocity: vec3<f32>,
    capped: bool,
    aligned: u32,
    cohered: u32,
    apart: vec3<f32>,
    alignedVelocity: vec3<f32>,
    coheredPlace: vec3<f32>,
    pull: vec3<f32>,
}

// Adds the neighbors among a key's entries; false once one more than the
// most is found. A particle that steers adds each counted neighbor to the
// flocking rules (AddFlock) and the pair force (AddPull) too.
fn visitKey(key: u32, skip: u32, place: vec3<f32>, heading: vec3<f32>,
            sums: ptr<function, Sums>) -> bool {
    let c = params.head.y;
    let span = spanOf(c);
    let n = sourceHeld();
    let last = firstKey(key, n, true);
    for (var j = firstKey(key, n, false); j < last; j++) {
        if (sourceWord(span + j) == skip) {
            continue;
        }
        var at = vec3<f32>(0.0);
        var d = vec3<f32>(0.0);
        var square = 0.0;
        for (var k = 0u; k < 3u; k++) {
            at[k] = bitcast<f32>(sourceWord(4u * span + k * c + j));
            d[k] = at[k] - place[k];
            square += d[k] * d[k];
        }
        if (!(square < params.cells.z)) {
            continue;
        }
        if ((*sums).count == params.grid.z) {
            (*sums).capped = true;
            return false;
        }
        (*sums).count += 1u;
        var moving = vec3<f32>(0.0);
        for (var k = 0u; k < 3u; k++) {
            moving[k] = bitcast<f32>(sourceWord(4u * span + (3u + k) * c + j));
            (*sums).place[k] += at[k];
            (*sums).velocity[k] += moving[k];
        }
        if ((params.steer.x & STEER_FLOCK) != 0u) {
            addFlock(at, moving, d, square, heading, sums);
        }
        if (square < params.ruleSquares.w && square > 0.0) {
            addPull(d, square, sums);
        }
    }
    return true;
}

// The neighbors of particle i from the 27 cells about its place (x
// fastest, then y, then z, a key two cells share visited once), its
// heading its velocity, as the core's mvfxSearchNeighbors.
fn walk(i: u32, place: vec3<f32>, heading: vec3<f32>) -> Sums {
    let inverse = params.cells.y;
    let cell = vec3<i32>(cellOf(place.x, inverse), cellOf(place.y, inverse),
                         cellOf(place.z, inverse));
    var visited: array<u32, 27>;
    var visits = 0u;
    var sums = Sums();
    let skip = select(NONE, i, own());
    for (var m = 0u; m < 27u; m++) {
        let around = cell + vec3<i32>(i32(m % 3u) - 1, i32(m / 3u % 3u) - 1, i32(m / 9u) - 1);
        let key = keyOf(around, params.grid.w);
        var seen = false;
        for (var u = 0u; u < visits; u++) {
            seen = seen || visited[u] == key;
        }
        if (seen) {
            continue;
        }
        visited[visits] = key;
        visits++;
        if (!visitKey(key, skip, place, heading, &sums)) {
            break;
        }
    }
    return sums;
}

// Stores particle i's neighbor count and the offsets to their mean place
// and velocity, as the core's mvfxStoreNeighbors.
fn storeNeighbors(i: u32, place: vec3<f32>, velocity: vec3<f32>, sums: Sums) {
    let c = params.head.x;
    let any = sums.count > 0u;
    var inverseCount = 0.0;
    // Correctly rounded, as the core's: WGSL's division is within 2.5
    // units in the last place.
    if (any) {
        inverseCount = divide(1.0, f32(sums.count));
    }
    if (params.words.w != NONE) {
        particles[params.words.w * c + i] = bitcast<u32>(f32(sums.count));
    }
    for (var k = 0u; k < 3u; k++) {
        let center = select(0.0, sums.place[k] * inverseCount - place[k], any);
        let mean = select(0.0, sums.velocity[k] * inverseCount - velocity[k], any);
        if (params.found.x != NONE) {
            particles[(params.found.x + k) * c + i] = bitcast<u32>(center);
        }
        if (params.found.y != NONE) {
            particles[(params.found.y + k) * c + i] = bitcast<u32>(mean);
        }
    }
}

// Neighbors: each particle that outlives the step given its neighbors'
// count and offsets, for an emitter that finds them, then its steering
// and pull added to its velocity, for one that steers (before update
// moves it, as the core's mvfxSteerEmitter); its caps counted. Each
// particle writes only its own words and reads the others' from the grid.
@compute @workgroup_size(64, 1, 1)
fn neighbors(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = atomicLoad(&statistics[0]);
    let finds = (params.grid.y & GRID_FINDS) != 0u;
    let steers = (params.grid.y & GRID_STEERS) != 0u;
    var capped = 0u;
    for (var i = id.x; i < live; i += params.head.w * 64u) {
        if (!outlives(i)) {
            continue;
        }
        let v = params.words.y;
        let place = placeOf(i);
        let velocity = vec3<f32>(floatAt(v, i), floatAt(v + 1u, i), floatAt(v + 2u, i));
        var sums = Sums();
        if (finds) {
            sums = walk(i, place, velocity);
            capped += select(0u, 1u, sums.capped);
            storeNeighbors(i, place, velocity, sums);
        }
        if (steers) {
            steerParticle(i, place, velocity, sums);
        }
    }
    if (capped > 0u) {
        atomicAdd(&statistics[STATISTICS_CAPPED], capped);
    }
}

// include: grid/steer.wgsl
