// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's particle contacts, as the core's (src/contact.c), on the
// compacted pool after the step's scatter and before its bounds:
//  - contactRadii gives each particle its radius (its size's x over life
//    at the step's end, as the record kernels find it, times the scale),
//    copies its place as built, and finds the largest radius;
//  - contactKeys gives each its cell's key from the built places, cells
//    of twice the largest radius and a slop, and the blocks' counts of the
//    lowest digit; sort.wgsl's four radix passes order them, stable;
//  - contactProject, once a pass, moves each particle by half of each
//    overlap along the line from the other, averaged over its contacts,
//    from the last pass's places into scratch (Jacobi), its caps counted
//    in the first; contactCommit takes the scratch places;
//  - contactImpulses gives the pairs still touching (within the slop) and
//    closing restitution and friction, averaged, into scratch;
//    contactKick adds them to the velocities.
// A particle's contacts are the entries of its built cell's 27 cells in
// the grid's order, at most the most; sqrt and division make the moves
// differ from the core's within its rounding.

// include: shared/record_fields.wgsl

// include: shared/grid_cells.wgsl

// How far past their radii's sum two particles still touch for impulses,
// as a share of the sum (the core's 1 + CONTACT_SLOP in binary32).
const SLOP_REACH: f32 = 1.00100004673;
// Where projection parts a pair to, as a share of the sum (the core's
// CONTACT_REST in binary32).
const REST_REACH: f32 = 1.00049996376;

// The statistics record's word counting the particles whose contacts in
// the first pass were more than the most (MVFX_GPU_CONTACTS_DROPPED).
const STATISTICS_CONTACTS_CAPPED: u32 = 47u;

// The count buffer's words: the particles the grid holds (the sort's
// statistics word), the largest radius's bits.
const COUNT_HELD: u32 = 0u;
const COUNT_LARGEST: u32 = 1u;

struct Contacts {
    // The particle buffer's capacity, the most contacts a pass takes, the
    // workgroups of the block passes and of the others.
    head: vec4<u32>,
    // The grid's mask, unused.
    grid: vec4<u32>,
    // The radius scale, the restitution, the friction, unused.
    response: vec4<f32>,
}

// The pass: which projection (0 for the first), unused.
struct Root {
    projection: u32,
    unused0: u32,
    unused1: u32,
    unused2: u32,
}

@group(0) @binding(0) var<uniform> params: Records;
@group(0) @binding(1) var<storage, read_write> particles: array<u32>;
@group(0) @binding(2) var<storage, read_write> statistics: array<atomic<u32>>;
@group(0) @binding(3) var<storage, read> grids: array<u32>;
// The places as built, the scratch places or kicks, the radii: three,
// three and one arrays of the capacity.
@group(0) @binding(4) var<storage, read_write> recordWords: array<u32>;
@group(0) @binding(5) var<storage, read_write> count: array<atomic<u32>>;
// Keys, then indices, twice over the capacity's blocks; two sets of digit
// counts, 256 a block each, digit-major (the sort's).
@group(0) @binding(6) var<storage, read_write> sortWords: array<u32>;
@group(0) @binding(7) var<storage, read_write> digits: array<u32>;
@group(0) @binding(8) var<uniform> contact: Contacts;

var<immediate> root: Root;

var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> held: u32;

fn builtAt(k: u32, i: u32) -> f32 {
    return bitcast<f32>(recordWords[k * contact.head.x + i]);
}

fn scratchAt(k: u32, i: u32) -> f32 {
    return bitcast<f32>(recordWords[(3u + k) * contact.head.x + i]);
}

fn setScratch(k: u32, i: u32, value: f32) {
    recordWords[(3u + k) * contact.head.x + i] = bitcast<u32>(value);
}

fn radiusOf(i: u32) -> f32 {
    return bitcast<f32>(recordWords[6u * contact.head.x + i]);
}

fn setFloat(first: u32, i: u32, value: f32) {
    particles[first * contact.head.x + i] = bitcast<u32>(value);
}

// The cells' inverse size: of twice the largest radius and the slop,
// divided exactly; 0 where the cells are not finite and above 0, when the
// core takes no contacts.
fn inverseCell() -> f32 {
    let cell = 2.0 * bitcast<f32>(atomicLoad(&count[COUNT_LARGEST])) * SLOP_REACH;
    if (!(cell > 0.0 && cell < bitcast<f32>(0x7F800000u))) {
        return 0.0;
    }
    return divide(1.0, cell);
}

