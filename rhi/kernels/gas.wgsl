// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A smoke gas's step, as the core's (src/gas_step.c, src/gas_pressure.c),
// a pass over cells or faces a stage, every value the core's expression
// in its order:
//  - gasAreas and gasCoefficients make each level's open areas, then its
//    faces' coefficients and cells' weights (mvfxGasAreas), once;
//  - each substep: gasPourCells (the sources into the cells), gasFaces
//    (the sources' velocities, then buoyancy), gasCarry and
//    gasCarryCells (semi-Lagrangian into the other buffers), then the
//    projection: gasDivergence, a V-cycle's gasJacobi sweeps, gasRestrict
//    (the residual summed over a coarse cell's children), gasProlong, and
//    gasTail running the levels of at most TAIL_CELLS cells in one
//    workgroup; gasCorrect closes the walls and takes the pressure's
//    rise off each open face.
// The fields live in one buffer (faces then cells, two of each, offsets
// in the numbers), the solve's levels in another.

const NONE: u32 = 0xFFFFFFFFu;
const GROUP: u32 = 64u;
// The invocations of a row of a point pass's dispatch: 32,768 workgroups,
// past which the host adds rows.
const ROW: u32 = 2097152u;
const TAIL_GROUP: u32 = 256u;
const SMOOTH_PASSES: u32 = 2u;
const COARSEST_PASSES: u32 = 64u;
const SPHERE: u32 = 0u;

// include: shared/gas_numbers.wgsl

// What a pass works on: the substep, the buffer current at its start, a
// level, and a pass's own word.
struct Root {
    substep: u32,
    now: u32,
    level: u32,
    word: u32,
}

@group(0) @binding(0) var<uniform> gas: Gas;
@group(0) @binding(1) var<storage, read_write> fields: array<f32>;
@group(0) @binding(2) var<storage, read_write> solve: array<f32>;

var<immediate> root: Root;

// The points of an array along each axis: the cells' (axis 3), or the
// faces' across axis.
fn pointsOf(n: vec3<u32>, axis: u32) -> vec3<u32> {
    var m = n;
    if (axis < 3u) {
        m[axis] += 1u;
    }
    return m;
}

fn indexOf(m: vec3<u32>, at: vec3<u32>) -> u32 {
    return (at.z * m.y + at.y) * m.x + at.x;
}

fn pointOf(m: vec3<u32>, i: u32) -> vec3<u32> {
    return vec3<u32>(i % m.x, i / m.x % m.y, i / (m.x * m.y));
}

fn sizeOf() -> vec3<u32> {
    return gas.size.xyz;
}

fn cellCount(n: vec3<u32>) -> u32 {
    return n.x * n.y * n.z;
}

fn faceCount(n: vec3<u32>, k: u32) -> u32 {
    let m = pointsOf(n, k);
    return m.x * m.y * m.z;
}

// Which axis's faces point i of a pass over every face lies on (3 past
// them), and its index among them; an axis of one cell has none.
fn faceAxis(n: vec3<u32>, i: u32) -> vec2<u32> {
    var first = 0u;
    for (var k = 0u; k < 3u; k++) {
        let count = select(0u, faceCount(n, k), n[k] > 1u);
        if (i < first + count) {
            return vec2<u32>(k, i - first);
        }
        first += count;
    }
    return vec2<u32>(3u, 0u);
}

fn allFaces(n: vec3<u32>) -> u32 {
    var count = 0u;
    for (var k = 0u; k < 3u; k++) {
        count += select(0u, faceCount(n, k), n[k] > 1u);
    }
    return count;
}

// Where buffer b's array of axis (3 for a scalar f) starts.
fn arrayAt(b: u32, axis: u32, f: u32) -> u32 {
    if (axis < 3u) {
        return gas.faces[b][axis];
    }
    return gas.cells[b][f];
}

