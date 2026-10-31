// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A particle liquid's passes (the core's src/liquid.c, position-based
// fluids), between the sim kernel's moves and slides (sim/liquid.wgsl):
// each substep the grid of the predicted places (keys hashed from cells
// of the kernel radius, sorted by sort.wgsl, each key's entries in index
// order); each iteration the multipliers, then the corrected places; then
// the velocities' gain, XSPH viscosity, the curls and confinement. Every
// gather walks a particle's neighbors as the core's Neighbors: the 27
// cells about its predicted place (x fastest, a key two cells share
// visited once), those within the kernel radius at the places now, at
// most the most, in the grid's order. The square roots and divisions are
// within their rounding of the core's.

// include: shared/exact.wgsl

// include: shared/grid_cells.wgsl

const CHUNK: u32 = 256u;
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;
const NONE: u32 = 0xFFFFFFFFu;
// The alive word of a particle ended before the moves (sim/step.wgsl's).
const LIVE_ENDED: u32 = 2u;
// The statistics word counting the particles whose neighbors were capped
// (the record's MVFX_GPU_CAPPED_NEIGHBOR_QUERY_COUNT).
const STATISTICS_CAPPED: u32 = 46u;

// A liquid's numbers: the particle buffer's capacity and where a
// particle's place, velocity and alive word are; the grid's mask, the
// most neighbors, the tensile correction's power, whether it is planar;
// the inverse cell; in the kernels' unit (a power of two) the kernel
// radius and its square, poly6's factor; the spiky gradient's factor, the
// inverse rest density, the softening, the tensile correction's inverse
// reference; its scale, the viscosity, the confinement's strength over a
// second (vorticity times spacing, over the unit), unused; the plan's
// ticks, the substeps, the workgroups of a particle pass and of a block
// pass; the unit and its inverse, both powers of two, so an offset or a
// move scaled between the particles' space and the unit by multiplying
// is exact, unused, unused.
struct Liquid {
    head: vec4<u32>,
    grid: vec4<u32>,
    cells: vec4<f32>,
    kernel: vec4<f32>,
    rates: vec4<f32>,
    plan: vec4<u32>,
    units: vec4<f32>,
}

// The substep, and whether the pass counts the capped (the first
// iteration of the first substep), unused, unused.
struct Root {
    substep: u32,
    counts: u32,
    unused0: u32,
    unused1: u32,
}

var<immediate> root: Root;

@group(0) @binding(0) var<uniform> liquid: Liquid;
// The particles: in the current buffer their places and velocities, in
// the other the predicted places (in its place words) and the corrected
// ones (in its velocity words).
@group(0) @binding(1) var<storage, read_write> source: array<u32>;
@group(0) @binding(2) var<storage, read_write> other: array<u32>;
@group(0) @binding(3) var<storage, read_write> statistics: array<atomic<u32>>;
// The sort's keys and indices, twice over the capacity's blocks; then the
// multipliers, three arrays of scratch velocities and three of curls,
// each of the capacity.
@group(0) @binding(4) var<storage, read_write> words: array<u32>;
@group(0) @binding(5) var<storage, read_write> digits: array<u32>;

var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> held: u32;

fn capacity() -> u32 {
    return liquid.head.x;
}

fn spanOf() -> u32 {
    return (capacity() + BLOCK - 1u) / BLOCK * BLOCK;
}

fn liveOf() -> u32 {
    return atomicLoad(&statistics[0]);
}

fn vectorIn(buffer: u32, first: u32, i: u32) -> vec3<f32> {
    let c = capacity();
    var v: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        if (buffer == 0u) {
            v[k] = bitcast<f32>(source[(first + k) * c + i]);
        } else {
            v[k] = bitcast<f32>(other[(first + k) * c + i]);
        }
    }
    return v;
}

fn setVector(buffer: u32, first: u32, i: u32, v: vec3<f32>) {
    let c = capacity();
    for (var k = 0u; k < 3u; k++) {
        if (buffer == 0u) {
            source[(first + k) * c + i] = bitcast<u32>(v[k]);
        } else {
            other[(first + k) * c + i] = bitcast<u32>(v[k]);
        }
    }
}

