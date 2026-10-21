// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter pouring into a smoke gas (the core's src/gas_splat.c), as a
// gather in the particles' order: after the emitter's step, each live
// particle's place and velocity in the box's cells and frame, and its key,
// the cell holding it (past every cell outside the box; a 2D gas's Z not
// judged); sort.wgsl's stable radix sort by key; each cell's run in the
// sorted order. Each substep then, a pass over the cells and one over the
// faces, each point walking the runs of the 3 by 3 by 3 cells about it,
// merged by particle index, which is the pool's order, and doing for each
// particle reaching it what SplatOne does to it.

// include: shared/gas_numbers.wgsl

const ROW: u32 = 2097152u;
const CHUNK: u32 = 256u;
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;
const NONE: u32 = 0xFFFFFFFFu;

// A splat: the emitter's particle capacity, its places' first array, its
// velocities', the workgroups of the particle passes; the map into the
// box (origin and scale, turn); the splat's rates a second, unused; each
// substep's pull.
struct Splat {
    head: vec4<u32>,
    origin: vec4<f32>,
    rotation: vec4<f32>,
    rates: vec4<f32>,
    pulls: array<vec4<f32>, 2>,
}

// The substep, the gas's buffer current, unused, unused.
struct Root {
    substep: u32,
    now: u32,
    unused: vec2<u32>,
}

var<immediate> root: Root;

@group(0) @binding(0) var<uniform> gas: Gas;
@group(0) @binding(1) var<uniform> splat: Splat;
@group(0) @binding(2) var<storage, read_write> fields: array<f32>;
// The emitter's particles after its step, and its statistics (word 0 the
// live count).
@group(0) @binding(3) var<storage, read> particles: array<u32>;
@group(0) @binding(4) var<storage, read> statistics: array<u32>;
// The sort's keys and indices, twice over the capacity's blocks; each
// particle's place in cells and velocity, six arrays of the capacity's
// blocks; each cell's run, its first and its end, the cell past every one
// last.
@group(0) @binding(5) var<storage, read_write> splatWords: array<u32>;
// The sort's two sets of digit counts.
@group(0) @binding(6) var<storage, read_write> digits: array<u32>;

var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> liveCount: u32;

fn spanOf() -> u32 {
    return (splat.head.x + BLOCK - 1u) / BLOCK * BLOCK;
}

fn rotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

fn cellCount() -> u32 {
    let n = gas.size.xyz;
    return n.x * n.y * n.z;
}

// Where particle i's place in cells and velocity are kept.
fn placeAt(i: u32) -> u32 {
    return 4u * spanOf() + i;
}

fn runsAt() -> u32 {
    return 10u * spanOf();
}

fn keptPlace(i: u32) -> vec3<f32> {
    let s = spanOf();
    let at = placeAt(i);
    return vec3<f32>(bitcast<f32>(splatWords[at]), bitcast<f32>(splatWords[at + s]),
                     bitcast<f32>(splatWords[at + 2u * s]));
}

fn keptVelocity(i: u32) -> vec3<f32> {
    let s = spanOf();
    let at = placeAt(i) + 3u * s;
    return vec3<f32>(bitcast<f32>(splatWords[at]), bitcast<f32>(splatWords[at + s]),
                     bitcast<f32>(splatWords[at + 2u * s]));
}

// Particle i's place in the box's cells and its velocity in its frame, as
// mvfxGasSplatInto maps them, kept; its key.
fn mapParticle(i: u32) -> u32 {
    let c = splat.head.x;
    let p = splat.head.y;
    let v = splat.head.z;
    let x = vec3<f32>(bitcast<f32>(particles[p * c + i]), bitcast<f32>(particles[(p + 1u) * c + i]),
                      bitcast<f32>(particles[(p + 2u) * c + i]));
    let u = vec3<f32>(bitcast<f32>(particles[v * c + i]), bitcast<f32>(particles[(v + 1u) * c + i]),
                      bitcast<f32>(particles[(v + 2u) * c + i]));
    let sigma = splat.origin.w;
    let local = rotate(splat.rotation, x * sigma) + splat.origin.xyz;
    let velocity = rotate(splat.rotation, u * sigma);
    let n = gas.size.xyz;
    var place: vec3<f32>;
    var cell = vec3<u32>(0u);
    var inside = true;
    for (var k = 0u; k < 3u; k++) {
        place[k] = local[k] / gas.lattice.x + 0.5 * f32(n[k]);
        if (n[k] > 1u) {
            inside = inside && place[k] >= 0.0 && place[k] <= f32(n[k]);
            cell[k] = min(u32(max(place[k], 0.0)), n[k] - 1u);
        }
    }
    let s = spanOf();
    let at = placeAt(i);
    for (var k = 0u; k < 3u; k++) {
        splatWords[at + k * s] = bitcast<u32>(place[k]);
        splatWords[at + (3u + k) * s] = bitcast<u32>(velocity[k]);
    }
    if (!inside) {
        return cellCount();
    }
    return (cell.z * n.y + cell.y) * n.x + cell.x;
}

