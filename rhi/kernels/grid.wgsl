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

// include: shared/pcg4d_hash.wgsl

// include: shared/exact.wgsl

const NONE: u32 = 0xFFFFFFFFu;
const CHUNK: u32 = 256u;
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;

// The salt that sets grid keys apart (the core's MVFX_GRID_SALT), and the
// greatest cell coordinate in size (MVFX_MAX_CELL).
const GRID_SALT: u32 = 0x4E474244u;
const MAX_CELL: f32 = 1073741824.0;

// What an emitter does with a grid (rhi/src/numbers.h).
const GRID_BUILDS: u32 = 1u;
const GRID_FINDS: u32 = 2u;
const GRID_OWN: u32 = 4u;

// The statistics record's word counting the caps since the instance's
// last clear (MVFX_GPU_NEIGHBORS_CAPPED).
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

var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> held: u32;

// The words of one of the sort's arrays over a capacity.
fn spanOf(capacity: u32) -> u32 {
    return (capacity + BLOCK - 1u) / BLOCK * BLOCK;
}

fn floatAt(first: u32, i: u32) -> f32 {
    return bitcast<f32>(particles[first * params.head.x + i]);
}

// A binary32 value's halves by Veltkamp's split, 12 bits each, so that
// products of halves are exact.
fn halvesOf(a: f32) -> vec2<f32> {
    let c = 4097.0 * a;
    let high = c - (c - a);
    return vec2<f32>(high, a - high);
}

// The exact remainder of x times y less t, their product rounded, by
// Dekker's TwoProduct: each product of halves is exact, and so is the sum.
// WGSL's fma may round twice (lavapipe's does), so it cannot give this;
// a compiler fusing these terms leaves them exact.
fn productBelow(x: f32, y: f32, t: f32) -> f32 {
    let a = halvesOf(x);
    let b = halvesOf(y);
    return ((a.x * b.x - t) + a.x * b.y + a.y * b.x) + a.y * b.y;
}

// The binary64 floor of x times inverse (exact: a product of two binary32
// values), clamped to MAX_CELL as the core's mvfxGridCell: the rounded
// product, one less where it is an integer the exact product lies below.
// A product rounded to 0 is below it for a negative x, told by its bits,
// which a device flushing subnormals keeps.
fn cellOf(x: f32, inverse: f32) -> i32 {
    let t = x * inverse;
    var c = floor(t);
    if (c == t) {
        let negative = select(productBelow(x, inverse, t) < 0.0, bitcast<u32>(x) > 0x80000000u,
                              t == 0.0);
        c -= select(0.0, 1.0, negative);
    }
    c = select(-MAX_CELL, c, c >= -MAX_CELL);
    return i32(min(c, MAX_CELL));
}

fn keyOf(cell: vec3<i32>, mask: u32) -> u32 {
    let c = bitcast<vec3<u32>>(cell);
    return pcg4d(vec4<u32>(c, GRID_SALT)).x & mask;
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
                    let p = params.words.x;
                    key = keyOf(vec3<i32>(cellOf(floatAt(p, i), inverse),
                                          cellOf(floatAt(p + 1u, i), inverse),
                                          cellOf(floatAt(p + 2u, i), inverse)),
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

// Grid values: the places and velocities of the grid's particles in its
// order.
@compute @workgroup_size(64, 1, 1)
fn gridValues(@builtin(global_invocation_id) id: vec3<u32>) {
    let c = params.head.x;
    let span = spanOf(c);
    let n = gridCount[0];
    for (var j = id.x; j < n; j += params.head.w * 64u) {
        let i = gridWords[span + j];
        for (var k = 0u; k < 3u; k++) {
            gridWords[4u * span + k * c + j] = particles[(params.words.x + k) * c + i];
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
// at the cap.
struct Sums {
    count: u32,
    place: vec3<f32>,
    velocity: vec3<f32>,
    capped: bool,
}

// Adds the neighbors among a key's entries; false once one more than the
// most is found.
fn visitKey(key: u32, skip: u32, place: vec3<f32>, sums: ptr<function, Sums>) -> bool {
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
        for (var k = 0u; k < 3u; k++) {
            (*sums).place[k] += at[k];
            (*sums).velocity[k] += bitcast<f32>(sourceWord(4u * span + (3u + k) * c + j));
        }
    }
    return true;
}

// Neighbors: each particle that outlives the step given its neighbors'
// count and the offsets to their mean place and velocity, from the 27
// cells about it (x fastest, then y, then z, a key two cells share
// visited once); its caps counted.
@compute @workgroup_size(64, 1, 1)
fn neighbors(@builtin(global_invocation_id) id: vec3<u32>) {
    let c = params.head.x;
    let live = atomicLoad(&statistics[0]);
    var capped = 0u;
    for (var i = id.x; i < live; i += params.head.w * 64u) {
        if (!outlives(i)) {
            continue;
        }
        let p = params.words.x;
        let v = params.words.y;
        let place = vec3<f32>(floatAt(p, i), floatAt(p + 1u, i), floatAt(p + 2u, i));
        let velocity = vec3<f32>(floatAt(v, i), floatAt(v + 1u, i), floatAt(v + 2u, i));
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
            if (!visitKey(key, skip, place, &sums)) {
                break;
            }
        }
        capped += select(0u, 1u, sums.capped);
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
    if (capped > 0u) {
        atomicAdd(&statistics[STATISTICS_CAPPED], capped);
    }
}
