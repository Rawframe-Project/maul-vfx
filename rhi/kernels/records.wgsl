// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's records: each live particle's values in a layout of the
// host's, written into a buffer of the host's as the CPU path's
// mvfxInstance_WriteParticles writes them, one invocation a record, in the
// pool's order, its reverse, or sorted (keys here, sort.wgsl's passes).

// include: shared/record_fields.wgsl

// The words of one of the sort's arrays: its blocks' elements.
fn sortSpan() -> u32 {
    return (params.head.x + BLOCK - 1u) / BLOCK * BLOCK;
}

// The particle record r shows, of live: in the pool's order, its reverse,
// or the sort's (its indices in the first set after four passes).
fn particleAt(r: u32, live: u32) -> u32 {
    switch params.order.x {
        case ORDER_REVERSED: {
            return live - 1u - r;
        }
        case ORDER_SORTED: {
            return sortWords[sortSpan() + r];
        }
        default: {
            return r;
        }
    }
}

// Records: the live particles' up to the capacity, the workgroups (at
// most a dispatch's along one axis) striding over them; the count by the
// first invocation. The workgroups come from the numbers, as the step's:
// num_workgroups would take a constant buffer of its own on D3D12.
@compute @workgroup_size(64, 1, 1)
fn records(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = statistics[0];
    let n = min(live, params.head.y);
    if (id.x == 0u && params.shape.z != NONE) {
        count[params.shape.z] = n;
    }
    for (var r = id.x; r < n; r += params.shape.y * 64u) {
        writeRecord(r, particleAt(r, live));
    }
}

// A binary32's key, ordered as the values are, as the core's
// mvfxOrderedKey: a negative value's bits flipped, a positive one's sign
// set; -0 as +0.
fn orderedKey(value: f32) -> u32 {
    var bits = bitcast<u32>(value);
    bits = select(bits, 0u, bits == 0x80000000u);
    return bits ^ select(0x80000000u, 0xFFFFFFFFu, (bits >> 31u) != 0u);
}

// Particle i's sort key: its distance squared or depth from the eye in
// binary32, as the core's binary64 measure rounded, flipped back to front.
fn keyOf(i: u32) -> u32 {
    let d = placeOf(i) - params.eye.xyz;
    var measure = d.x * d.x + d.y * d.y + d.z * d.z;
    if (params.order.y == MEASURE_DEPTH) {
        let f = params.forward;
        measure = d.x * f.x + d.y * f.y + d.z * f.z;
    }
    return orderedKey(measure) ^ params.order.z;
}

// Keys: each live particle's key and index in the first set, a block a
// workgroup, and each block's counts of the lowest digit.
@compute @workgroup_size(256, 1, 1)
fn keys(@builtin(local_invocation_index) local: u32,
        @builtin(workgroup_id) group: vec3<u32>) {
    if (local == 0u) {
        liveCount = statistics[0];
    }
    let live = workgroupUniformLoad(&liveCount);
    let n = (params.head.x + BLOCK - 1u) / BLOCK;
    let span = n * BLOCK;
    let blocks = (live + BLOCK - 1u) / BLOCK;
    for (var b = group.x; b < blocks; b += params.order.w) {
        // An exchange, not a store: naga's HLSL of an atomic store names a
        // result it never declares.
        _ = atomicExchange(&tally[local], 0u);
        workgroupBarrier();
        for (var k = 0u; k < ITEMS; k++) {
            let i = b * BLOCK + k * CHUNK + local;
            if (i < live) {
                let key = keyOf(i);
                sortWords[i] = key;
                sortWords[span + i] = i;
                atomicAdd(&tally[key & 0xFFu], 1u);
            }
        }
        workgroupBarrier();
        digits[local * n + b] = atomicLoad(&tally[local]);
        workgroupBarrier();
    }
}

// An event's words in an emitter's events buffer, as sim.wgsl's.
const EVENT_WORDS: u32 = 28u;

// Event values: each of the step's events (statistics word 28) given its
// colour and size with the values over life, at the age and speed it
// keeps in its last words; the events from word head.z of the events
// buffer bound for the records.
@compute @workgroup_size(64, 1, 1)
fn eventValues(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = statistics[28];
    for (var e = id.x; e < n; e += params.shape.y * 64u) {
        let w = params.head.z + EVENT_WORDS * e;
        let age = ageAt(vec2<u32>(recordWords[w + 20u], recordWords[w + 21u]),
                        bitcast<f32>(recordWords[w + 22u]));
        let speed = bitcast<f32>(recordWords[w + 23u]);
        var color = vec4<f32>(0.0);
        var size = vec3<f32>(0.0);
        for (var k = 0u; k < 4u; k++) {
            color[k] = bitcast<f32>(recordWords[w + 13u + k]);
        }
        for (var k = 0u; k < 3u; k++) {
            size[k] = bitcast<f32>(recordWords[w + 17u + k]);
        }
        color = colorAt(color, speed, age);
        size = sizeAt(size, speed, age);
        for (var k = 0u; k < 4u; k++) {
            recordWords[w + 13u + k] = bitcast<u32>(color[k]);
        }
        for (var k = 0u; k < 3u; k++) {
            recordWords[w + 17u + k] = bitcast<u32>(size[k]);
        }
    }
}