// A lattice's values at place p (in cells): the cells' for axis 3, else
// the faces' across axis, as mvfxGasSample: trilinear, p held within the
// array's points (a 2D array's four terms alone, which are the trilinear
// sum's bit for bit).
fn sampleAt(start: u32, axis: u32, p: vec3<f32>) -> f32 {
    let m = pointsOf(sizeOf(), axis);
    var low: vec3<u32>;
    var high: vec3<u32>;
    var t: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        let shift = select(0.5, 0.0, k == axis);
        let place = min(max(p[k] - shift, 0.0), f32(m[k] - 1u));
        low[k] = u32(place);
        high[k] = select(low[k], low[k] + 1u, low[k] + 1u < m[k]);
        t[k] = place - f32(low[k]);
    }
    var wx = vec2<f32>(1.0 - t.x, t.x);
    var wy = vec2<f32>(1.0 - t.y, t.y);
    var x = vec2<u32>(low.x, high.x);
    if (m.z == 1u) {
        let lower = start + low.y * m.x;
        let upper = start + high.y * m.x;
        var sum = 0.0;
        sum += wx[0] * wy[0] * fields[lower + x[0]];
        sum += wx[1] * wy[0] * fields[lower + x[1]];
        sum += wx[0] * wy[1] * fields[upper + x[0]];
        sum += wx[1] * wy[1] * fields[upper + x[1]];
        return sum;
    }
    var wz = vec2<f32>(1.0 - t.z, t.z);
    var y = vec2<u32>(low.y, high.y);
    var z = vec2<u32>(low.z, high.z);
    var sum = 0.0;
    for (var corner = 0u; corner < 8u; corner++) {
        let a = corner & 1u;
        let b = (corner >> 1u) & 1u;
        let d = corner >> 2u;
        let weight = wx[a] * wy[b] * wz[d];
        sum += weight * fields[start + (z[d] * m.y + y[b]) * m.x + x[a]];
    }
    return sum;
}

// The velocity at place p in buffer b, in cells a second.
fn velocityAt(b: u32, p: vec3<f32>) -> vec3<f32> {
    let n = sizeOf();
    var out = vec3<f32>(0.0);
    for (var k = 0u; k < 3u; k++) {
        if (n[k] > 1u || k < 2u) {
            out[k] = sampleAt(arrayAt(b, k, 0u), k, p) / gas.lattice.x;
        }
    }
    return out;
}

// Where the point at p came from over a substep of h, by the midpoint
// rule through buffer b's velocity (mvfxGasTraceBack).
fn traceBack(b: u32, p: vec3<f32>, h: f32) -> vec3<f32> {
    let v = velocityAt(b, p);
    var mid: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        mid[k] = p[k] - 0.5 * h * v[k];
    }
    let w = velocityAt(b, mid);
    var out: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        out[k] = p[k] - h * w[k];
    }
    return out;
}

// A point's place in cells: a cell's center, or (across axis) a face's.
fn placeOf(axis: u32, at: vec3<u32>) -> vec3<f32> {
    var out: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        out[k] = f32(at[k]) + select(0.5, 0.0, k == axis);
    }
    return out;
}

// A point's place in the box's frame.
fn localOf(axis: u32, at: vec3<u32>) -> vec3<f32> {
    let n = sizeOf();
    var out: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        let place = f32(at[k]) + select(0.5, 0.0, k == axis);
        out[k] = (place - 0.5 * f32(n[k])) * gas.lattice.x;
    }
    return out;
}

// Whether a source holds a place in the box's frame.
fn holds(s: Source, local: vec3<f32>) -> bool {
    var square = 0.0;
    var inside = true;
    for (var k = 0u; k < 3u; k++) {
        let d = local[k] - s.center[k];
        square += d * d;
        inside = inside && abs(d) <= s.extents[k];
    }
    if (bitcast<u32>(s.center.w) == SPHERE) {
        return square <= s.extents.w * s.extents.w;
    }
    return inside;
}

// Whether a point of an array lies within the points a source can hold
// (the core's Reach: its bounds' reach, a point more on each side).
fn reaches(s: Source, axis: u32, at: vec3<u32>) -> bool {
    let n = sizeOf();
    let m = pointsOf(n, axis);
    let sphere = bitcast<u32>(s.center.w) == SPHERE;
    var within = true;
    for (var k = 0u; k < 3u; k++) {
        let extent = select(s.extents[k], s.extents.w, sphere);
        let offset = 0.5 * f32(n[k]) - select(0.5, 0.0, k == axis);
        let low = (s.center[k] - extent) / gas.lattice.x + offset;
        let high = (s.center[k] + extent) / gas.lattice.x + offset;
        var first = 0;
        if (low > 1.0) {
            first = i32(min(low, f32(m[k]))) - 1;
        }
        var end = i32(m[k]);
        if (high < f32(m[k]) - 2.0) {
            end = i32(max(high, 0.0)) + 2;
        }
        within = within && i32(at[k]) >= first && i32(at[k]) < end;
    }
    return within;
}

