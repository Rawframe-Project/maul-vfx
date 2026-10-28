// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A smoke gas's step, as the core's (src/gas_step.c, src/gas_pressure.c),
// a pass over cells or faces a stage, every value the core's expression
// in its order:
//  - gasAreas and gasCoefficients make each level's open areas, then its
//    faces' coefficients and cells' weights (mvfxGasAreas), once;
//  - each substep: gasPourCells (the sources into the cells), gasFaces
//    (the sources' velocities, buoyancy, the forces), gasCurl and
//    gasConfine (vorticity confinement), gasCarry and gasCarryCells
//    (semi-Lagrangian into the other buffers, or MacCormack's three
//    stages), gasBurn (the fuel burning, its expansion into the work),
//    gasMean (a sealed box's mean expansion), then the projection
//    (gas/solve.wgsl): gasDivergence (less the expansion), a V-cycle's
//    gasJacobi sweeps, gasRestrict (the residual summed over a coarse
//    cell's children), gasProlong, and
//    gasTail running the levels of at most TAIL_CELLS cells in one
//    workgroup; gasCorrect closes the walls and takes the pressure's
//    rise off each open face;
//  - at the step's end gasHolds marks the statistics record when any
//    scalar holds anything.
// The fields live in one buffer (faces then cells, two of each, then the
// work and the expansion's mean, offsets in the numbers), the solve's
// levels in another.

const NONE: u32 = 0xFFFFFFFFu;
const GROUP: u32 = 64u;
// The invocations of a row of a point pass's dispatch: 32,768 workgroups,
// past which the host adds rows.
const ROW: u32 = 2097152u;
const TAIL_GROUP: u32 = 256u;
const SMOOTH_PASSES: u32 = 2u;
const COARSEST_PASSES: u32 = 64u;
const SPHERE: u32 = 0u;
// The stages of a carry (root.word): semi-Lagrangian times the keep;
// MacCormack's semi-Lagrangian, its trace ahead into the work, and its
// correction times the keep.
const CARRY_PLAIN: u32 = 0u;
const CARRY_BACK: u32 = 1u;
const CARRY_AHEAD: u32 = 2u;
const CARRY_CORRECT: u32 = 3u;

// include: shared/gas_numbers.wgsl

// include: shared/gas_lattice.wgsl

// include: shared/pcg4d_hash.wgsl

// include: shared/finite.wgsl

// include: shared/forces.wgsl

// include: shared/colliders.wgsl

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
// The grids its forces read (rhi/src/grids.c).
@group(0) @binding(3) var<storage, read> grids: array<u32>;
// Its statistics record (MVFX_GPU_GAS_STATISTICS_WORDS), whose holds word
// the host's write starts at 0.
@group(0) @binding(4) var<storage, read_write> statistics: array<atomic<u32>>;

// Force n's field among the gas's numbers (shared/forces.wgsl).
fn fieldOf(n: u32) -> Field {
    return gas.fields[n];
}

var<immediate> root: Root;

// Where buffer b's array of axis (3 for a scalar f) starts.
fn arrayAt(b: u32, axis: u32, f: u32) -> u32 {
    if (axis < 3u) {
        return gas.faces[b][axis];
    }
    return gas.cells[b][f];
}

// include: shared/gas_sample.wgsl

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
    let stride = vec3<u32>(1u, n.x, n.x * n.y);
    let cell = at.z * stride.z + at.y * stride.y + at.x;
    var beside = false;
    if (gas.obstacleHead.x > 0u) {
        beside = (!low && solidAt(cell - stride[k]) != 0u) || (!high && solidAt(cell) != 0u);
    }
    return select(1.0, 0.0, closed || beside);
}