// Lights: the particles that carry one, every n-th by serial,
// written as mvfxLight (8 words) in the pool's order, as the CPU path's
// mvfxInstance_WriteLights writes them, by three passes over blocks of the
// live particles: each block's carriers counted into the digit counts,
// the counts scanned in one workgroup (the total to the count word), each
// block's carriers written from its offset, up to the capacity. The light
// def's numbers stand in the fields' place: every, whether alpha scales,
// the block passes' workgroups; intensity, radius, radius by size.

var<workgroup> lightScan: array<u32, CHUNK>;

// Whether particle i carries a light: its serial's low word a multiple of
// every.
fn carriesLight(i: u32) -> bool {
    return wordAt(params.words.serial, i) % params.fields[0].x == 0u;
}

// The blocks of the live particles; the first word of the blocks' offsets
// in the digit counts, past a count for each block of the capacity.
fn lightBlocks(live: u32) -> u32 {
    return (live + BLOCK - 1u) / BLOCK;
}

fn lightOffsetsFirst() -> u32 {
    return (params.head.x + BLOCK - 1u) / BLOCK;
}

// The sum of value over the invocations before this one.
fn exclusiveLightScan(local: u32, value: u32) -> u32 {
    lightScan[local] = value;
    workgroupBarrier();
    for (var offset = 1u; offset < CHUNK; offset <<= 1u) {
        var add = 0u;
        if (local >= offset) {
            add = lightScan[local - offset];
        }
        workgroupBarrier();
        lightScan[local] += add;
        workgroupBarrier();
    }
    return lightScan[local] - value;
}

// The live count, the same in every invocation.
fn liveLights(local: u32) -> u32 {
    if (local == 0u) {
        liveCount = statistics[0];
    }
    return workgroupUniformLoad(&liveCount);
}

// Light count: each block's carriers, a block a workgroup at a time.
@compute @workgroup_size(256, 1, 1)
fn lightCount(@builtin(local_invocation_index) local: u32,
              @builtin(workgroup_id) group: vec3<u32>) {
    let live = liveLights(local);
    let blocks = lightBlocks(live);
    for (var b = group.x; b < blocks; b += params.fields[0].z) {
        var kept = 0u;
        for (var k = 0u; k < ITEMS; k++) {
            let i = b * BLOCK + local * ITEMS + k;
            kept += select(0u, 1u, i < live && carriesLight(i));
        }
        let before = exclusiveLightScan(local, kept);
        if (local == CHUNK - 1u) {
            digits[b] = before + kept;
        }
    }
}

// Light offsets, in one workgroup: each block's first light, and every
// light there is to the count word.
@compute @workgroup_size(256, 1, 1)
fn lightOffsets(@builtin(local_invocation_index) local: u32) {
    let blocks = lightBlocks(liveLights(local));
    let first = lightOffsetsFirst();
    let per = (blocks + CHUNK - 1u) / CHUNK;
    let lo = min(local * per, blocks);
    let hi = min(lo + per, blocks);
    var sum = 0u;
    for (var b = lo; b < hi; b++) {
        sum += digits[b];
    }
    var at = exclusiveLightScan(local, sum);
    for (var b = lo; b < hi; b++) {
        let n = digits[b];
        digits[first + b] = at;
        at += n;
    }
    if (local == CHUNK - 1u && params.shape.z != NONE) {
        count[params.shape.z] = at;
    }
}

// The light particle i carries, as the j-th: its place less the view's
// origin and radius; its colour times the intensity (and its alpha); its
// serial's low word.
fn writeLight(j: u32, i: u32) {
    let age = ageOf(i);
    let size = sizeOf(i, age) / params.scales.x;
    let largest = max(abs(size.x), max(abs(size.y), abs(size.z)));
    let color = colorOf(i, age);
    let numbers = params.fields[1];
    let scale = bitcast<f32>(numbers.x) * select(1.0, color.w, params.fields[0].y != 0u);
    let place = placeOf(i);
    let w = params.head.z + 8u * j;
    for (var k = 0u; k < 3u; k++) {
        recordWords[w + k] = bitcast<u32>(place[k]);
        recordWords[w + 4u + k] = bitcast<u32>(color[k] * scale);
    }
    recordWords[w + 3u] = bitcast<u32>(bitcast<f32>(numbers.y) + bitcast<f32>(numbers.z) * largest);
    recordWords[w + 7u] = wordAt(params.words.serial, i);
}

// Light write: each block's carriers from its first light, in order, up
// to the capacity.
@compute @workgroup_size(256, 1, 1)
fn lightWrite(@builtin(local_invocation_index) local: u32,
              @builtin(workgroup_id) group: vec3<u32>) {
    let live = liveLights(local);
    let blocks = lightBlocks(live);
    let first = lightOffsetsFirst();
    for (var b = group.x; b < blocks; b += params.fields[0].z) {
        var keep = 0u;
        var kept = 0u;
        for (var k = 0u; k < ITEMS; k++) {
            let i = b * BLOCK + local * ITEMS + k;
            let carries = i < live && carriesLight(i);
            keep |= select(0u, 1u << k, carries);
            kept += select(0u, 1u, carries);
        }
        var at = digits[first + b] + exclusiveLightScan(local, kept);
        for (var k = 0u; k < ITEMS; k++) {
            if ((keep & (1u << k)) != 0u) {
                if (at < params.head.y) {
                    writeLight(at, b * BLOCK + local * ITEMS + k);
                }
                at++;
            }
        }
    }
}