fn placeOf(i: u32) -> vec3<f32> {
    return vectorIn(0u, liquid.head.y, i);
}

fn velocityOf(i: u32) -> vec3<f32> {
    return vectorIn(0u, liquid.head.z, i);
}

fn predictedOf(i: u32) -> vec3<f32> {
    return vectorIn(1u, liquid.head.y, i);
}

fn isEnded(i: u32) -> bool {
    return source[liquid.head.w * capacity() + i] == LIVE_ENDED;
}

// The scratch arrays: the multipliers, the scratch velocities, the curls.
fn multiplierAt() -> u32 {
    return 4u * spanOf();
}

fn scratchAt() -> u32 {
    return 4u * spanOf() + capacity();
}

fn curlsAt() -> u32 {
    return 4u * spanOf() + 4u * capacity();
}

fn wordVector(first: u32, i: u32) -> vec3<f32> {
    let c = capacity();
    return vec3<f32>(bitcast<f32>(words[first + i]), bitcast<f32>(words[first + c + i]),
                     bitcast<f32>(words[first + 2u * c + i]));
}

fn setWordVector(first: u32, i: u32, v: vec3<f32>) {
    let c = capacity();
    for (var k = 0u; k < 3u; k++) {
        words[first + k * c + i] = bitcast<u32>(v[k]);
    }
}

fn cellAt(at: vec3<f32>) -> vec3<i32> {
    let inverse = liquid.cells.x;
    return vec3<i32>(cellOf(at.x, inverse), cellOf(at.y, inverse), cellOf(at.z, inverse));
}

