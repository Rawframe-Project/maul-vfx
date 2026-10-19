// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's plexus links, as the core's (src/plexus.c), on the
// compacted pool at the step's end, after its bounds:
//  - plexusKeys gives each particle its cell's key from its place, cells
//    of the link distance, and the blocks' counts of the lowest digit;
//    sort.wgsl's four radix passes order them, stable;
//  - plexusCount gives each particle the later particles within the
//    distance in the 27 cells about it, found and owned (up to its most);
//  - plexusOffsets scans the owned counts in pool order into each
//    particle's first slot, in one workgroup, and counts the step's links
//    and the drops (found less written);
//  - plexusWrite writes each particle's owned links in the grid's order
//    from its first slot, those past the emitter's most left out.
// The distance test is a sum of squared differences against the square
// of the distance, both rounded as the core's, so from the same places
// the links are the core's.

// include: shared/grid_cells.wgsl

const CHUNK: u32 = 256u;
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;

// The statistics record's words: the step's links written, and the links
// dropped since the instance was last cleared (MVFX_GPU_LINKS,
// MVFX_GPU_LINKS_DROPPED).
const STATISTICS_LINKS: u32 = 48u;
const STATISTICS_LINKS_DROPPED: u32 = 49u;

struct Plexus {
    // The particle buffer's capacity, the most links a particle owns, the
    // most the emitter keeps, the workgroups of the block passes.
    head: vec4<u32>,
    // The grid's mask, the first word of the places, the workgroups of
    // the particle passes, unused.
    grid: vec4<u32>,
    // The cells' inverse size (one over the distance, as the core rounds
    // it), the distance squared, unused.
    reach: vec4<f32>,
}

@group(0) @binding(0) var<storage, read> particles: array<u32>;
@group(0) @binding(1) var<storage, read_write> statistics: array<atomic<u32>>;
// Keys, then indices, twice over the capacity's blocks; two sets of digit
// counts, 256 a block each, digit-major (the sort's).
@group(0) @binding(2) var<storage, read_write> sortWords: array<u32>;
@group(0) @binding(3) var<storage, read_write> digits: array<u32>;
// Each particle's links found, owned and first slot: three arrays of the
// capacity.
@group(0) @binding(4) var<storage, read_write> counts: array<u32>;
// The links, two particle indices each, the owner first.
@group(0) @binding(5) var<storage, read_write> links: array<u32>;
@group(0) @binding(6) var<uniform> plexus: Plexus;

var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> held: u32;
var<workgroup> sums: array<vec2<u32>, CHUNK>;

fn placeOf(i: u32) -> vec3<f32> {
    let c = plexus.head.x;
    let first = plexus.grid.y;
    return vec3<f32>(bitcast<f32>(particles[first * c + i]),
                     bitcast<f32>(particles[(first + 1u) * c + i]),
                     bitcast<f32>(particles[(first + 2u) * c + i]));
}

// The words of one of the sort's arrays over a capacity.
fn spanOf(capacity: u32) -> u32 {
    return (capacity + BLOCK - 1u) / BLOCK * BLOCK;
}

// Plexus keys: each particle's key and index in the first set, a block a
// workgroup at a time, and each block's counts of the lowest digit.
@compute @workgroup_size(256, 1, 1)
fn plexusKeys(@builtin(local_invocation_index) local: u32,
              @builtin(workgroup_id) group: vec3<u32>) {
    if (local == 0u) {
        held = atomicLoad(&statistics[0]);
    }
    let n = workgroupUniformLoad(&held);
    let inverse = plexus.reach.x;
    let blocks = spanOf(plexus.head.x) / BLOCK;
    let span = blocks * BLOCK;
    for (var b = group.x; b < (n + BLOCK - 1u) / BLOCK; b += plexus.head.w) {
        _ = atomicExchange(&tally[local], 0u);
        workgroupBarrier();
        for (var k = 0u; k < ITEMS; k++) {
            let i = b * BLOCK + k * CHUNK + local;
            if (i < n) {
                let at = placeOf(i);
                let key = keyOf(vec3<i32>(cellOf(at.x, inverse), cellOf(at.y, inverse),
                                          cellOf(at.z, inverse)),
                                plexus.grid.x);
                sortWords[i] = key;
                sortWords[span + i] = i;
                atomicAdd(&tally[key & 0xFFu], 1u);
            }
        }
        workgroupBarrier();
        digits[local * blocks + b] = atomicLoad(&tally[local]);
        workgroupBarrier();
    }
}