// The fine cells a cell of level l spans along k at index i.
fn widthOf(l: u32, k: u32, i: u32) -> f32 {
    let fine = sizeOf()[k];
    if (fine == 1u) {
        return 1.0;
    }
    let span = gas.levels[l].areas.w;
    let end = min((i + 1u) * span, fine);
    return f32(end - i * span);
}

// The distance between the centers across face i along k of level l.
fn distanceOf(l: u32, k: u32, i: u32) -> f32 {
    let n = gas.levels[l].size[k];
    var below = 0.0;
    if (i > 0u) {
        below = widthOf(l, k, i - 1u);
    }
    var above = 0.0;
    if (i < n) {
        above = widthOf(l, k, i);
    }
    return 0.5 * (below + above);
}

// The open area of a face across k of the gas's own cells: 0 on a closed
// wall, else 1.
fn fineArea(k: u32, at: vec3<u32>) -> f32 {
    let n = sizeOf();
    let walls = gas.walls.x;
    let low = at[k] == 0u;
    let high = at[k] == n[k];
    let closed = (low && (walls & (1u << (2u * k))) != 0u) ||
                 (high && (walls & (2u << (2u * k))) != 0u);
    return select(1.0, 0.0, closed);
}

// The open area of a face across k of level l > 0: its finer faces' on
// its plane within the box.
fn coarseArea(l: u32, k: u32, at: vec3<u32>) -> f32 {
    let fine = gas.levels[l - 1u].size.xyz;
    let a = (k + 1u) % 3u;
    let b = (k + 2u) % 3u;
    var child: vec3<u32>;
    child[k] = select(fine[k], 2u * at[k], 2u * at[k] < fine[k]);
    var sum = 0.0;
    for (var j = 0u; j < 2u; j++) {
        for (var i = 0u; i < 2u; i++) {
            child[a] = 2u * at[a] + i;
            child[b] = 2u * at[b] + j;
            if (child[a] < fine[a] && child[b] < fine[b]) {
                sum += solve[gas.levels[l - 1u].areas[k] + indexOf(pointsOf(fine, k), child)];
            } else {
                sum += 0.0;
            }
        }
    }
    return sum;
}

// Each face of level root.level's open area.
@compute @workgroup_size(64, 1, 1)
fn gasAreas(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let l = root.level;
    let n = gas.levels[l].size.xyz;
    let face = faceAxis(n, point);
    if (face.x == 3u) {
        return;
    }
    let k = face.x;
    let at = pointOf(pointsOf(n, k), face.y);
    var area: f32;
    if (l == 0u) {
        area = fineArea(k, at);
    } else {
        area = coarseArea(l, k, at);
    }
    solve[gas.levels[l].areas[k] + face.y] = area;
}

// Each face of level root.level's coefficient (its open area over the
// distance between the centers), then each cell's sum of its faces'.
@compute @workgroup_size(64, 1, 1)
fn gasCoefficients(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let l = root.level;
    let level = gas.levels[l];
    let n = level.size.xyz;
    let faces = allFaces(n);
    if (point < faces) {
        let face = faceAxis(n, point);
        let k = face.x;
        let at = pointOf(pointsOf(n, k), face.y);
        solve[level.coefficients[k] + face.y] =
            solve[level.areas[k] + face.y] / distanceOf(l, k, at[k]);
        return;
    }
    let i = point - faces;
    if (i >= cellCount(n)) {
        return;
    }
    let at = pointOf(n, i);
    let stride = vec3<u32>(1u, n.x, n.x * n.y);
    var weight = 0.0;
    for (var k = 0u; k < 3u; k++) {
        if (n[k] == 1u) {
            continue;
        }
        let low = indexOf(pointsOf(n, k), at);
        let below = solve[level.areas[k] + low] / distanceOf(l, k, at[k]);
        let above = solve[level.areas[k] + low + stride[k]] / distanceOf(l, k, at[k] + 1u);
        weight += below + above;
    }
    solve[level.at.w + i] = weight;
}

// The sources' rates over the substep into each cell they hold, in their
// order.
@compute @workgroup_size(64, 1, 1)
fn gasPourCells(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = sizeOf();
    if (point >= cellCount(n)) {
        return;
    }
    let step = gas.substeps[root.substep];
    if (step.span.z == 0.0) {
        return;
    }
    let h = step.span.x;
    let at = pointOf(n, point);
    let local = localOf(3u, at);
    for (var s = 0u; s < gas.walls.z; s++) {
        let source = gas.sources[s];
        if (!reaches(source, 3u, at) || !holds(source, local)) {
            continue;
        }
        for (var f = 0u; f < 3u; f++) {
            let i = arrayAt(root.now, 3u, f) + point;
            fields[i] += source.rates[f] * h;
        }
    }
}