// Radii: each particle's radius and built place, the largest radius, the
// particles the grid will hold.
@compute @workgroup_size(64, 1, 1)
fn contactRadii(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = atomicLoad(&statistics[0]);
    let c = contact.head.x;
    if (id.x == 0u) {
        _ = atomicExchange(&count[COUNT_HELD], live);
    }
    for (var i = id.x; i < live; i += contact.head.w * 64u) {
        let r = abs(sizeOf(i, ageOf(i)).x) * contact.response.x;
        recordWords[6u * c + i] = bitcast<u32>(r);
        let at = vectorAt(params.words.place, i);
        for (var k = 0u; k < 3u; k++) {
            recordWords[k * c + i] = bitcast<u32>(at[k]);
        }
        // Radii are at or above 0, so their bits order as they do.
        _ = atomicMax(&count[COUNT_LARGEST], bitcast<u32>(r));
    }
}

// The words of one of the sort's arrays over a capacity.
fn spanOf(capacity: u32) -> u32 {
    return (capacity + BLOCK - 1u) / BLOCK * BLOCK;
}

// Contact keys: each particle's key and index in the first set, a block a
// workgroup at a time, and each block's counts of the lowest digit; none
// held where the core takes no contacts.
@compute @workgroup_size(256, 1, 1)
fn contactKeys(@builtin(local_invocation_index) local: u32,
               @builtin(workgroup_id) group: vec3<u32>) {
    let inverse = inverseCell();
    if (local == 0u) {
        held = select(0u, atomicLoad(&count[COUNT_HELD]), inverse > 0.0);
    }
    let n = workgroupUniformLoad(&held);
    let blocks = spanOf(contact.head.x) / BLOCK;
    let span = blocks * BLOCK;
    for (var b = group.x; b < (n + BLOCK - 1u) / BLOCK; b += contact.head.z) {
        _ = atomicExchange(&tally[local], 0u);
        workgroupBarrier();
        for (var k = 0u; k < ITEMS; k++) {
            let i = b * BLOCK + k * CHUNK + local;
            if (i < n) {
                let key = keyOf(vec3<i32>(cellOf(builtAt(0u, i), inverse),
                                          cellOf(builtAt(1u, i), inverse),
                                          cellOf(builtAt(2u, i), inverse)),
                                contact.grid.x);
                sortWords[i] = key;
                sortWords[span + i] = i;
                atomicAdd(&tally[key & 0xFFu], 1u);
            }
        }
        workgroupBarrier();
        digits[local * blocks + b] = atomicLoad(&tally[local]);
        workgroupBarrier();
    }
    if (group.x == 0u && local == 0u && n == 0u) {
        _ = atomicExchange(&count[COUNT_HELD], 0u);
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

// What a particle's contacts gave: the projection's shift or the
// impulses' change, the contacts that gave it, whether more touched than
// the most.
struct Touches {
    sum: vec3<f32>,
    count: u32,
    capped: bool,
}

// The unit line from j to i into line and its length; false when they
// share a place.
fn lineOf(at: vec3<f32>, j: u32, line: ptr<function, vec3<f32>>,
          length: ptr<function, f32>) -> bool {
    let other = vectorAt(params.words.place, j);
    var square = 0.0;
    for (var k = 0u; k < 3u; k++) {
        (*line)[k] = at[k] - other[k];
        square += (*line)[k] * (*line)[k];
    }
    *length = sqrt(square);
    for (var k = 0u; k < 3u; k++) {
        (*line)[k] = select(0.0, quotient((*line)[k], *length), *length > 0.0);
    }
    return *length > 0.0;
}

// Adds contact j of particle i (at, radius r, velocity v) to the touches:
// the projection's half overlap along the line, or for impulses a closing
// pair's restitution and friction.
fn addTouch(at: vec3<f32>, r: f32, v: vec3<f32>, j: u32, impulses: bool,
            touches: ptr<function, Touches>) {
    var line = vec3<f32>(0.0);
    var length = 0.0;
    if (!lineOf(at, j, &line, &length)) {
        // No direction to part them along.
        return;
    }
    if (!impulses) {
        let half = 0.5 * ((r + radiusOf(j)) * REST_REACH - length);
        for (var k = 0u; k < 3u; k++) {
            (*touches).sum[k] += half * line[k];
        }
        (*touches).count += 1u;
        return;
    }
    let relative = v - vectorAt(params.words.velocity, j);
    let closing = relative.x * line.x + relative.y * line.y + relative.z * line.z;
    if (!(closing < 0.0)) {
        return;
    }
    let restitution = contact.response.y;
    let friction = contact.response.z;
    for (var k = 0u; k < 3u; k++) {
        let sliding = relative[k] - closing * line[k];
        (*touches).sum[k] += 0.5 * (-(1.0 + restitution) * closing * line[k] - friction * sliding);
    }
    (*touches).count += 1u;
}

// Particle i's touches: the particles within reach times their radii's
// sum of its place now, from the 27 cells about its built place in the
// grid's order (a key two cells share visited once), at most the most.
fn touchesOf(i: u32, inverse: f32, reach: f32, impulses: bool) -> Touches {
    let n = atomicLoad(&count[COUNT_HELD]);
    let span = spanOf(contact.head.x);
    let at = vectorAt(params.words.place, i);
    let v = vectorAt(params.words.velocity, i);
    let r = radiusOf(i);
    let cell = vec3<i32>(cellOf(builtAt(0u, i), inverse), cellOf(builtAt(1u, i), inverse),
                         cellOf(builtAt(2u, i), inverse));
    var visited: array<u32, 27>;
    var visits = 0u;
    var touched = 0u;
    var touches = Touches();
    for (var m = 0u; m < 27u; m++) {
        let around = cell + vec3<i32>(i32(m % 3u) - 1, i32(m / 3u % 3u) - 1, i32(m / 9u) - 1);
        let key = keyOf(around, contact.grid.x);
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
            let other = vectorAt(params.words.place, j);
            var square = 0.0;
            for (var k = 0u; k < 3u; k++) {
                let d = at[k] - other[k];
                square += d * d;
            }
            let within = (r + radiusOf(j)) * reach;
            if (j == i || !(square < within * within)) {
                continue;
            }
            if (touched == contact.head.y) {
                touches.capped = true;
                return touches;
            }
            touched++;
            addTouch(at, r, v, j, impulses, &touches);
        }
    }
    return touches;
}

// A touches' sum averaged over the contacts that gave it.
fn shareOf(touches: Touches) -> vec3<f32> {
    var share = 0.0;
    if (touches.count > 0u) {
        share = divide(1.0, f32(touches.count));
    }
    return touches.sum * share;
}

// Projection: each particle's place moved by its overlaps' average, into
// scratch; the caps counted in the first pass.
@compute @workgroup_size(64, 1, 1)
fn contactProject(@builtin(global_invocation_id) id: vec3<u32>) {
    let inverse = inverseCell();
    let n = atomicLoad(&count[COUNT_HELD]);
    var capped = 0u;
    for (var i = id.x; i < n; i += contact.head.w * 64u) {
        let touches = touchesOf(i, inverse, 1.0, false);
        capped += select(0u, 1u, touches.capped);
        let at = vectorAt(params.words.place, i);
        let shift = shareOf(touches);
        for (var k = 0u; k < 3u; k++) {
            setScratch(k, i, at[k] + shift[k]);
        }
    }
    if (root.projection == 0u && capped > 0u) {
        atomicAdd(&statistics[STATISTICS_CONTACTS_CAPPED], capped);
    }
}

// Commit: each particle takes its place from scratch.
@compute @workgroup_size(64, 1, 1)
fn contactCommit(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = atomicLoad(&count[COUNT_HELD]);
    for (var i = id.x; i < n; i += contact.head.w * 64u) {
        for (var k = 0u; k < 3u; k++) {
            setFloat(params.words.place + k, i, scratchAt(k, i));
        }
    }
}

// Impulses: each particle's kick from the pairs still touching within the
// slop and closing, averaged, into scratch.
@compute @workgroup_size(64, 1, 1)
fn contactImpulses(@builtin(global_invocation_id) id: vec3<u32>) {
    let inverse = inverseCell();
    let n = atomicLoad(&count[COUNT_HELD]);
    for (var i = id.x; i < n; i += contact.head.w * 64u) {
        let kick = shareOf(touchesOf(i, inverse, SLOP_REACH, true));
        for (var k = 0u; k < 3u; k++) {
            setScratch(k, i, kick[k]);
        }
    }
}

// Kick: each particle's velocity gains its kick from scratch.
@compute @workgroup_size(64, 1, 1)
fn contactKick(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = atomicLoad(&count[COUNT_HELD]);
    let first = params.words.velocity;
    for (var i = id.x; i < n; i += contact.head.w * 64u) {
        for (var k = 0u; k < 3u; k++) {
            setFloat(first + k, i, floatAt(first + k, i) + scratchAt(k, i));
        }
    }
}