// The first of the sorted keys at or past key (past when after).
fn firstKey(key: u32, n: u32, after: bool) -> u32 {
    var a = 0u;
    var b = n;
    while (a < b) {
        let m = (a + b) / 2u;
        let k = sortWords[m];
        if (k < key || (after && k == key)) {
            a = m + 1u;
        } else {
            b = m;
        }
    }
    return a;
}

// Particle i's links, the later particles within the distance in the 27
// cells about it in the grid's order (a key two cells share visited
// once): found and, with write, the first room of them written from slot
// first. Returns the count found.
fn walk(i: u32, n: u32, write: bool, first: u32, room: u32) -> u32 {
    let span = spanOf(plexus.head.x);
    let inverse = plexus.reach.x;
    let most = plexus.reach.y;
    let at = placeOf(i);
    let cell = vec3<i32>(cellOf(at.x, inverse), cellOf(at.y, inverse), cellOf(at.z, inverse));
    var visited: array<u32, 27>;
    var visits = 0u;
    var found = 0u;
    for (var m = 0u; m < 27u; m++) {
        let around = cell + vec3<i32>(i32(m % 3u) - 1, i32(m / 3u % 3u) - 1, i32(m / 9u) - 1);
        let key = keyOf(around, plexus.grid.x);
        var seen = false;
        for (var u = 0u; u < visits; u++) {
            seen = seen || visited[u] == key;
        }
        if (seen) {
            continue;
        }
        visited[visits] = key;
        visits++;
        let last = firstKey(key, n, true);
        for (var e = firstKey(key, n, false); e < last; e++) {
            let j = sortWords[span + e];
            let other = placeOf(j);
            var square = 0.0;
            for (var k = 0u; k < 3u; k++) {
                let d = other[k] - at[k];
                square += d * d;
            }
            if (j <= i || !(square < most)) {
                continue;
            }
            if (write && found < room) {
                links[2u * (first + found)] = i;
                links[2u * (first + found) + 1u] = j;
            }
            found++;
            if (write && found == room) {
                return found;
            }
        }
    }
    return found;
}

// Count: each particle's links found and owned.
@compute @workgroup_size(64, 1, 1)
fn plexusCount(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = atomicLoad(&statistics[0]);
    let c = plexus.head.x;
    for (var i = id.x; i < n; i += plexus.grid.z * 64u) {
        let found = walk(i, n, false, 0u, 0u);
        counts[i] = found;
        counts[c + i] = min(found, plexus.head.y);
    }
}

// Offsets, in one workgroup: each invocation's run of particles summed,
// the runs' sums scanned, then each particle's first slot; the step's
// links (the owned, at most the emitter's most) and the drops.
@compute @workgroup_size(256, 1, 1)
fn plexusOffsets(@builtin(local_invocation_index) local: u32) {
    let n = atomicLoad(&statistics[0]);
    let c = plexus.head.x;
    let run = (n + CHUNK - 1u) / CHUNK;
    let start = min(local * run, n);
    let end = min(start + run, n);
    var sum = vec2<u32>(0u);
    for (var i = start; i < end; i++) {
        sum += vec2<u32>(counts[i], counts[c + i]);
    }
    sums[local] = sum;
    workgroupBarrier();
    // An inclusive scan of the runs' sums, by doubling.
    for (var d = 1u; d < CHUNK; d *= 2u) {
        var add = vec2<u32>(0u);
        if (local >= d) {
            add = sums[local - d];
        }
        workgroupBarrier();
        sums[local] += add;
        workgroupBarrier();
    }
    var slot = sums[local].y - sum.y;
    for (var i = start; i < end; i++) {
        counts[2u * c + i] = slot;
        slot += counts[c + i];
    }
    if (local == CHUNK - 1u) {
        let total = sums[local];
        let written = min(total.y, plexus.head.z);
        _ = atomicExchange(&statistics[STATISTICS_LINKS], written);
        atomicAdd(&statistics[STATISTICS_LINKS_DROPPED], total.x - written);
    }
}

// Write: each particle's owned links that fit the emitter's most.
@compute @workgroup_size(64, 1, 1)
fn plexusWrite(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = atomicLoad(&statistics[0]);
    let c = plexus.head.x;
    for (var i = id.x; i < n; i += plexus.grid.z * 64u) {
        let first = counts[2u * c + i];
        let owned = counts[c + i];
        if (owned > 0u && first < plexus.head.z) {
            _ = walk(i, n, true, first, min(owned, plexus.head.z - first));
        }
    }
}