// The buoyant factor at cell i of buffer b.
fn lift(b: u32, i: u32) -> f32 {
    return gas.lattice.z * fields[arrayAt(b, 3u, 0u) + i] -
           gas.lattice.w * fields[arrayAt(b, 3u, 1u) + i];
}

// Each face: the sources' velocities (the last that holds it), then
// buoyancy over the substep.
@compute @workgroup_size(64, 1, 1)
fn gasFaces(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = sizeOf();
    let face = faceAxis(n, point);
    if (face.x == 3u) {
        return;
    }
    let k = face.x;
    let at = pointOf(pointsOf(n, k), face.y);
    let step = gas.substeps[root.substep];
    let i = arrayAt(root.now, k, 0u) + face.y;
    var v = fields[i];
    if (step.span.z != 0.0) {
        let local = localOf(k, at);
        for (var s = 0u; s < gas.walls.z; s++) {
            let source = gas.sources[s];
            if (source.velocity.w != 0.0 && reaches(source, k, at) && holds(source, local)) {
                v = source.velocity[k];
            }
        }
    }
    let lifts = gas.lattice.z != 0.0 || gas.lattice.w != 0.0;
    if (lifts && gas.gravity[k] != 0.0) {
        let stride = vec3<u32>(1u, n.x, n.x * n.y);
        let cell = at.z * stride.z + at.y * stride.y + at.x;
        let low = at[k] > 0u;
        let high = at[k] < n[k];
        var below = 0.0;
        if (low) {
            below = lift(root.now, cell - stride[k]);
        }
        var above = 0.0;
        if (high) {
            above = lift(root.now, cell);
        }
        var sum = below + above;
        if (low && high) {
            sum = 0.5 * sum;
        }
        let scale = step.span.x * gas.gravity[k];
        v += scale * sum;
    }
    fields[i] = v;
}

// Each face carried semi-Lagrangian from the current buffer into the
// other, times the velocity's keep.
@compute @workgroup_size(64, 1, 1)
fn gasCarry(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = sizeOf();
    let face = faceAxis(n, point);
    if (face.x == 3u || (face.x == 2u && n.z == 1u)) {
        return;
    }
    let k = face.x;
    let at = pointOf(pointsOf(n, k), face.y);
    let step = gas.substeps[root.substep];
    let back = traceBack(root.now, placeOf(k, at), step.span.x);
    let value = sampleAt(arrayAt(root.now, k, 0u), k, back);
    fields[arrayAt(1u - root.now, k, 0u) + face.y] = step.span.y * value;
}

// Each cell's scalars carried semi-Lagrangian from the current buffer
// into the other, each times its keep, one trace for all.
@compute @workgroup_size(64, 1, 1)
fn gasCarryCells(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = sizeOf();
    if (point >= cellCount(n)) {
        return;
    }
    let step = gas.substeps[root.substep];
    let at = pointOf(n, point);
    let back = traceBack(root.now, placeOf(3u, at), step.span.x);
    for (var f = 0u; f < 3u; f++) {
        let value = sampleAt(arrayAt(root.now, 3u, f), 3u, back);
        fields[arrayAt(1u - root.now, 3u, f) + point] = step.keeps[f] * value;
    }
}

// Whether face index f across k at at lies on a closed wall (an axis of
// one cell's all do).
fn isClosed(k: u32, at: vec3<u32>) -> bool {
    let n = sizeOf();
    let walls = gas.walls.x;
    return n[k] == 1u || (at[k] == 0u && (walls & (1u << (2u * k))) != 0u) ||
           (at[k] == n[k] && (walls & (2u << (2u * k))) != 0u);
}

// A face's velocity across k in buffer b with the walls closed.
fn walledFace(b: u32, k: u32, at: vec3<u32>) -> f32 {
    if (isClosed(k, at)) {
        return 0.0;
    }
    return fields[arrayAt(b, k, 0u) + indexOf(pointsOf(sizeOf(), k), at)];
}

