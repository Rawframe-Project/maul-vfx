// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A liquid gas's step around the smoke's projection (the core's
// src/gas_liquid.c, APIC), its emitter's particles as their step left
// them:
//  - liquidGasKeys maps each live particle into the box's cells and frame
//    (MapRange), its affine matrix turned with it, and keys it by the cell
//    holding it (CellOf; past every cell outside the box); sort.wgsl's
//    stable radix sort by key; liquidGasRuns finds each cell's run;
//  - liquidGasMark marks the liquid cells and each cell's filled share and
//    the expansion it asks (Mark), and liquidGasCoarse each coarse level's
//    cells liquid when all their children are;
//  - liquidGasGather gives each face the momentum of the particles about it
//    over their trilinear weights (GatherFace), whether any reached it;
//  - gas.wgsl's projection, the liquid's air held at pressure 0;
//  - liquidGasKeep keeps the faces beside a liquid cell (LiquidFaceRows),
//    liquidGasExtend extends the velocity two layers into the air
//    (Extend), liquidGasWalls closes the walls (mvfxCloseGasWalls), and
//    liquidGasRead gives each particle in the box its velocity and affine
//    matrix back (ReadRange), and the square of the fastest speed of all
//    to the gas's statistics record (MVFX_GPU_GAS_FASTEST).

// include: shared/gas_numbers.wgsl

// include: shared/gas_lattice.wgsl

const ROW: u32 = 2097152u;
const CHUNK: u32 = 256u;
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;
const NONE: u32 = 0xFFFFFFFFu;

// A liquid: the emitter's particle capacity, its places' first array, its
// velocities', the workgroups of the particle passes; the map into the
// box (origin and scale, turn); its affine matrix's first array, a
// particle's share of a cell, the growth's time, the step's seconds.
struct Liquid {
    head: vec4<u32>,
    origin: vec4<f32>,
    rotation: vec4<f32>,
    affine: u32,
    volume: f32,
    growth: f32,
    seconds: f32,
}

// A pass's own words: unused but for the extension's layer.
struct Root {
    layer: u32,
    now: u32,
    unused: vec2<u32>,
}

var<immediate> root: Root;

@group(0) @binding(0) var<uniform> gas: Gas;
@group(0) @binding(1) var<uniform> liquid: Liquid;
@group(0) @binding(2) var<storage, read_write> fields: array<f32>;
// The emitter's particles after its step, which the read back writes,
// and its statistics (word 0 the live count).
@group(0) @binding(3) var<storage, read_write> particles: array<u32>;
@group(0) @binding(4) var<storage, read> statistics: array<u32>;
// The sort's keys and indices, twice over the capacity's blocks; each
// particle's place in cells, velocity and affine matrix in the box's
// frame (fifteen arrays of the capacity's blocks); each cell's run, its
// first and its end, the cell past every one last.
@group(0) @binding(5) var<storage, read_write> liquidWords: array<u32>;
// The sort's two sets of digit counts.
@group(0) @binding(6) var<storage, read_write> digits: array<u32>;
// The solve's levels, each level's liquid mask among them (a float a
// cell at its numbers' coefficients.w).
@group(0) @binding(7) var<storage, read_write> solve: array<f32>;
// The gas's statistics record (MVFX_GPU_GAS_STATISTICS_WORDS): the read
// back raises its word 3 to the fastest particle's squared speed (a
// positive binary32's bits order as the value), from the host's 0.
@group(0) @binding(8) var<storage, read_write> gasStatistics: array<atomic<u32>>;

var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> liveCount: u32;

fn spanOf() -> u32 {
    return (liquid.head.x + BLOCK - 1u) / BLOCK * BLOCK;
}

fn rotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// Where particle i's kept array a is: place (0 to 2), velocity (3 to 5),
// affine matrix (6 to 14).
fn keptAt(a: u32, i: u32) -> u32 {
    return (4u + a) * spanOf() + i;
}

fn kept(a: u32, i: u32) -> f32 {
    return bitcast<f32>(liquidWords[keptAt(a, i)]);
}

fn runsAt() -> u32 {
    return 19u * spanOf();
}

fn particleAt(first: u32, k: u32, i: u32) -> f32 {
    return bitcast<f32>(particles[(first + k) * liquid.head.x + i]);
}