// A cell's solid: 1 + the obstacle it lies in, 0 for none.
fn solidAt(i: u32) -> u32 {
    return u32(fields[gas.faces[0].w + i]);
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

// What gasFaces does (root.word): the sources and what follows them; with
// emitters splatting between, the sources alone, then what follows.
const FACES_ALL: u32 = 0u;
const FACES_SOURCES: u32 = 1u;
const FACES_AFTER: u32 = 2u;

// Each face: the sources' velocities (the last that holds it), then
// buoyancy and the forces over the substep.
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
    if (step.span.z != 0.0 && root.word != FACES_AFTER) {
        let local = localOf(k, at);
        for (var s = 0u; s < gas.walls.z; s++) {
            let source = gas.sources[s];
            if (source.velocity.w != 0.0 && reaches(source, k, at) && holds(source, local)) {
                v = source.velocity[k];
            }
        }
    }
    if (root.word == FACES_SOURCES) {
        fields[i] = v;
        return;
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
    if (gas.forceHead.x > 0u) {
        v = pushed(k, localOf(k, at), step, v);
    }
    fields[i] = v;
}

// A face's velocity across k at x after the forces over the substep, as
// the core's Pushed: v e^(-dh) + a / d (1 - e^(-dh)) under a drag d, else
// v + a h.
fn pushed(k: u32, x: vec3<f32>, step: Substep, v: f32) -> f32 {
    var a = vec3<f32>(0.0);
    var drag = 0.0;
    for (var n = 0u; n < gas.forceHead.x; n++) {
        let f = gas.forces[n];
        let weight = falloffAt(f, x);
        if (weight > 0.0) {
            forceAt(n, f, x, step.span.w, weight, &a, &drag);
        }
    }
    let h = step.span.x;
    if (!(drag > 0.0)) {
        return v + a[k] * h;
    }
    let keep = exp(-drag * h);
    return v * keep + a[k] / drag * (1.0 - keep);
}

// The cell-centered velocity's component c at cell at of the current
// buffer: the mean of its two faces across c (0 along a 2D gas's Z).
fn centered(c: u32, at: vec3<u32>) -> f32 {
    let n = sizeOf();
    if (n[c] == 1u) {
        return 0.0;
    }
    let m = pointsOf(n, c);
    let stride = vec3<u32>(1u, m.x, m.x * m.y);
    let low = arrayAt(root.now, c, 0u) + indexOf(m, at);
    return 0.5 * (fields[low] + fields[low + stride[c]]);
}

// The derivative along a of the centered velocity's component c at a
// cell: a central difference, one-sided at the box's edges (0 across a
// 2D gas's Z).
fn derivative(c: u32, a: u32, at: vec3<u32>) -> f32 {
    let n = sizeOf();
    if (n[a] == 1u || n[c] == 1u) {
        return 0.0;
    }
    var low = at;
    var high = at;
    low[a] = select(at[a], at[a] - 1u, at[a] > 0u);
    high[a] = select(at[a], at[a] + 1u, at[a] + 1u < n[a]);
    let span = f32(high[a] - low[a]) * gas.lattice.x;
    return (centered(c, high) - centered(c, low)) / span;
}

// Each cell's curl into the work: a 2D gas's about Z alone, a 3D gas's
// three components one array after another.
@compute @workgroup_size(64, 1, 1)
fn gasCurl(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = sizeOf();
    let cells = cellCount(n);
    if (point >= cells) {
        return;
    }
    let at = pointOf(n, point);
    let work = gas.cells[0].w;
    let about = derivative(1u, 0u, at) - derivative(0u, 1u, at);
    if (n.z == 1u) {
        fields[work + point] = about;
        return;
    }
    fields[work + point] = derivative(2u, 1u, at) - derivative(1u, 2u, at);
    fields[work + cells + point] = derivative(0u, 2u, at) - derivative(2u, 0u, at);
    fields[work + 2u * cells + point] = about;
}

// The curl's components at cell at (a 2D gas's about Z alone).
fn curlAt(at: vec3<u32>) -> vec3<f32> {
    let n = sizeOf();
    let cells = cellCount(n);
    let i = gas.cells[0].w + indexOf(n, at);
    if (n.z == 1u) {
        return vec3<f32>(0.0, 0.0, fields[i]);
    }
    return vec3<f32>(fields[i], fields[i + cells], fields[i + 2u * cells]);
}

// The curl's size at a cell.
fn curlSize(at: vec3<u32>) -> f32 {
    let w = curlAt(at);
    return sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
}

// Vorticity confinement's force at a cell, as the core's ConfineAt:
// epsilon h (N x w), N the curl's size's gradient over the larger of its
// own size and 1e-4 of its scale.
fn confineAt(at: vec3<u32>) -> vec3<f32> {
    let n = sizeOf();
    var gradient = vec3<f32>(0.0);
    var peak = 0.0;
    for (var a = 0u; a < 3u; a++) {
        if (n[a] == 1u) {
            continue;
        }
        var low = at;
        var high = at;
        low[a] = select(at[a], at[a] - 1u, at[a] > 0u);
        high[a] = select(at[a], at[a] + 1u, at[a] + 1u < n[a]);
        let below = curlSize(low);
        let above = curlSize(high);
        peak = max(peak, max(below, above));
        gradient[a] = (above - below) / (f32(high[a] - low[a]) * gas.lattice.x);
    }
    let size = sqrt(gradient[0] * gradient[0] + gradient[1] * gradient[1] +
                    gradient[2] * gradient[2]);
    let w = curlAt(at);
    let least = 1e-4 * peak / gas.lattice.x;
    var scale = 0.0;
    if (peak > 0.0) {
        scale = gas.burn.w * gas.lattice.x / max(size, least);
    }
    return vec3<f32>(scale * (gradient[1] * w[2] - gradient[2] * w[1]),
                     scale * (gradient[2] * w[0] - gradient[0] * w[2]),
                     scale * (gradient[0] * w[1] - gradient[1] * w[0]));
}

// Each face given confinement over the substep: the mean of the cells'
// forces beside it, or a wall face's one.
@compute @workgroup_size(64, 1, 1)
fn gasConfine(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = sizeOf();
    let face = faceAxis(n, point);
    if (face.x == 3u) {
        return;
    }
    let k = face.x;
    let at = pointOf(pointsOf(n, k), face.y);
    var sum = 0.0;
    var count = 0u;
    if (at[k] > 0u) {
        var low = at;
        low[k] -= 1u;
        sum += confineAt(low)[k];
        count += 1u;
    }
    if (at[k] < n[k]) {
        sum += confineAt(at)[k];
        count += 1u;
    }
    if (count == 2u) {
        sum = 0.5 * sum;
    }
    let i = arrayAt(root.now, k, 0u) + face.y;
    fields[i] += gas.substeps[root.substep].span.x * sum;
}

// A lattice's values at place p as sampleAt in all eight corners' order
// (mvfxGasSampleRange), and the least and most of the values it took.
fn sampleRange(start: u32, axis: u32, p: vec3<f32>) -> vec3<f32> {
    let m = pointsOf(sizeOf(), axis);
    var low: vec3<u32>;
    var high: vec3<u32>;
    var t: vec3<f32>;
    for (var k = 0u; k < 3u; k++) {
        let shift = select(0.5, 0.0, k == axis);
        // Held as the core's, a NaN to 0.
        let place = select(0.0, min(p[k] - shift, f32(m[k] - 1u)), p[k] - shift >= 0.0);
        low[k] = u32(place);
        high[k] = select(low[k], low[k] + 1u, low[k] + 1u < m[k]);
        t[k] = place - f32(low[k]);
    }
    var sum = 0.0;
    var least = 0.0;
    var most = 0.0;
    for (var corner = 0u; corner < 8u; corner++) {
        var at: vec3<u32>;
        var weight = 1.0;
        for (var k = 0u; k < 3u; k++) {
            let up = ((corner >> k) & 1u) != 0u;
            at[k] = select(low[k], high[k], up);
            weight *= select(1.0 - t[k], t[k], up);
        }
        let value = fields[start + (at.z * m.y + at.y) * m.x + at.x];
        sum += weight * value;
        least = select(min(least, value), value, corner == 0u);
        most = select(max(most, value), value, corner == 0u);
    }
    return vec3<f32>(sum, least, most);
}

// The point i of an array along axis, traced to back, carried by stage
// root.word from the current buffer's array (starting at origin) into the
// other's (at into), MacCormack's trace ahead into the work's float work
// instead; times keep where the stage takes it.
fn carryPoint(axis: u32, back: vec3<f32>, i: u32, origin: u32, into: u32, work: u32,
              keep: f32) {
    switch root.word {
        case CARRY_PLAIN: {
            fields[into + i] = keep * sampleAt(origin, axis, back);
        }
        case CARRY_BACK: {
            fields[into + i] = sampleAt(origin, axis, back);
        }
        case CARRY_AHEAD: {
            fields[work] = sampleAt(into, axis, back);
        }
        default: {
            // Half the error taken back, the plain value kept where the
            // result leaves the points sampled from.
            let plain = sampleRange(origin, axis, back);
            let sharp = plain.x + 0.5 * (fields[origin + i] - fields[work]);
            let within = sharp >= plain.y && sharp <= plain.z;
            fields[into + i] = keep * select(plain.x, sharp, within);
        }
    }
}

// Where the point at p of a carry's stage traces to through the current
// buffer's velocity: back over the substep, or ahead for MacCormack's
// trace ahead.
fn traced(p: vec3<f32>) -> vec3<f32> {
    let h = gas.substeps[root.substep].span.x;
    return traceBack(root.now, p, select(h, -h, root.word == CARRY_AHEAD));
}

// Each face carried by a stage (root.word) from the current buffer into
// the other, times the velocity's keep.
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
    let back = traced(placeOf(k, at));
    carryPoint(k, back, face.y, arrayAt(root.now, k, 0u), arrayAt(1u - root.now, k, 0u),
               gas.cells[0].w + point, gas.substeps[root.substep].span.y);
}

