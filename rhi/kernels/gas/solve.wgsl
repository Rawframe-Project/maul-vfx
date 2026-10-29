// A smoke gas's projection (spliced into gas.wgsl): gasMean,
// gasDivergence, the V-cycle's gasJacobi, gasRestrict, gasProlong and
// gasTail, and gasCorrect.

// A binary32 pair's exact sum and its error (Knuth's two-sum).
fn twoSum(a: f32, b: f32) -> vec2<f32> {
    let s = a + b;
    let part = s - a;
    return vec2<f32>(s, (a - (s - part)) + (b - part));
}

// Two sums held as pairs added, the result a pair again.
fn addPairs(x: vec2<f32>, y: vec2<f32>) -> vec2<f32> {
    let s = twoSum(x.x, y.x);
    return twoSum(s.x, s.y + (x.y + y.y));
}

var<workgroup> sums: array<vec2<f32>, 256>;
var<workgroup> opens: array<u32, 256>;
var<workgroup> airs: array<u32, 256>;

// A sealed box's expansion's mean over its open cells: summed as pairs of
// binary32 in one workgroup, standing for the core's binary64 sum; 0 for
// a liquid holding an open air cell, which holds the pressure (HasAir).
@compute @workgroup_size(256, 1, 1)
fn gasMean(@builtin(local_invocation_index) lane: u32) {
    let n = sizeOf();
    let cells = cellCount(n);
    let work = gas.cells[0].w;
    var sum = vec2<f32>(0.0);
    var open = 0u;
    var air = 0u;
    for (var i = lane; i < cells; i += TAIL_GROUP) {
        if (isOpen(pointOf(n, i))) {
            sum = addPairs(sum, vec2<f32>(fields[work + i], 0.0));
            open += 1u;
            air += select(1u, 0u, solvedAt(gas.levels[0], i));
        }
    }
    sums[lane] = sum;
    opens[lane] = open;
    airs[lane] = air;
    workgroupBarrier();
    for (var half = TAIL_GROUP / 2u; half > 0u; half >>= 1u) {
        if (lane < half) {
            sums[lane] = addPairs(sums[lane], sums[lane + half]);
            opens[lane] += opens[lane + half];
            airs[lane] += airs[lane + half];
        }
        workgroupBarrier();
    }
    if (lane == 0u) {
        let total = sums[0];
        var mean = 0.0;
        if (opens[0] > 0u && airs[0] == 0u) {
            mean = (total.x + total.y) / f32(opens[0]);
        }
        fields[gas.cells[1].w] = mean;
    }
}

// A face's velocity across k in buffer b with the walls closed.
fn walledFace(b: u32, k: u32, at: vec3<u32>) -> f32 {
    if (isClosed(k, at)) {
        return 0.0;
    }
    return fields[arrayAt(b, k, 0u) + indexOf(pointsOf(sizeOf(), k), at)];
}

// Whether a cell of the gas has an open face across an axis of more than
// one cell (IsOpen).
fn isOpen(at: vec3<u32>) -> bool {
    let n = sizeOf();
    let level = gas.levels[0];
    let stride = vec3<u32>(1u, n.x, n.x * n.y);
    var open = false;
    for (var k = 0u; k < 3u; k++) {
        let low = indexOf(pointsOf(n, k), at);
        open = open || (n[k] > 1u && (solve[level.areas[k] + low] > 0.0 ||
                                      solve[level.areas[k] + low + stride[k]] > 0.0));
    }
    return open;
}

// Each open cell's divergence (the core's after closing the walls) less
// the burn's expansion (less its mean in a sealed box) into the first
// level's right-hand side; 0 for a cell nothing passes into.
@compute @workgroup_size(64, 1, 1)
fn gasDivergence(@builtin(global_invocation_id) id: vec3<u32>) {
    let point = id.x + id.y * ROW;
    let n = sizeOf();
    if (point >= cellCount(n)) {
        return;
    }
    let at = pointOf(n, point);
    var out = 0.0;
    for (var k = 0u; k < 3u; k++) {
        var up = at;
        up[k] += 1u;
        if (n[k] > 1u) {
            out += walledFace(root.now, k, up) - walledFace(root.now, k, at);
        } else {
            out += 0.0;
        }
    }
    let open = isOpen(at);
    var rhs = select(0.0, out, open);
    if (gas.modes.y != 0u) {
        var mean = 0.0;
        if (gas.modes.z != 0u) {
            mean = fields[gas.cells[1].w];
        }
        rhs -= select(0.0, fields[gas.cells[0].w + point] - mean, open);
    }
    solve[gas.levels[0].at.y + point] = rhs;
    // A liquid's air cells start at 0: those it left since the last solve
    // are air now.
    let level = gas.levels[0];
    solve[level.at.x + point] = select(0.0, solve[level.at.x + point], solvedAt(level, point));
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

// Whether cell i of a level is solved (IsSolved): every cell of smoke,
// a liquid's liquid cells (its mask at coefficients.w); an air cell's
// pressure is held at 0.
fn solvedAt(level: Level, i: u32) -> bool {
    return level.coefficients.w == NONE || solve[level.coefficients.w + i] != 0.0;
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
    let next = (1.0 - omega) * solve[origin + i] + omega * solved;
    solve[to + i] = select(0.0, next, solvedAt(level, i));
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
    solve[fine.at.x + i] += select(0.0, sum, solvedAt(fine, i));
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
// (across an open wall the pressure is 0 on the wall, half a cell away);
// then held (heldFace).
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
        fields[i] = heldFace(fields[i]);
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
    fields[i] = heldFace(fields[i] - select(1.0, 2.0, low || high) * (after - before));
}