// A matrix turned by q, q M q^-1, column by column (Conjugate).
fn conjugate(q: vec4<f32>, m: array<f32, 9>) -> array<f32, 9> {
    let back = vec4<f32>(-q.xyz, q.w);
    var out: array<f32, 9>;
    for (var j = 0u; j < 3u; j++) {
        var axis = vec3<f32>(0.0);
        axis[j] = 1.0;
        let v = rotate(back, axis);
        let column = vec3<f32>(m[0] * v.x + m[1] * v.y + m[2] * v.z,
                               m[3] * v.x + m[4] * v.y + m[5] * v.z,
                               m[6] * v.x + m[7] * v.y + m[8] * v.z);
        let turned = rotate(q, column);
        for (var k = 0u; k < 3u; k++) {
            out[3u * k + j] = turned[k];
        }
    }
    return out;
}

// The cell of a place in cells, the box's count past every cell outside
// it (a 2D gas's Z not judged), as CellOf.
fn cellAt(p: vec3<f32>) -> u32 {
    let n = gas.size.xyz;
    var cell = 0u;
    for (var k = 3u; k > 0u; k--) {
        let a = k - 1u;
        if (n[a] > 1u && !(p[a] >= 0.0 && p[a] < f32(n[a]))) {
            return cellCount(sizeOf());
        }
        let c = select(0u, u32(p[a]), n[a] > 1u);
        cell = cell * n[a] + c;
    }
    return cell;
}

// Particle i mapped into the box's cells and frame (MapRange), kept; its
// key.
fn mapParticle(i: u32) -> u32 {
    let x = vec3<f32>(particleAt(liquid.head.y, 0u, i), particleAt(liquid.head.y, 1u, i),
                      particleAt(liquid.head.y, 2u, i));
    let u = vec3<f32>(particleAt(liquid.head.z, 0u, i), particleAt(liquid.head.z, 1u, i),
                      particleAt(liquid.head.z, 2u, i));
    let sigma = liquid.origin.w;
    let local = rotate(liquid.rotation, x * sigma) + liquid.origin.xyz;
    let velocity = rotate(liquid.rotation, u * sigma);
    var m: array<f32, 9>;
    for (var a = 0u; a < 9u; a++) {
        m[a] = particleAt(liquid.affine, a, i);
    }
    let affine = conjugate(liquid.rotation, m);
    let n = gas.size.xyz;
    var place: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        place[k] = local[k] / gas.lattice.x + 0.5 * f32(n[k]);
        liquidWords[keptAt(k, i)] = bitcast<u32>(place[k]);
        liquidWords[keptAt(3u + k, i)] = bitcast<u32>(velocity[k]);
    }
    for (var a = 0u; a < 9u; a++) {
        liquidWords[keptAt(6u + a, i)] = bitcast<u32>(affine[a]);
    }
    return cellAt(place);
}

// Keys: each live particle mapped, its key and index in the sort's first
// set, a block a workgroup, and each block's counts of the lowest digit.
@compute @workgroup_size(256, 1, 1)
fn liquidGasKeys(@builtin(local_invocation_index) local: u32,
              @builtin(workgroup_id) group: vec3<u32>) {
    if (local == 0u) {
        liveCount = statistics[0];
    }
    let live = workgroupUniformLoad(&liveCount);
    let span = spanOf();
    let n = span / BLOCK;
    let blocks = (live + BLOCK - 1u) / BLOCK;
    for (var b = group.x; b < blocks; b += liquid.head.w) {
        _ = atomicExchange(&tally[local], 0u);
        workgroupBarrier();
        for (var k = 0u; k < ITEMS; k++) {
            let i = b * BLOCK + k * CHUNK + local;
            if (i < live) {
                let key = mapParticle(i);
                liquidWords[i] = key;
                liquidWords[span + i] = i;
                atomicAdd(&tally[key & 0xFFu], 1u);
            }
        }
        workgroupBarrier();
        digits[local * n + b] = atomicLoad(&tally[local]);
        workgroupBarrier();
    }
}

// Runs: each cell's first place in the sorted order and its end, from the
// cleared runs, written at each run's first and last element.
@compute @workgroup_size(64, 1, 1)
fn liquidGasRuns(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = statistics[0];
    let i = id.x + id.y * ROW;
    if (i >= live) {
        return;
    }
    let key = liquidWords[i];
    let runs = runsAt();
    if (i == 0u || liquidWords[i - 1u] != key) {
        liquidWords[runs + 2u * key] = i;
    }
    if (i + 1u == live || liquidWords[i + 1u] != key) {
        liquidWords[runs + 2u * key + 1u] = i + 1u;
    }
}