// Each cell's scalars carried by a stage (root.word) from the current
// buffer into the other, each times its keep, one trace for all.
@compute @workgroup_size(64, 1, 1)
fn gasCarryCells(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = sizeOf();
    let cells = cellCount(n);
    if (point >= cells) {
        return;
    }
    let step = gas.substeps[root.substep];
    let back = traced(placeOf(3u, pointOf(n, point)));
    for (var f = 0u; f < 3u; f++) {
        carryPoint(3u, back, point, arrayAt(root.now, 3u, f), arrayAt(1u - root.now, 3u, f),
                   gas.cells[0].w + f * cells + point, step.keeps[f]);
    }
}

// The fuel burning over the substep in each cell of the current buffer
// hot enough, adding its heat and smoke; the divergence it asks for into
// the work when the burn expands.
@compute @workgroup_size(64, 1, 1)
fn gasBurn(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    if (point >= cellCount(sizeOf())) {
        return;
    }
    let step = gas.substeps[root.substep];
    let density = arrayAt(root.now, 3u, 0u) + point;
    let temperature = arrayAt(root.now, 3u, 1u) + point;
    let fuel = arrayAt(root.now, 3u, 2u) + point;
    let held = fields[fuel];
    var burned = 0.0;
    if (fields[temperature] >= gas.burn.x && held > 0.0) {
        burned = held * step.burn.x;
    }
    fields[fuel] = held - burned;
    fields[temperature] += gas.burn.y * burned;
    fields[density] += gas.burn.z * burned;
    if (gas.modes.y != 0u) {
        fields[gas.cells[0].w + point] = step.burn.y * burned;
    }
}

