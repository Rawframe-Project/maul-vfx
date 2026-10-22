// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A surface gas's step (the core's src/gas_surface.c), its emitter's
// particles as their step left them:
//  - surfaceKeys maps each live particle into the box's cells (MapRange)
//    and keys it by the bucket holding it (BucketOf; past every bucket
//    beyond them), and marks the gas's statistics record holding while
//    the emitter has particles; sort.wgsl's stable radix sort by key, so
//    a bucket keeps the pool's order; surfaceRuns finds each bucket's run;
//  - surfaceField gives each cell its distance from the buckets about it
//    (FieldAt), into the first buffer;
//  - surfaceSmooth is a smoothing pass (root.layer 0 the mean, 1 the
//    lower of the mean and the cell) from buffer root.now into the other.

// include: shared/gas_numbers.wgsl

// include: shared/gas_lattice.wgsl

const ROW: u32 = 2097152u;
const CHUNK: u32 = 256u;
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;
// The statistics record's holds word (MVFX_GPU_GAS_HOLDS).
const HOLDS: u32 = 0u;
// Cyclic Jacobi's sweeps (gas_surface.c's JACOBI_SWEEPS).
const SWEEPS: u32 = 3u;

// A surface: the emitter's particle capacity, its places' first array,
// unused, the keys' workgroups; the map into the box (origin and scale,
// turn); the buckets along each axis and a bucket's cells; the reach and
// a particle's radius in cells, the eigenvalue's thresholds; the reach in
// the box's lengths, unused.
struct Surface {
    head: vec4<u32>,
    origin: vec4<f32>,
    rotation: vec4<f32>,
    buckets: vec4<u32>,
    shape: vec4<f32>,
    far: vec4<f32>,
}

// A pass's own words: a smoothing pass's kind and the buffer it reads.
struct Root {
    layer: u32,
    now: u32,
    unused: vec2<u32>,
}

var<immediate> root: Root;

@group(0) @binding(0) var<uniform> gas: Gas;
@group(0) @binding(1) var<uniform> surface: Surface;
@group(0) @binding(2) var<storage, read_write> fields: array<f32>;
// The emitter's particles after its step, and its statistics (word 0 the
// live count).
@group(0) @binding(3) var<storage, read> particles: array<u32>;
@group(0) @binding(4) var<storage, read> statistics: array<u32>;
// The sort's keys and indices, twice over the capacity's blocks; each
// particle's place in cells (three arrays of the capacity's blocks); each
// bucket's run, its first and its end, the bucket past every one last.
@group(0) @binding(5) var<storage, read_write> surfaceWords: array<u32>;
// The sort's two sets of digit counts.
@group(0) @binding(6) var<storage, read_write> digits: array<u32>;
// The gas's statistics record (MVFX_GPU_GAS_STATISTICS_WORDS).
@group(0) @binding(7) var<storage, read_write> gasStatistics: array<u32>;

var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> liveCount: u32;

fn spanOf() -> u32 {
    return (surface.head.x + BLOCK - 1u) / BLOCK * BLOCK;
}

// 3, or 2 for a gas in the XY plane.
fn axesOf() -> u32 {
    return select(2u, 3u, gas.size.z > 1u);
}

// Where particle i's place along axis k is.
fn placeAt(k: u32, i: u32) -> u32 {
    return (4u + k) * spanOf() + i;
}

fn place(k: u32, i: u32) -> f32 {
    return bitcast<f32>(surfaceWords[placeAt(k, i)]);
}

fn runsAt() -> u32 {
    return 7u * spanOf();
}

fn bucketCount() -> u32 {
    return surface.buckets.x * surface.buckets.y * surface.buckets.z;
}

fn rotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

fn particleAt(k: u32, i: u32) -> f32 {
    return bitcast<f32>(particles[(surface.head.y + k) * surface.head.x + i]);
}

// Particle i mapped into the box's cells (MapRange), kept: a 2D gas's at
// its cells' Z. Its bucket, the count past every bucket for one beyond
// them (BucketOf).
fn mapParticle(i: u32) -> u32 {
    let x = vec3<f32>(particleAt(0u, i), particleAt(1u, i), particleAt(2u, i));
    let local = rotate(surface.rotation, x * surface.origin.w) + surface.origin.xyz;
    let n = gas.size.xyz;
    let axes = axesOf();
    var p: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        p[k] = select(0.5, local[k] / gas.lattice.x + 0.5 * f32(n[k]), k < axes);
        surfaceWords[placeAt(k, i)] = bitcast<u32>(p[k]);
    }
    let m = surface.buckets.xyz;
    var key = 0u;
    for (var k = 3u; k > 0u; k--) {
        let a = k - 1u;
        // The first bucket lies before the box's first cell.
        let b = select(0.0, floor(p[a] / f32(surface.buckets.w)) + 1.0, a < axes);
        if (!(b >= 0.0 && b < f32(m[a]))) {
            return bucketCount();
        }
        key = key * m[a] + u32(b);
    }
    return key;
}