// Each open cell's divergence (the core's after closing the walls) into
// the first level's right-hand side; 0 for a cell nothing passes into.
@compute @workgroup_size(64, 1, 1)
fn gasDivergence(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = sizeOf();
    if (point >= cellCount(n)) {
        return;
    }
    let level = gas.levels[0];
    let at = pointOf(n, point);
    let stride = vec3<u32>(1u, n.x, n.x * n.y);
    var out = 0.0;
    var open = false;
    for (var k = 0u; k < 3u; k++) {
        var up = at;
        up[k] += 1u;
        if (n[k] > 1u) {
            out += walledFace(root.now, k, up) - walledFace(root.now, k, at);
        } else {
            out += 0.0;
        }
        let low = indexOf(pointsOf(n, k), at);
        open = open || (n[k] > 1u && (solve[level.areas[k] + low] > 0.0 ||
                                      solve[level.areas[k] + low + stride[k]] > 0.0));
    }
    solve[level.at.y + point] = select(0.0, out, open);
}

// A level's pressure's neighbors about cell at, weighted by their faces'
// coefficients, in the core's order (Neighbors).
fn neighbors(level: Level, origin: u32, at: vec3<u32>, i: u32) -> f32 {
    let n = level.size.xyz;
    let stride = vec3<u32>(1u, n.x, n.x * n.y);
    var sum = 0.0;
    for (var k = 0u; k < 3u; k++) {
        if (n[k] == 1u) {
            continue;
        }
        let low = indexOf(pointsOf(n, k), at);
        if (at[k] > 0u) {
            sum += solve[level.coefficients[k] + low] * solve[origin + i - stride[k]];
        } else {
            sum += 0.0;
        }
        if (at[k] + 1u < n[k]) {
            sum += solve[level.coefficients[k] + low + stride[k]] * solve[origin + i + stride[k]];
        } else {
            sum += 0.0;
        }
    }
    return sum;
}

// One cell's weighted Jacobi on a level, from one buffer into the other.
fn jacobiAt(level: Level, origin: u32, to: u32, i: u32) {
    let at = pointOf(level.size.xyz, i);
    let sums = neighbors(level, origin, at, i);
    let weight = solve[level.at.w + i];
    var solved = 0.0;
    if (weight > 0.0) {
        solved = (sums - solve[level.at.y + i]) / weight;
    }
    let omega = gas.lattice.y;
    solve[to + i] = (1.0 - omega) * solve[origin + i] + omega * solved;
}

// A sweep of level root.level, from its pressure into its scratch (word
// 0) or back (word 1).
@compute @workgroup_size(64, 1, 1)
fn gasJacobi(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let level = gas.levels[root.level];
    if (point >= level.size.w) {
        return;
    }
    let origin = select(level.at.x, level.at.z, root.word == 1u);
    let to = select(level.at.z, level.at.x, root.word == 1u);
    jacobiAt(level, origin, to, point);
}

// A fine cell's residual, rhs - (weighted sum - weight p).
fn residualAt(level: Level, i: u32) -> f32 {
    let at = pointOf(level.size.xyz, i);
    let sums = neighbors(level, level.at.x, at, i);
    return solve[level.at.y + i] - (sums - solve[level.at.w + i] * solve[level.at.x + i]);
}

// Coarse cell c of level l + 1: its right-hand side the sum of its
// children's residuals in the fine cells' order (Z, then Y, then X), its
// pressure 0.
fn restrictAt(l: u32, c: u32) {
    let fine = gas.levels[l];
    let coarse = gas.levels[l + 1u];
    let f = fine.size.xyz;
    let at = pointOf(coarse.size.xyz, c);
    var sum = 0.0;
    for (var z = 2u * at.z; z < 2u * at.z + 2u && z < f.z; z++) {
        for (var y = 2u * at.y; y < 2u * at.y + 2u && y < f.y; y++) {
            for (var x = 2u * at.x; x < 2u * at.x + 2u && x < f.x; x++) {
                sum += residualAt(fine, (z * f.y + y) * f.x + x);
            }
        }
    }
    solve[coarse.at.y + c] = sum;
    solve[coarse.at.x + c] = 0.0;
}

@compute @workgroup_size(64, 1, 1)
fn gasRestrict(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    if (point < gas.levels[root.level + 1u].size.w) {
        restrictAt(root.level, point);
    }
}

// Along one axis, a fine cell's parent and the parent beside it toward
// the cell (held within the coarse cells), and their weights: 3/4 and
// 1/4, linear between the coarse centers.
struct Parents {
    at: vec2<u32>,
    weight: vec2<f32>,
}