// include: gas/solve.wgsl

var<workgroup> heldHere: atomic<u32>;

// Whether any scalar of a cell of the current buffer is past nothing, as
// the core's HoldsAny: a workgroup's cells joined, then one mark a
// workgroup into the statistics record's holds word.
@compute @workgroup_size(64, 1, 1)
fn gasHolds(@builtin(global_invocation_id) id: vec3<u32>,
            @builtin(local_invocation_index) lane: u32) {
    // Workgroup memory starts at 0.
    let point = id.x + id.y * ROW;
    let empty = bitcast<f32>(gas.modes.w);
    if (point < cellCount(sizeOf())) {
        var held = false;
        for (var f = 0u; f < 3u; f++) {
            held = held || abs(fields[arrayAt(root.now, 3u, f) + point]) > empty;
        }
        if (held) {
            atomicMax(&heldHere, 1u);
        }
    }
    workgroupBarrier();
    if (lane == 0u && atomicLoad(&heldHere) != 0u) {
        atomicMax(&statistics[0], 1u);
    }
}

// Each cell's solid, as the core's mvfxMarkGasSolids: 1 + the first
// obstacle its center lies on the wrong side of, 0 for none.
@compute @workgroup_size(64, 1, 1)
fn gasMark(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = sizeOf();
    if (point >= cellCount(n)) {
        return;
    }
    let local = localOf(3u, pointOf(n, point));
    var solid = 0u;
    for (var c = 0u; c < gas.obstacleHead.x; c++) {
        if (colliderGap(gas.obstacles[c], local) < 0.0) {
            solid = c + 1u;
            break;
        }
    }
    fields[gas.faces[0].w + point] = f32(solid);
}