// Keys: each live particle mapped, its key and index in the sort's first
// set, a block a workgroup, and each block's counts of the lowest digit;
// the record marked holding while there are particles.
@compute @workgroup_size(256, 1, 1)
fn surfaceKeys(@builtin(local_invocation_index) local: u32,
               @builtin(workgroup_id) group: vec3<u32>) {
    if (local == 0u) {
        liveCount = statistics[0];
    }
    let live = workgroupUniformLoad(&liveCount);
    if (group.x == 0u && local == 0u && live > 0u) {
        gasStatistics[HOLDS] = 1u;
    }
    let span = spanOf();
    let n = span / BLOCK;
    let blocks = (live + BLOCK - 1u) / BLOCK;
    for (var b = group.x; b < blocks; b += surface.head.w) {
        _ = atomicExchange(&tally[local], 0u);
        workgroupBarrier();
        for (var k = 0u; k < ITEMS; k++) {
            let i = b * BLOCK + k * CHUNK + local;
            if (i < live) {
                let key = mapParticle(i);
                surfaceWords[i] = key;
                surfaceWords[span + i] = i;
                atomicAdd(&tally[key & 0xFFu], 1u);
            }
        }
        workgroupBarrier();
        digits[local * n + b] = atomicLoad(&tally[local]);
        workgroupBarrier();
    }
}

// Runs: each bucket's first place in the sorted order and its end, from
// the cleared runs, written at each run's first and last element.
@compute @workgroup_size(64, 1, 1)
fn surfaceRuns(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = statistics[0];
    let i = id.x + id.y * ROW;
    if (i >= live) {
        return;
    }
    let key = surfaceWords[i];
    let runs = runsAt();
    if (i == 0u || surfaceWords[i - 1u] != key) {
        surfaceWords[runs + 2u * key] = i;
    }
    if (i + 1u == live || surfaceWords[i + 1u] != key) {
        surfaceWords[runs + 2u * key + 1u] = i + 1u;
    }
}

// The sums a cell gathers (gas_surface.c's Sums): the kernel's weights,
// the weighted offsets to the particles, the kernel's gradients, and
// their outer products with the offsets (XX, YY, ZZ, XY, XZ, YZ).
struct Sums {
    weight: f32,
    offset: vec3<f32>,
    gradient: vec3<f32>,
    outer: array<f32, 6>,
}

// Adds the particles of bucket b within the reach of point x (in cells)
// in their order (GatherBucket).
fn gatherBucket(b: u32, x: vec3<f32>, sums: Sums) -> Sums {
    var out = sums;
    let runs = runsAt();
    let span = spanOf();
    let inverse = 1.0 / (surface.shape.x * surface.shape.x);
    for (var e = surfaceWords[runs + 2u * b]; e < surfaceWords[runs + 2u * b + 1u]; e++) {
        let i = surfaceWords[span + e];
        let d = vec3<f32>(place(0u, i) - x.x, place(1u, i) - x.y, place(2u, i) - x.z);
        let a = 1.0 - (d.x * d.x + d.y * d.y + d.z * d.z) * inverse;
        if (!(a > 0.0)) {
            continue;
        }
        // The kernel (1 - s^2)^3 and its gradient's factor 6 (1 - s^2)^2 /
        // R^2 along the offset.
        let k = a * a * a;
        let c = 6.0 * a * a * inverse;
        out.weight += k;
        out.offset += k * d;
        out.gradient += c * d;
        out.outer[0] += c * d.x * d.x;
        out.outer[1] += c * d.y * d.y;
        out.outer[2] += c * d.z * d.z;
        out.outer[3] += c * d.x * d.y;
        out.outer[4] += c * d.x * d.z;
        out.outer[5] += c * d.y * d.z;
    }
    return out;
}

// Zeroes a's (p, q) entry by a rotation in its plane, a turned on both
// sides (JacobiRotate), a row-major.
fn jacobiRotate(given: array<f32, 9>, p: u32, q: u32) -> array<f32, 9> {
    var a = given;
    if (a[3u * p + q] == 0.0) {
        return a;
    }
    let theta = (a[3u * q + q] - a[3u * p + p]) / (2.0 * a[3u * p + q]);
    let t = select(-1.0, 1.0, theta >= 0.0) / (abs(theta) + sqrt(theta * theta + 1.0));
    let c = 1.0 / sqrt(t * t + 1.0);
    let s = t * c;
    for (var k = 0u; k < 3u; k++) {
        let x = a[3u * k + p];
        let y = a[3u * k + q];
        a[3u * k + p] = c * x - s * y;
        a[3u * k + q] = s * x + c * y;
    }
    for (var k = 0u; k < 3u; k++) {
        let x = a[3u * p + k];
        let y = a[3u * q + k];
        a[3u * p + k] = c * x - s * y;
        a[3u * q + k] = s * x + c * y;
    }
    return a;
}