fn parentsOf(i: u32, coarse: u32, fine: u32) -> Parents {
    let at = i / 2u;
    var beside = i32(at) - 1;
    if (i % 2u == 1u) {
        beside = i32(at) + 1;
    }
    let held = u32(clamp(beside, 0, i32(coarse) - 1));
    return Parents(vec2<u32>(at, held),
                   vec2<f32>(select(1.0, 0.75, fine > 1u), select(0.0, 0.25, fine > 1u)));
}

// Fine cell i of level l: the coarse level's pressure, interpolated,
// added to its own.
fn prolongAt(l: u32, i: u32) {
    let fine = gas.levels[l];
    let coarse = gas.levels[l + 1u];
    let f = fine.size.xyz;
    let c = coarse.size.xyz;
    let at = pointOf(f, i);
    let px = parentsOf(at.x, c.x, f.x);
    let py = parentsOf(at.y, c.y, f.y);
    let pz = parentsOf(at.z, c.z, f.z);
    var ax = px.at;
    var ay = py.at;
    var az = pz.at;
    var wx = px.weight;
    var wy = py.weight;
    var wz = pz.weight;
    var sum = 0.0;
    for (var corner = 0u; corner < 8u; corner++) {
        let a = corner & 1u;
        let b = (corner >> 1u) & 1u;
        let d = corner >> 2u;
        let origin = coarse.at.x + (az[d] * c.y + ay[b]) * c.x + ax[a];
        sum += wx[a] * wy[b] * wz[d] * solve[origin];
    }
    solve[fine.at.x + i] += sum;
}

@compute @workgroup_size(64, 1, 1)
fn gasProlong(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    if (point < gas.levels[root.level].size.w) {
        prolongAt(root.level, point);
    }
}

fn tailWait() {
    storageBarrier();
    workgroupBarrier();
}

// passes sweeps of level l in one workgroup.
fn tailSmooth(t: u32, l: u32, passes: u32) {
    let level = gas.levels[l];
    for (var s = 0u; s < passes; s += 2u) {
        for (var i = t; i < level.size.w; i += TAIL_GROUP) {
            jacobiAt(level, level.at.x, level.at.z, i);
        }
        tailWait();
        for (var i = t; i < level.size.w; i += TAIL_GROUP) {
            jacobiAt(level, level.at.z, level.at.x, i);
        }
        tailWait();
    }
}

// The V-cycle from level root.level down, in one workgroup: the levels
// of at most a few hundred cells, whose passes could not fill a device.
@compute @workgroup_size(256, 1, 1)
fn gasTail(@builtin(local_invocation_index) t: u32) {
    let last = gas.size.w - 1u;
    for (var l = root.level; l < last; l++) {
        tailSmooth(t, l, SMOOTH_PASSES);
        for (var c = t; c < gas.levels[l + 1u].size.w; c += TAIL_GROUP) {
            restrictAt(l, c);
        }
        tailWait();
    }
    tailSmooth(t, last, COARSEST_PASSES);
    for (var l = last; l > root.level; l--) {
        for (var i = t; i < gas.levels[l - 1u].size.w; i += TAIL_GROUP) {
            prolongAt(l - 1u, i);
        }
        tailWait();
        tailSmooth(t, l - 1u, SMOOTH_PASSES);
    }
}

// Each face: closed on a wall; with a solve, an open face's velocity less
// the pressure's rise across it over the distance between the centers
// (across an open wall the pressure is 0 on the wall, half a cell away).
@compute @workgroup_size(64, 1, 1)
fn gasCorrect(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = sizeOf();
    let face = faceAxis(n, point);
    if (face.x == 3u) {
        return;
    }
    let k = face.x;
    let at = pointOf(pointsOf(n, k), face.y);
    let i = arrayAt(root.now, k, 0u) + face.y;
    if (isClosed(k, at)) {
        fields[i] = 0.0;
        return;
    }
    let level = gas.levels[0];
    if (gas.walls.y == 0u || solve[level.areas[k] + face.y] == 0.0) {
        return;
    }
    let stride = vec3<u32>(1u, n.x, n.x * n.y);
    let cell = at.z * stride.z + at.y * stride.y + at.x;
    let low = at[k] == 0u;
    let high = at[k] == n[k];
    var before = 0.0;
    if (!low) {
        before = solve[level.at.x + cell - stride[k]];
    }
    var after = 0.0;
    if (!high) {
        after = solve[level.at.x + cell];
    }
    fields[i] = fields[i] - select(1.0, 2.0, low || high) * (after - before);
}