// Each solid cell's scalars of the current buffer emptied
// (mvfxEmptyGasSolids).
@compute @workgroup_size(64, 1, 1)
fn gasEmpty(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    if (point >= cellCount(sizeOf()) || solidAt(point) == 0u) {
        return;
    }
    for (var f = 0u; f < 3u; f++) {
        fields[arrayAt(root.now, 3u, f) + point] = 0.0;
    }
}

// Where an obstacle's motion from before to now carried the place p that
// now lies at p, as the core's Before: a plane moved along its normal, a
// sphere moved, any other moved and turned as its frame was.
fn placeBefore(now: Collider, before: Collider, p: vec3<f32>) -> vec3<f32> {
    let d = p - now.center.xyz;
    if (now.modes.x == COLLIDER_PLANE) {
        var along = 0.0;
        for (var k = 0u; k < 3u; k++) {
            along += (now.center[k] - before.center[k]) * now.normal[k];
        }
        return p - along * now.normal.xyz;
    }
    if (now.modes.x == COLLIDER_SPHERE) {
        return before.center.xyz + d;
    }
    let local = rotate(conjugate(now.rotation), d);
    return before.center.xyz + rotate(before.rotation, local);
}

// Each face beside a solid given its obstacle's velocity across it over
// the step (0 for one that did not move), as the core's
// mvfxMoveGasSolids: the solid below it, else the one above.
@compute @workgroup_size(64, 1, 1)
fn gasMove(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = sizeOf();
    let face = faceAxis(n, point);
    if (face.x == 3u) {
        return;
    }
    let k = face.x;
    let at = pointOf(pointsOf(n, k), face.y);
    let stride = vec3<u32>(1u, n.x, n.x * n.y);
    let cell = at.z * stride.z + at.y * stride.y + at.x;
    var below = 0u;
    if (at[k] > 0u) {
        below = solidAt(cell - stride[k]);
    }
    var above = 0u;
    if (at[k] < n[k]) {
        above = solidAt(cell);
    }
    let solid = select(above, below, below != 0u);
    if (solid == 0u) {
        return;
    }
    let c = solid - 1u;
    var v = 0.0;
    if ((gas.obstacleHead.y & (1u << c)) != 0u) {
        let p = localOf(k, at);
        let origin = placeBefore(gas.obstacles[c], gas.before[c], p);
        v = (p[k] - origin[k]) / bitcast<f32>(gas.obstacleHead.z);
    }
    fields[arrayAt(root.now, k, 0u) + face.y] = v;
}