// The largest eigenvalue of the symmetric m (XX, YY, ZZ, XY, XZ, YZ)
// (mvfxSurfaceEigenvalue): in closed form in 2D; in 3D Gershgorin's
// bound when below low, else three cyclic Jacobi sweeps.
fn eigenvalue(m: array<f32, 6>, axes: u32, low: f32) -> f32 {
    if (axes == 2u) {
        let half = 0.5 * (m[0] - m[1]);
        return 0.5 * (m[0] + m[1]) + sqrt(half * half + m[3] * m[3]);
    }
    let bound = max(max(m[0] + abs(m[3]) + abs(m[4]), m[1] + abs(m[3]) + abs(m[5])),
                    m[2] + abs(m[4]) + abs(m[5]));
    if (bound < low) {
        return bound;
    }
    var a = array<f32, 9>(m[0], m[3], m[4], m[3], m[1], m[5], m[4], m[5], m[2]);
    for (var sweep = 0u; sweep < SWEEPS; sweep++) {
        for (var r = 0u; r < 3u; r++) {
            a = jacobiRotate(a, select(0u, 1u, r == 2u), select(2u, 1u, r == 0u));
        }
    }
    return max(max(a[0], a[4]), a[8]);
}

// The distance at a point from its sums, a particle within its reach, in
// cells (DistanceOf).
fn distanceOf(sums: Sums) -> f32 {
    let mean = sums.offset / sums.weight;
    let low = surface.shape.z;
    let high = surface.shape.w;
    var f = 1.0;
    if (low < high) {
        // The Jacobian's symmetric part: (outer - (mean G^T + G mean^T) / 2)
        // over the weight.
        var i = array<u32, 6>(0u, 1u, 2u, 0u, 0u, 1u);
        var j = array<u32, 6>(0u, 1u, 2u, 1u, 2u, 2u);
        var m: array<f32, 6>;
        for (var e = 0u; e < 6u; e++) {
            let both = mean[i[e]] * sums.gradient[j[e]] + sums.gradient[i[e]] * mean[j[e]];
            m[e] = (sums.outer[e] - 0.5 * both) / sums.weight;
        }
        let largest = eigenvalue(m, axesOf(), low);
        let g = (high - largest) / (high - low);
        let rest = 1.0 - g;
        f = select(min(max(1.0 - rest * rest * rest, 0.0), 1.0), 1.0, largest < low);
    }
    return sqrt(mean.x * mean.x + mean.y * mean.y + mean.z * mean.z) - surface.shape.y * f;
}

// Each cell's distance at its center, in the box's lengths, gathered from
// its bucket and those about it in Z, Y, X order (FieldAt); the reach
// without a particle within it.
@compute @workgroup_size(64, 1, 1)
fn surfaceField(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = sizeOf();
    let c = id.x + id.y * ROW;
    if (c >= cellCount(n)) {
        return;
    }
    let at = pointOf(n, c);
    let p = vec3<f32>(at) + vec3<f32>(0.5);
    let axes = axesOf();
    let m = surface.buckets.xyz;
    var low = vec3<u32>(0u);
    var high = vec3<u32>(0u);
    for (var k = 0u; k < axes; k++) {
        let b = at[k] / surface.buckets.w + 1u;
        low[k] = b - 1u;
        high[k] = min(b + 1u, m[k] - 1u);
    }
    var sums = Sums(0.0, vec3<f32>(0.0), vec3<f32>(0.0), array<f32, 6>(0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
    for (var bz = low.z; bz <= high.z; bz++) {
        for (var by = low.y; by <= high.y; by++) {
            for (var bx = low.x; bx <= high.x; bx++) {
                sums = gatherBucket((bz * m.y + by) * m.x + bx, p, sums);
            }
        }
    }
    var value = surface.far.x;
    if (sums.weight > 0.0) {
        value = distanceOf(sums) * gas.lattice.x;
    }
    fields[gas.cells[0].x + c] = value;
}

// A smoothing pass (SmoothRows): each cell the mean of it and its
// neighbors along the gas's axes in buffer root.now, a cell at the box's
// edge standing in for the one beyond; with root.layer 1 only where it
// lowers the cell. Into the other buffer.
@compute @workgroup_size(64, 1, 1)
fn surfaceSmooth(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = sizeOf();
    let c = id.x + id.y * ROW;
    if (c >= cellCount(n)) {
        return;
    }
    let at = pointOf(n, c);
    let source = gas.cells[root.now].x;
    let step = vec3<u32>(1u, n.x, n.x * n.y);
    let axes = axesOf();
    var sum = fields[source + c];
    for (var k = 0u; k < axes; k++) {
        sum += fields[source + select(c, c - step[k], at[k] > 0u)];
        sum += fields[source + select(c, c + step[k], at[k] + 1u < n[k])];
    }
    let mean = sum / f32(1u + 2u * axes);
    let written = gas.cells[1u - root.now].x;
    fields[written + c] = select(mean, min(mean, fields[source + c]), root.layer != 0u);
}