// Keys: each live particle mapped, its key and index in the sort's first
// set, a block a workgroup, and each block's counts of the lowest digit.
@compute @workgroup_size(256, 1, 1)
fn splatKeys(@builtin(local_invocation_index) local: u32,
            @builtin(workgroup_id) group: vec3<u32>) {
    if (local == 0u) {
        liveCount = statistics[0];
    }
    let live = workgroupUniformLoad(&liveCount);
    let span = spanOf();
    let n = span / BLOCK;
    let blocks = (live + BLOCK - 1u) / BLOCK;
    for (var b = group.x; b < blocks; b += splat.head.w) {
        _ = atomicExchange(&tally[local], 0u);
        workgroupBarrier();
        for (var k = 0u; k < ITEMS; k++) {
            let i = b * BLOCK + k * CHUNK + local;
            if (i < live) {
                let key = mapParticle(i);
                splatWords[i] = key;
                splatWords[span + i] = i;
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
fn splatRuns(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = statistics[0];
    let i = id.x + id.y * ROW;
    if (i >= live) {
        return;
    }
    let key = splatWords[i];
    let runs = runsAt();
    if (i == 0u || splatWords[i - 1u] != key) {
        splatWords[runs + 2u * key] = i;
    }
    if (i + 1u == live || splatWords[i + 1u] != key) {
        splatWords[runs + 2u * key + 1u] = i + 1u;
    }
}

// The 8 points of an array (the cells' for axis 3, else the faces' across
// axis) nearest place p, held within the array, and their weights, as the
// core's Nearest; corner c's point at[c].
struct Nearest {
    at: array<u32, 8>,
    weight: array<f32, 8>,
}

fn nearest(axis: u32, p: vec3<f32>) -> Nearest {
    var m = gas.size.xyz;
    if (axis < 3u) {
        m[axis] += 1u;
    }
    var low: vec3<u32>;
    var high: vec3<u32>;
    var t: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        let shift = select(0.5, 0.0, k == axis);
        let c = min(max(p[k] - shift, 0.0), f32(m[k] - 1u));
        let base = floor(c);
        low[k] = u32(base);
        high[k] = select(low[k], low[k] + 1u, low[k] + 1u < m[k]);
        t[k] = c - base;
    }
    var out: Nearest;
    for (var corner = 0u; corner < 8u; corner++) {
        var point: vec3<u32>;
        var weight = 1.0;
        for (var k = 0u; k < 3u; k++) {
            let up = ((corner >> k) & 1u) != 0u;
            point[k] = select(low[k], high[k], up);
            weight *= select(1.0 - t[k], t[k], up);
        }
        out.at[corner] = (point.z * m.y + point.y) * m.x + point.x;
        out.weight[corner] = weight;
    }
    return out;
}

// The runs of the cells about a point at `at` (a cell's coordinates, or a
// face's): those of the cells from one below to one above along each axis
// within the box, which hold every particle whose nearest points can
// include it.
struct Walk {
    next: array<u32, 27>,
    end: array<u32, 27>,
    head: array<u32, 27>,
    count: u32,
}

fn walkAbout(at: vec3<u32>) -> Walk {
    let n = gas.size.xyz;
    var low: vec3<u32>;
    var high: vec3<u32>;
    for (var k = 0u; k < 3u; k++) {
        low[k] = select(0u, at[k] - 1u, at[k] > 0u);
        high[k] = min(at[k] + 1u, n[k] - 1u);
    }
    let span = spanOf();
    let runs = runsAt();
    var walk: Walk;
    walk.count = 0u;
    for (var z = low.z; z <= high.z; z++) {
        for (var y = low.y; y <= high.y; y++) {
            for (var x = low.x; x <= high.x; x++) {
                let cell = (z * n.y + y) * n.x + x;
                let first = splatWords[runs + 2u * cell];
                let end = splatWords[runs + 2u * cell + 1u];
                if (first < end) {
                    walk.next[walk.count] = first;
                    walk.end[walk.count] = end;
                    walk.head[walk.count] = splatWords[span + first];
                    walk.count += 1u;
                }
            }
        }
    }
    return walk;
}

// The walk's next particle in the pool's order, NONE when it is done.
fn nextOf(walk: ptr<function, Walk>) -> u32 {
    var least = NONE;
    var which = 0u;
    for (var r = 0u; r < (*walk).count; r++) {
        if ((*walk).head[r] < least) {
            least = (*walk).head[r];
            which = r;
        }
    }
    if (least == NONE) {
        return NONE;
    }
    let at = (*walk).next[which] + 1u;
    (*walk).next[which] = at;
    (*walk).head[which] = select(NONE, splatWords[spanOf() + at], at < (*walk).end[which]);
    return least;
}

// Cells: each cell's scalars given each particle's rates times its
// weights at the cell, in the pool's order.
@compute @workgroup_size(64, 1, 1)
fn splatCells(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = gas.size.xyz;
    if (point >= cellCount()) {
        return;
    }
    let at = vec3<u32>(point % n.x, point / n.x % n.y, point / (n.x * n.y));
    var walk = walkAbout(at);
    if (walk.count == 0u) {
        return;
    }
    let h = gas.substeps[root.substep].span.x;
    let rates = splat.rates.xyz * h;
    var values: vec3<f32>;
    for (var f = 0u; f < 3u; f++) {
        values[f] = fields[gas.cells[root.now][f] + point];
    }
    for (var i = nextOf(&walk); i != NONE; i = nextOf(&walk)) {
        let near = nearest(3u, keptPlace(i));
        for (var c = 0u; c < 8u; c++) {
            if (near.at[c] == point) {
                for (var f = 0u; f < 3u; f++) {
                    values[f] += near.weight[c] * rates[f];
                }
            }
        }
    }
    for (var f = 0u; f < 3u; f++) {
        fields[gas.cells[root.now][f] + point] = values[f];
    }
}

// The faces across axis k, none across an axis of one cell.
fn facesAcross(k: u32) -> u32 {
    var m = gas.size.xyz;
    if (m[k] == 1u) {
        return 0u;
    }
    m[k] += 1u;
    return m.x * m.y * m.z;
}

// Faces: each face pulled toward each particle's velocity across its axis
// by the substep's pull times its weights at the face, in the pool's
// order (none for a pull of 0, as the core skips it).
@compute @workgroup_size(64, 1, 1)
fn splatFaces(@builtin(global_invocation_id) id: vec3<u32>) {
    var point = id.x + id.y * ROW;
    var k = 0u;
    for (; k < 3u && point >= facesAcross(k); k++) {
        point -= facesAcross(k);
    }
    let pull = splat.pulls[root.substep / 4u][root.substep % 4u];
    if (k == 3u || !(pull > 0.0)) {
        return;
    }
    var m = gas.size.xyz;
    m[k] += 1u;
    let at = vec3<u32>(point % m.x, point / m.x % m.y, point / (m.x * m.y));
    var walk = walkAbout(at);
    if (walk.count == 0u) {
        return;
    }
    let i = gas.faces[root.now][k] + point;
    var face = fields[i];
    for (var p = nextOf(&walk); p != NONE; p = nextOf(&walk)) {
        let near = nearest(k, keptPlace(p));
        let v = keptVelocity(p)[k];
        for (var c = 0u; c < 8u; c++) {
            if (near.at[c] == point) {
                face += near.weight[c] * pull * (v - face);
            }
        }
    }
    fields[i] = face;
}