// A cell's particles: its run's end less its first (both 0 when empty).
fn heldBy(c: u32) -> u32 {
    let runs = runsAt();
    return liquidWords[runs + 2u * c + 1u] - liquidWords[runs + 2u * c];
}

// The finest level's marks (Mark): a cell is liquid when it holds a
// particle and is not solid; its filled share into the density (at most
// 1), the other scalars 0; the expansion a cell filled past full asks,
// into the work, which the projection's divergence takes.
@compute @workgroup_size(64, 1, 1)
fn liquidGasMark(@builtin(global_invocation_id) id: vec3<u32>) {
    let c = id.x + id.y * ROW;
    if (c >= cellCount(sizeOf())) {
        return;
    }
    let held = heldBy(c);
    var solid = false;
    if (gas.obstacleHead.x > 0u) {
        solid = u32(fields[gas.faces[0].w + c]) != 0u;
    }
    let wet = held > 0u && !solid;
    let fill = f32(held) * liquid.volume;
    fields[gas.cells[root.now].x + c] = min(fill, 1.0);
    fields[gas.cells[root.now].y + c] = 0.0;
    fields[gas.cells[root.now].z + c] = 0.0;
    var expansion = 0.0;
    if (wet && fill > 1.0) {
        expansion = (fill - 1.0) * gas.lattice.x / liquid.growth;
    }
    fields[gas.cells[0].w + c] = expansion;
    solve[gas.levels[0].coefficients.w + c] = select(0.0, 1.0, wet);
}

// A coarse level's marks (root.layer the level): a cell is liquid only
// when all its children within the finer level are.
@compute @workgroup_size(64, 1, 1)
fn liquidGasCoarse(@builtin(global_invocation_id) id: vec3<u32>) {
    let level = gas.levels[root.layer];
    let fine = gas.levels[root.layer - 1u];
    let n = level.size.xyz;
    let c = id.x + id.y * ROW;
    if (c >= n.x * n.y * n.z) {
        return;
    }
    let at = vec3<u32>(c % n.x, c / n.x % n.y, c / (n.x * n.y));
    let m = fine.size.xyz;
    var wet = true;
    for (var dz = 0u; dz < 2u; dz++) {
        for (var dy = 0u; dy < 2u; dy++) {
            for (var dx = 0u; dx < 2u; dx++) {
                let child = 2u * at + vec3<u32>(dx, dy, dz);
                if (all(child < m)) {
                    let i = (child.z * m.y + child.y) * m.x + child.x;
                    wet = wet && solve[fine.coefficients.w + i] != 0.0;
                }
            }
        }
    }
    solve[level.coefficients.w + c] = select(0.0, 1.0, wet);
}

// Every axis's faces, a one-cell axis's too (the core's gas->faces counts
// them, and a liquid gathers, extends and reads them back): point i's
// axis among them and its index, (3, 0) past them.
fn liquidFace(i: u32) -> vec2<u32> {
    let n = sizeOf();
    var first = 0u;
    for (var k = 0u; k < 3u; k++) {
        let count = faceCount(n, k);
        if (i < first + count) {
            return vec2<u32>(k, i - first);
        }
        first += count;
    }
    return vec2<u32>(3u, 0u);
}

fn liquidFaces() -> u32 {
    let n = sizeOf();
    return faceCount(n, 0u) + faceCount(n, 1u) + faceCount(n, 2u);
}

// Where a set of the faces' flags (0 or 1) starts: whether a face is set.
fn validAt(which: u32) -> u32 {
    return runsAt() + 2u * (cellCount(sizeOf()) + 1u) + which * liquidFaces();
}

// Particle i's trilinear weight at q (in cells), over the axes the gas
// has (WeightAt).
fn weightAt(i: u32, q: vec3<f32>) -> f32 {
    let n = sizeOf();
    var w = 1.0;
    for (var k = 0u; k < 3u; k++) {
        let d = abs(kept(k, i) - q[k]);
        w *= select(1.0, max(1.0 - d, 0.0), n[k] > 1u);
    }
    return w;
}

