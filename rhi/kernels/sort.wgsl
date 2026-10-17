// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's sort by place: a stable least-significant-digit radix sort
// of the keys records.wgsl's keys pass writes, 8 bits a pass, four passes,
// as the core's mvfxRadixSort. A pass is two kernels over blocks of BLOCK
// elements: offsets scans the blocks' digit counts into each block's first
// place for each digit; scatter ranks each block's elements by digit,
// keeping their order, writes each at its digit's place for the block plus
// its rank, and counts the next digit by the block each lands in. The
// keys and indices go from one set to the other and back, so after four
// passes they are in the first.

const NONE: u32 = 0xFFFFFFFFu;
const CHUNK: u32 = 256u;
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;

// The pass's digit (its lowest bit), the particle buffer's capacity, the
// workgroups dispatched, unused.
struct Root {
    shift: u32,
    capacity: u32,
    groups: u32,
    unused: u32,
}

var<immediate> root: Root;

@group(0) @binding(0) var<storage, read> statistics: array<u32>;
// Keys, then indices, twice over the capacity's blocks.
@group(0) @binding(1) var<storage, read_write> sortWords: array<u32>;
// Two sets of digit counts, 256 a block each, digit-major: digit d of
// block b at d n + b, of n blocks.
@group(0) @binding(2) var<storage, read_write> digits: array<atomic<u32>>;

var<workgroup> scan: array<u32, CHUNK>;
var<workgroup> carried: u32;
var<workgroup> liveCount: u32;
var<workgroup> zeros: u32;
var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> starts: array<u32, CHUNK>;
var<workgroup> localKeys: array<u32, BLOCK>;
var<workgroup> localIndices: array<u32, BLOCK>;

// Adds keep's values of the workgroup in order: each invocation's sum of
// those before it, as sim.wgsl's.
fn exclusiveScan(local: u32, keep: u32) -> u32 {
    scan[local] = keep;
    workgroupBarrier();
    for (var offset = 1u; offset < CHUNK; offset <<= 1u) {
        var add = 0u;
        if (local >= offset) {
            add = scan[local - offset];
        }
        workgroupBarrier();
        scan[local] += add;
        workgroupBarrier();
    }
    return scan[local] - keep;
}

fn blocksOf(count: u32) -> u32 {
    return (count + BLOCK - 1u) / BLOCK;
}

fn digitOf(key: u32) -> u32 {
    return (key >> root.shift) & 0xFFu;
}

// Atomics are set by exchanges, not stores: naga's HLSL of an atomic store
// names a result it never declares.

// The live count, the same in every invocation.
fn liveOf(local: u32) -> u32 {
    if (local == 0u) {
        liveCount = statistics[0];
    }
    return workgroupUniformLoad(&liveCount);
}

// Offsets, in one workgroup: the pass's counts, in order, made each
// digit's first place for each block (the blocks past the live count's
// none); the next pass's counts cleared for the scatter to add to.
@compute @workgroup_size(256, 1, 1)
fn offsets(@builtin(local_invocation_index) local: u32) {
    if (local == 0u) {
        carried = 0u;
    }
    let blocks = blocksOf(liveOf(local));
    let n = blocksOf(root.capacity);
    let total = 256u * n;
    let current = (root.shift / 8u) % 2u * total;
    let next = total - current;
    for (var base = 0u; base < total; base += CHUNK) {
        let k = base + local;
        var count = 0u;
        if (k < total && k % n < blocks) {
            count = atomicLoad(&digits[current + k]);
        }
        let before = exclusiveScan(local, count);
        if (k < total) {
            _ = atomicExchange(&digits[current + k], carried + before);
            if (root.shift < 24u) {
                _ = atomicExchange(&digits[next + k], 0u);
            }
        }
        workgroupBarrier();
        if (local == CHUNK - 1u) {
            carried += before + count;
        }
        workgroupBarrier();
    }
}

// Scatter, a block a workgroup: the block's elements (ITEMS in a row an
// invocation; those past the live count last, as the largest key) sorted
// by the digit within the block through a stable split on each of its 8
// bits; each live one written at its digit's place for the block plus its
// rank among the block's of that digit.
@compute @workgroup_size(256, 1, 1)
fn scatter(@builtin(local_invocation_index) local: u32,
           @builtin(workgroup_id) group: vec3<u32>) {
    let live = liveOf(local);
    let blocks = blocksOf(live);
    let n = blocksOf(root.capacity);
    let span = n * BLOCK;
    let parity = (root.shift / 8u) % 2u;
    let source = parity * 2u * span;
    let into = (1u - parity) * 2u * span;
    let current = parity * 256u * n;
    let next = 256u * n - current;
    for (var b = group.x; b < blocks; b += root.groups) {
        var key: array<u32, ITEMS>;
        var index: array<u32, ITEMS>;
        for (var k = 0u; k < ITEMS; k++) {
            let e = b * BLOCK + local * ITEMS + k;
            key[k] = NONE;
            index[k] = NONE;
            if (e < live) {
                key[k] = sortWords[source + e];
                index[k] = sortWords[source + span + e];
            }
        }
        _ = atomicExchange(&tally[local], 0u);
        workgroupBarrier();
        for (var k = 0u; k < ITEMS; k++) {
            if (index[k] != NONE) {
                atomicAdd(&tally[digitOf(key[k])], 1u);
            }
        }
        workgroupBarrier();
        starts[local] = exclusiveScan(local, atomicLoad(&tally[local]));
        for (var bit = 0u; bit < 8u; bit++) {
            var mine = 0u;
            for (var k = 0u; k < ITEMS; k++) {
                mine += 1u - ((digitOf(key[k]) >> bit) & 1u);
            }
            let before = exclusiveScan(local, mine);
            if (local == CHUNK - 1u) {
                zeros = before + mine;
            }
            workgroupBarrier();
            let total = zeros;
            var z = before;
            for (var k = 0u; k < ITEMS; k++) {
                let at = local * ITEMS + k;
                var place = z;
                if (((digitOf(key[k]) >> bit) & 1u) == 1u) {
                    place = total + at - z;
                } else {
                    z += 1u;
                }
                localKeys[place] = key[k];
                localIndices[place] = index[k];
            }
            workgroupBarrier();
            for (var k = 0u; k < ITEMS; k++) {
                key[k] = localKeys[local * ITEMS + k];
                index[k] = localIndices[local * ITEMS + k];
            }
            workgroupBarrier();
        }
        for (var k = 0u; k < ITEMS; k++) {
            if (index[k] != NONE) {
                let d = digitOf(key[k]);
                let rank = local * ITEMS + k - starts[d];
                let place = atomicLoad(&digits[current + d * n + b]) + rank;
                sortWords[into + place] = key[k];
                sortWords[into + span + place] = index[k];
                if (root.shift < 24u) {
                    let following = (key[k] >> (root.shift + 8u)) & 0xFFu;
                    atomicAdd(&digits[next + following * n + place / BLOCK], 1u);
                }
            }
        }
        workgroupBarrier();
    }
}