// Keys: each live particle's key (its predicted place's cell's; past every
// key for one ended) and index in the sort's first set, a block a
// workgroup, and each block's counts of the lowest digit.
@compute @workgroup_size(256, 1, 1)
fn liquidKeys(@builtin(local_invocation_index) local: u32,
              @builtin(workgroup_id) group: vec3<u32>) {
    if (local == 0u) {
        held = liveOf();
    }
    let n = workgroupUniformLoad(&held);
    let span = spanOf();
    let blocks = span / BLOCK;
    for (var b = group.x; b < (n + BLOCK - 1u) / BLOCK; b += liquid.plan.w) {
        _ = atomicExchange(&tally[local], 0u);
        workgroupBarrier();
        for (var k = 0u; k < ITEMS; k++) {
            let i = b * BLOCK + k * CHUNK + local;
            if (i < n) {
                var key = NONE;
                if (!isEnded(i)) {
                    key = keyOf(cellAt(predictedOf(i)), liquid.grid.x);
                }
                words[i] = key;
                words[span + i] = i;
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
        let k = words[m];
        if (k < key || (after && k == key)) {
            a = m + 1u;
        } else {
            b = m;
        }
    }
    return a;
}

// poly6 at a squared distance, 0 at the radius and past it.
fn poly6(square: f32) -> f32 {
    let d = liquid.cells.z - square;
    return select(0.0, liquid.cells.w * d * d * d, d > 0.0);
}

// The gradient of particle i's density with respect to a neighbor at
// offset d (from it to i) of length length: the spiky gradient over the
// rest density.
fn gradientOf(d: vec3<f32>, length: f32) -> vec3<f32> {
    let e = liquid.cells.y - length;
    var factor = 0.0;
    if (e > 0.0 && length > 0.0) {
        factor = -liquid.kernel.x * e * e / length;
    }
    return factor * d * liquid.kernel.y;
}

// The tensile correction at a squared distance.
fn tensile(square: f32) -> f32 {
    let ratio = poly6(square) * liquid.kernel.w;
    var power = 1.0;
    for (var p = 0u; p < liquid.grid.z; p++) {
        power *= ratio;
    }
    return -liquid.rates.x * power;
}

// What a gather sums over a particle's neighbors.
const GATHER_MULTIPLIER: u32 = 0u;
const GATHER_CORRECTION: u32 = 1u;
const GATHER_VISCOSITY: u32 = 2u;
const GATHER_CURL: u32 = 3u;
const GATHER_CONFINE: u32 = 4u;

struct Sums {
    capped: bool,
    density: f32,
    own: vec3<f32>,
    sizes: f32,
    sum: vec3<f32>,
}

// A curl's size.
fn swirlSize(i: u32) -> f32 {
    let w = wordVector(curlsAt(), i);
    return sqrt(w.x * w.x + w.y * w.y + w.z * w.z);
}

// Neighbor j's share in particle i's gather, as each core gather adds it.
fn add(mode: u32, i: u32, j: u32, x: vec3<f32>, d: vec3<f32>, sums: ptr<function, Sums>) {
    let length = sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    let square = length * length;
    switch mode {
        case GATHER_MULTIPLIER: {
            let g = gradientOf(d, length);
            (*sums).density += poly6(square);
            (*sums).own += g;
            (*sums).sizes += g.x * g.x + g.y * g.y + g.z * g.z;
        }
        case GATHER_CORRECTION: {
            let g = gradientOf(d, length);
            let m = multiplierAt();
            let weight = bitcast<f32>(words[m + i]) + bitcast<f32>(words[m + j]) +
                         tensile(square);
            (*sums).sum += weight * g;
        }
        case GATHER_VISCOSITY: {
            let w = poly6(square) * liquid.kernel.y;
            (*sums).sum += (velocityOf(j) - velocityOf(i)) * w;
        }
        case GATHER_CURL: {
            let g = gradientOf(d, length);
            let dv = velocityOf(j) - velocityOf(i);
            (*sums).sum += vec3<f32>(dv.z * g.y - dv.y * g.z, dv.x * g.z - dv.z * g.x,
                                     dv.y * g.x - dv.x * g.y);
        }
        default: {
            let g = gradientOf(d, length);
            (*sums).sum += (swirlSize(j) - (*sums).density) * g;
        }
    }
}

// Particle i's gather over its neighbors, as the core's Neighbors walks
// them, their offsets in the kernels' unit; a confinement's sums start
// from i's curl's size.
fn gather(mode: u32, i: u32) -> Sums {
    var sums = Sums();
    if (mode == GATHER_CONFINE) {
        sums.density = swirlSize(i);
    }
    let x = placeOf(i);
    let cell = cellAt(predictedOf(i));
    let n = liveOf();
    let span = spanOf();
    var visited: array<u32, 27>;
    var visits = 0u;
    var count = 0u;
    for (var m = 0u; m < 27u; m++) {
        let around = cell + vec3<i32>(i32(m % 3u) - 1, i32(m / 3u % 3u) - 1, i32(m / 9u) - 1);
        let key = keyOf(around, liquid.grid.x);
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
            let j = words[span + e];
            let d = (x - placeOf(j)) * liquid.units.y;
            let square = d.x * d.x + d.y * d.y + d.z * d.z;
            if (j == i || !(square < liquid.cells.z)) {
                continue;
            }
            if (count == liquid.grid.y) {
                sums.capped = true;
                return sums;
            }
            count++;
            add(mode, i, j, x, d, &sums);
        }
    }
    return sums;
}

// The substep's seconds.
fn substepSeconds() -> f32 {
    let ticks = liquid.plan.x;
    let substeps = liquid.plan.y;
    let since = ticks * root.substep / substeps;
    return secondsOf(vec2<u32>(ticks * (root.substep + 1u) / substeps - since, 0u));
}

// Multipliers: each particle's minus its density's excess over the
// gradients' squared sizes and the softening; the capped counted when the
// root says.
@compute @workgroup_size(64, 1, 1)
fn liquidMultipliers(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = liveOf();
    for (var i = id.x; i < n; i += liquid.plan.z * 64u) {
        if (isEnded(i)) {
            continue;
        }
        var sums = gather(GATHER_MULTIPLIER, i);
        let density = poly6(0.0) + sums.density;
        let error = max(density * liquid.kernel.y - 1.0, 0.0);
        let sizes = sums.sizes + (sums.own.x * sums.own.x + sums.own.y * sums.own.y +
                                  sums.own.z * sums.own.z);
        words[multiplierAt() + i] = bitcast<u32>(-error / (sizes + liquid.kernel.z));
        if (sums.capped && root.counts != 0u) {
            atomicAdd(&statistics[STATISTICS_CAPPED], 1u);
        }
    }
}

// Corrections: each particle's place plus its neighbors' gradients weighted
// by both multipliers and the tensile correction, a move in the kernels'
// unit (Z held for a planar liquid), into the other buffer's velocity
// words.
@compute @workgroup_size(64, 1, 1)
fn liquidCorrections(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = liveOf();
    for (var i = id.x; i < n; i += liquid.plan.z * 64u) {
        if (isEnded(i)) {
            continue;
        }
        var delta = gather(GATHER_CORRECTION, i).sum;
        if (liquid.grid.w != 0u) {
            delta.z = 0.0;
        }
        setVector(1u, liquid.head.z, i, placeOf(i) + delta * liquid.units.x);
    }
}

// Gain: each velocity plus its place's change from its prediction over the
// substep.
@compute @workgroup_size(64, 1, 1)
fn liquidGain(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = liveOf();
    let h = substepSeconds();
    for (var i = id.x; i < n; i += liquid.plan.z * 64u) {
        if (isEnded(i)) {
            continue;
        }
        let x = placeOf(i);
        let p = predictedOf(i);
        var v = velocityOf(i);
        for (var k = 0u; k < 3u; k++) {
            v[k] += (x[k] - p[k]) / h;
        }
        setVector(0u, liquid.head.z, i, v);
    }
}

// Viscosity: each velocity blended toward its neighbors' (Z held for a
// planar liquid), into the scratch velocities.
@compute @workgroup_size(64, 1, 1)
fn liquidViscosity(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = liveOf();
    for (var i = id.x; i < n; i += liquid.plan.z * 64u) {
        if (isEnded(i)) {
            continue;
        }
        var blend = gather(GATHER_VISCOSITY, i).sum;
        if (liquid.grid.w != 0u) {
            blend.z = 0.0;
        }
        setWordVector(scratchAt(), i, velocityOf(i) + liquid.rates.y * blend);
    }
}

// Curls: each particle's, into the curls.
@compute @workgroup_size(64, 1, 1)
fn liquidCurls(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = liveOf();
    for (var i = id.x; i < n; i += liquid.plan.z * 64u) {
        if (isEnded(i)) {
            continue;
        }
        setWordVector(curlsAt(), i, gather(GATHER_CURL, i).sum);
    }
}

// Confinement over the substep: each velocity plus h times the strength
// times N x w, N the gradient of the curls' size over the larger of its
// size and 1e-4 of the curl's over the kernel radius, into the scratch
// velocities.
@compute @workgroup_size(64, 1, 1)
fn liquidConfine(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = liveOf();
    let strength = substepSeconds() * liquid.rates.z;
    for (var i = id.x; i < n; i += liquid.plan.z * 64u) {
        if (isEnded(i)) {
            continue;
        }
        let size = swirlSize(i);
        let eta = gather(GATHER_CONFINE, i).sum;
        let length = sqrt(eta.x * eta.x + eta.y * eta.y + eta.z * eta.z);
        let least = 1e-4 * size / liquid.cells.y;
        let bound = select(least, length, length > least);
        var scale = 0.0;
        if (bound > 0.0) {
            scale = strength / bound;
        }
        let w = wordVector(curlsAt(), i);
        var f = vec3<f32>(eta.y * w.z - eta.z * w.y, eta.z * w.x - eta.x * w.z,
                          eta.x * w.y - eta.y * w.x);
        if (liquid.grid.w != 0u) {
            f.z = 0.0;
        }
        setWordVector(scratchAt(), i, velocityOf(i) + scale * f);
    }
}

// Keep: the scratch velocities made the velocities.
@compute @workgroup_size(64, 1, 1)
fn liquidKeep(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = liveOf();
    for (var i = id.x; i < n; i += liquid.plan.z * 64u) {
        if (!isEnded(i)) {
            setVector(0u, liquid.head.z, i, wordVector(scratchAt(), i));
        }
    }
}