// Each face's velocity, the momentum v + C (x_face - x_p) of the
// particles in the cells about it (two across its axis, three along the
// others, held in the box) over their weights, cell by cell in z, y, x
// order and each cell's particles in the pool's order (GatherFace); set
// when any reached it.
@compute @workgroup_size(64, 1, 1)
fn liquidGasGather(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let face = liquidFace(point);
    if (face.x == 3u) {
        return;
    }
    let k = face.x;
    let n = sizeOf();
    let at = pointOf(pointsOf(n, k), face.y);
    var q: vec3<f32>;
    var low: vec3<u32>;
    var high: vec3<u32>;
    for (var j = 0u; j < 3u; j++) {
        q[j] = f32(at[j]) + select(0.5, 0.0, j == k);
        low[j] = select(at[j] - 1u, 0u, at[j] == 0u);
        high[j] = min(select(at[j] + 1u, at[j], j == k), n[j] - 1u);
    }
    let runs = runsAt();
    let span = spanOf();
    var sum = 0.0;
    var weights = 0.0;
    for (var z = low.z; z <= high.z; z++) {
        for (var y = low.y; y <= high.y; y++) {
            for (var x = low.x; x <= high.x; x++) {
                let c = (z * n.y + y) * n.x + x;
                for (var e = liquidWords[runs + 2u * c]; e < liquidWords[runs + 2u * c + 1u]; e++) {
                    let i = liquidWords[span + e];
                    let w = weightAt(i, q);
                    if (w > 0.0) {
                        var u = kept(3u + k, i);
                        for (var j = 0u; j < 3u; j++) {
                            u += kept(6u + 3u * k + j, i) * (q[j] - kept(j, i)) * gas.lattice.x;
                        }
                        sum += w * u;
                        weights += w;
                    }
                }
            }
        }
    }
    fields[gas.faces[root.now][k] + face.y] = select(0.0, sum / weights, weights > 0.0);
    liquidWords[validAt(0u) + point] = select(0u, 1u, weights > 0.0);
}

// Whether the cell at (cell coordinates, signed) is liquid; false outside
// the box.
fn wetAt(at: vec3<i32>) -> bool {
    let n = vec3<i32>(sizeOf());
    if (any(at < vec3<i32>(0)) || any(at >= n)) {
        return false;
    }
    let c = u32((at.z * n.y + at.y) * n.x + at.x);
    return solve[gas.levels[0].coefficients.w + c] != 0.0;
}

// After the projection: a face beside a liquid cell is set and keeps its
// velocity, any other is 0 (LiquidFaceRows).
@compute @workgroup_size(64, 1, 1)
fn liquidGasKeep(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let face = liquidFace(point);
    if (face.x == 3u) {
        return;
    }
    let k = face.x;
    var at = vec3<i32>(pointOf(pointsOf(sizeOf(), k), face.y));
    let above = wetAt(at);
    at[k] -= 1;
    let valid = above || wetAt(at);
    let f = gas.faces[root.now][k] + face.y;
    fields[f] = select(0.0, fields[f], valid);
    liquidWords[validAt(0u) + point] = select(0u, 1u, valid);
}

// A layer of the extension (root.layer: the set its flags are read
// from): a face set keeps its velocity; one not set takes the mean of the
// set faces beside it along each axis the gas has, set when any is
// (MeanAround, ExtendRows); into the other buffer and the other set.
@compute @workgroup_size(64, 1, 1)
fn liquidGasExtend(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let face = liquidFace(point);
    if (face.x == 3u) {
        return;
    }
    let k = face.x;
    let n = sizeOf();
    let m = pointsOf(n, k);
    let at = pointOf(m, face.y);
    let source = gas.faces[root.now][k];
    let flags = validAt(root.layer) + point - face.y;
    let valid = liquidWords[flags + face.y] != 0u;
    var out = fields[source + face.y];
    var sum = 0.0;
    var count = 0u;
    let stride = vec3<u32>(1u, m.x, m.x * m.y);
    for (var j = 0u; j < 3u; j++) {
        for (var side = 0u; side < 2u; side++) {
            if (n[j] == 1u || (side == 0u && at[j] == 0u) || (side == 1u && at[j] + 1u >= m[j])) {
                continue;
            }
            let g = select(face.y + stride[j], face.y - stride[j], side == 0u);
            if (liquidWords[flags + g] != 0u) {
                sum += fields[source + g];
                count += 1u;
            }
        }
    }
    if (!valid) {
        out = select(0.0, sum / f32(count), count > 0u);
    }
    fields[gas.faces[1u - root.now][k] + face.y] = out;
    liquidWords[validAt(1u - root.layer) + point] = select(0u, 1u, valid || count > 0u);
}

// After the extension, the closed walls' faces 0 (mvfxCloseGasWalls).
@compute @workgroup_size(64, 1, 1)
fn liquidGasWalls(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let face = liquidFace(point);
    if (face.x == 3u) {
        return;
    }
    let k = face.x;
    let at = pointOf(pointsOf(sizeOf(), k), face.y);
    if (isClosed(k, at)) {
        fields[gas.faces[root.now][k] + face.y] = 0.0;
    }
}

// The trilinear interpolant of axis k's faces at p (in cells), and its
// gradient in the box's lengths (Interpolate).
fn interpolate(k: u32, p: vec3<f32>, gradient: ptr<function, vec3<f32>>) -> f32 {
    let n = sizeOf();
    let m = pointsOf(n, k);
    var low: vec3<u32>;
    var high: vec3<u32>;
    var t: vec3<f32>;
    var inner: vec3<bool>;
    for (var j = 0u; j < 3u; j++) {
        let c = p[j] - select(0.5, 0.0, j == k);
        let held = min(max(c, 0.0), f32(m[j] - 1u));
        low[j] = u32(held);
        high[j] = select(low[j], low[j] + 1u, low[j] + 1u < m[j]);
        t[j] = held - f32(low[j]);
        inner[j] = held == c && high[j] > low[j] && n[j] > 1u;
    }
    let start = gas.faces[root.now][k];
    var value = 0.0;
    *gradient = vec3<f32>(0.0);
    for (var corner = 0u; corner < 8u; corner++) {
        let bit = vec3<u32>(corner & 1u, (corner >> 1u) & 1u, corner >> 2u);
        var at: vec3<u32>;
        var w: vec3<f32>;
        for (var j = 0u; j < 3u; j++) {
            at[j] = select(low[j], high[j], bit[j] != 0u);
            w[j] = select(1.0 - t[j], t[j], bit[j] != 0u);
        }
        let u = fields[start + indexOf(m, at)];
        value += w.x * w.y * w.z * u;
        for (var j = 0u; j < 3u; j++) {
            let slope = select(-1.0, 1.0, bit[j] != 0u);
            let others = w[(j + 1u) % 3u] * w[(j + 2u) % 3u];
            if (inner[j]) {
                (*gradient)[j] += slope * others * u / gas.lattice.x;
            }
        }
    }
    return value;
}

// Each particle in the box its velocity and affine matrix back from the
// faces, mapped back out of the box (ReadRange); every live particle's
// squared speed after it raises the speed word, and any marks the gas
// holding.
@compute @workgroup_size(64, 1, 1)
fn liquidGasRead(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x + id.y * ROW;
    if (i >= statistics[0]) {
        return;
    }
    let c = liquid.head.x;
    let v = liquid.head.z;
    var u = vec3<f32>(particleAt(v, 0u, i), particleAt(v, 1u, i), particleAt(v, 2u, i));
    let p = vec3<f32>(kept(0u, i), kept(1u, i), kept(2u, i));
    if (cellAt(p) < cellCount(sizeOf())) {
        var velocity: vec3<f32>;
        var m: array<f32, 9>;
        for (var k = 0u; k < 3u; k++) {
            var gradient: vec3<f32>;
            velocity[k] = interpolate(k, p, &gradient);
            for (var j = 0u; j < 3u; j++) {
                m[3u * k + j] = gradient[j];
            }
        }
        let back = vec4<f32>(-liquid.rotation.xyz, liquid.rotation.w);
        u = rotate(back, velocity) / liquid.origin.w;
        let affine = conjugate(back, m);
        for (var k = 0u; k < 3u; k++) {
            particles[(v + k) * c + i] = bitcast<u32>(u[k]);
        }
        for (var a = 0u; a < 9u; a++) {
            particles[(liquid.affine + a) * c + i] = bitcast<u32>(affine[a]);
        }
    }
    let square = u.x * u.x + u.y * u.y + u.z * u.z;
    atomicMax(&gasStatistics[3], bitcast<u32>(square));
    // The gas holds anything while its emitter holds a particle.
    atomicMax(&gasStatistics[0], 1u);
}
