// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's units' self-collision, as the core's (src/numbers.c), on the
// pool the unit pass steps (the ended units in place, skipped):
//  - selfReach finds the fastest squared speed of the points and keeps
//    their places as the first substep's start;
//  - selfKeys gives each point its cell's key from its place, cells of the
//    reach (the thickness plus twice the farthest a point goes in the step
//    at that speed and gravity), and the blocks' counts of the lowest
//    digit; sort.wgsl's four radix passes order them, stable;
//  - selfGather keeps each point's candidates, the other points within the
//    reach in the 27 cells about it in the grid's order, but those of its
//    unit near it at rest, at most the most (a point with more counted);
//  - each substep, between the unit pass's halves, selfPart gives each
//    point its pairs' mean move from the same places (each touching pair's
//    share of the overlap less its share of the sliding up to friction
//    times the overlap) into scratch, and selfCommit adds the moves and
//    keeps the places as the next substep's start.
// The square roots and the divisions differ from the core's within their
// rounding, so a point near a cell's edge may fall in its neighbour, which
// orders its candidates otherwise (and past the most keeps others).

// include: shared/exact.wgsl

// include: shared/finite.wgsl

// include: shared/grid_cells.wgsl

const NONE: u32 = 0xFFFFFFFFu;
const CHUNK: u32 = 256u;
const ITEMS: u32 = 4u;
const BLOCK: u32 = CHUNK * ITEMS;

// A point's alive word in a unit pass: ended at the step's start (the
// core's units gone before it steps them).
const LIVE_ENDED: u32 = 2u;

// The pins (the unit pass's UNIT_PIN_*) and the let-go flag (the core's
// MVFX_ATTACH_LET_GO).
const PIN_FIRST: u32 = 1u;
const PIN_ROW: u32 = 2u;
const LET_GO: u32 = 8u;

// The statistics record's word counting the points with more candidates
// than the most (MVFX_GPU_SELF_CONTACTS_DROPPED).
const STATISTICS_SELF_CAPPED: u32 = 51u;

// The count buffer's words: the points the grid holds (the sort's
// statistics word), the fastest squared speed's bits.
const COUNT_HELD: u32 = 0u;
const COUNT_FASTEST: u32 = 1u;

struct SelfNumbers {
    // The particle buffer's capacity, the most candidates, the workgroups
    // of the block passes and of the others.
    head: vec4<u32>,
    // A unit's points and its points across, the grid's mask, the attach
    // flags' word (NONE for none).
    unit: vec4<u32>,
    // The places', the velocities' and the alive words, the pins (0 for
    // an emitter without anchors).
    words: vec4<u32>,
    // The thickness, the step's seconds, gravity's travel over them (half
    // its pull times their square), the friction.
    reach: vec4<f32>,
    // The direction across and the spacing.
    across: vec4<f32>,
    // The direction down and the thickness over the def's.
    down: vec4<f32>,
}

// The pass: the substep (selfPart and selfCommit), unused.
struct Root {
    substep: u32,
    unused0: u32,
    unused1: u32,
    unused2: u32,
}

@group(0) @binding(0) var<uniform> numbers: SelfNumbers;
@group(0) @binding(1) var<storage, read_write> particles: array<u32>;
@group(0) @binding(2) var<storage, read_write> statistics: array<atomic<u32>>;
// The substep's start places and the moves (three arrays of the capacity
// each), the candidates' counts (one), the candidates (the most a point).
@group(0) @binding(3) var<storage, read_write> scratch: array<u32>;
@group(0) @binding(4) var<storage, read_write> count: array<atomic<u32>>;
// Keys, then indices, twice over the capacity's blocks; two sets of digit
// counts, 256 a block each, digit-major (the sort's).
@group(0) @binding(5) var<storage, read_write> sortWords: array<u32>;
@group(0) @binding(6) var<storage, read_write> digits: array<u32>;

var<immediate> root: Root;

var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> held: u32;

fn floatAt(first: u32, i: u32) -> f32 {
    return bitcast<f32>(particles[first * numbers.head.x + i]);
}

fn placeOf(i: u32) -> vec3<f32> {
    let first = numbers.words.x;
    return vec3<f32>(floatAt(first, i), floatAt(first + 1u, i), floatAt(first + 2u, i));
}

fn isEnded(i: u32) -> bool {
    return particles[numbers.words.z * numbers.head.x + i] == LIVE_ENDED;
}

// Scratch's arrays: k of the start places (0 to 2) or the moves (3 to
// 5), the counts, the candidates.
fn scratchAt(k: u32, i: u32) -> f32 {
    return bitcast<f32>(scratch[k * numbers.head.x + i]);
}

fn setScratch(k: u32, i: u32, value: f32) {
    scratch[k * numbers.head.x + i] = bitcast<u32>(value);
}

fn countAt(i: u32) -> u32 {
    return 6u * numbers.head.x + i;
}

fn candidateAt(i: u32, n: u32) -> u32 {
    return 7u * numbers.head.x + i * numbers.head.y + n;
}

// The reach, as the core's: the thickness plus twice the farthest a point
// goes at the fastest speed and gravity, the thickness where that is not
// finite.
fn reachOf() -> f32 {
    let fastest = bitcast<f32>(atomicLoad(&count[COUNT_FASTEST]));
    let t = numbers.reach.y;
    let reach = numbers.reach.x + 2.0 * (sqrt(fastest) * t + numbers.reach.z);
    return select(numbers.reach.x, reach, reach < bitcast<f32>(0x7F800000u));
}

// Reach: the points the grid holds, the fastest squared speed of those
// not ended (their bits order as they do; NaN left out, as the core's
// comparison leaves it), and their places as the first substep's start.
@compute @workgroup_size(64, 1, 1)
fn selfReach(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = atomicLoad(&statistics[0]);
    if (id.x == 0u) {
        _ = atomicExchange(&count[COUNT_HELD], live);
    }
    let v = numbers.words.y;
    for (var i = id.x; i < live; i += numbers.head.w * 64u) {
        if (isEnded(i)) {
            continue;
        }
        var square = 0.0;
        for (var k = 0u; k < 3u; k++) {
            let speed = floatAt(v + k, i);
            square += speed * speed;
        }
        if (!isNan(square)) {
            _ = atomicMax(&count[COUNT_FASTEST], bitcast<u32>(square));
        }
        let at = placeOf(i);
        for (var k = 0u; k < 3u; k++) {
            setScratch(k, i, at[k]);
        }
    }
}

// The words of one of the sort's arrays over a capacity.
fn spanOf(capacity: u32) -> u32 {
    return (capacity + BLOCK - 1u) / BLOCK * BLOCK;
}

fn cellAt(at: vec3<f32>, inverse: f32) -> vec3<i32> {
    return vec3<i32>(cellOf(at.x, inverse), cellOf(at.y, inverse), cellOf(at.z, inverse));
}

// Self keys: each point's key and index in the first set (an ended
// point's past every cell's), a block a workgroup at a time, and each
// block's counts of the lowest digit.
@compute @workgroup_size(256, 1, 1)
fn selfKeys(@builtin(local_invocation_index) local: u32,
            @builtin(workgroup_id) group: vec3<u32>) {
    if (local == 0u) {
        held = atomicLoad(&count[COUNT_HELD]);
    }
    let n = workgroupUniformLoad(&held);
    let inverse = divide(1.0, reachOf());
    let blocks = spanOf(numbers.head.x) / BLOCK;
    let span = blocks * BLOCK;
    for (var b = group.x; b < (n + BLOCK - 1u) / BLOCK; b += numbers.head.z) {
        _ = atomicExchange(&tally[local], 0u);
        workgroupBarrier();
        for (var k = 0u; k < ITEMS; k++) {
            let i = b * BLOCK + k * CHUNK + local;
            if (i < n) {
                var key = NONE;
                if (!isEnded(i)) {
                    key = keyOf(cellAt(placeOf(i), inverse), numbers.unit.z);
                }
                sortWords[i] = key;
                sortWords[span + i] = i;
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
        let k = sortWords[m];
        if (k < key || (after && k == key)) {
            a = m + 1u;
        } else {
            b = m;
        }
    }
    return a;
}

// Whether points i and j of one unit are apart by less than the thickness
// at rest, as the core's IsNearAtRest; points of different units never
// are.
fn isNearAtRest(i: u32, j: u32, thickness: f32) -> bool {
    let points = numbers.unit.x;
    let w = numbers.unit.y;
    if (i / points != j / points) {
        return false;
    }
    let a = i % points;
    let b = j % points;
    let across = f32(i32(b % w) - i32(a % w));
    let down = f32(i32(b / w) - i32(a / w));
    var square = 0.0;
    for (var k = 0u; k < 3u; k++) {
        let d = (across * numbers.across[k] + down * numbers.down[k]) * numbers.across.w * numbers.down.w;
        square += d * d;
    }
    return square < thickness * thickness;
}

// Gather: each point's candidates in the grid's order, at most the most,
// those with more counted.
@compute @workgroup_size(64, 1, 1)
fn selfGather(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = atomicLoad(&count[COUNT_HELD]);
    let reach = reachOf();
    let inverse = divide(1.0, reach);
    let thickness = numbers.reach.x;
    let span = spanOf(numbers.head.x);
    let most = numbers.head.y;
    var capped = 0u;
    for (var i = id.x; i < n; i += numbers.head.w * 64u) {
        var kept = 0u;
        var over = false;
        if (!isEnded(i)) {
            let at = placeOf(i);
            let cell = cellAt(at, inverse);
            var visited: array<u32, 27>;
            var visits = 0u;
            for (var m = 0u; m < 27u; m++) {
                let around = cell + vec3<i32>(i32(m % 3u) - 1, i32(m / 3u % 3u) - 1, i32(m / 9u) - 1);
                let key = keyOf(around, numbers.unit.z);
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
                    let other = placeOf(j);
                    var square = 0.0;
                    for (var k = 0u; k < 3u; k++) {
                        let d = at[k] - other[k];
                        square += d * d;
                    }
                    if (j == i || !(square < reach * reach) || isNearAtRest(i, j, thickness)) {
                        continue;
                    }
                    over = over || kept == most;
                    if (kept < most) {
                        scratch[candidateAt(i, kept)] = j;
                        kept++;
                    }
                }
            }
        }
        scratch[countAt(i)] = kept;
        capped += select(0u, 1u, over);
    }
    if (capped > 0u) {
        atomicAdd(&statistics[STATISTICS_SELF_CAPPED], capped);
    }
}

// Point i's inverse mass, as the core's mvfxPointWeight: 0 for a pinned
// point of a unit not let go, else 1.
fn weightOf(i: u32) -> f32 {
    let q = i % numbers.unit.x;
    let pins = numbers.words.w;
    let pinned = ((pins & PIN_FIRST) != 0u && q == 0u) || ((pins & PIN_ROW) != 0u && q < numbers.unit.y);
    let flags = numbers.unit.w;
    let letGo = flags != NONE && (particles[flags * numbers.head.x + i - q] & LET_GO) != 0u;
    return select(1.0, 0.0, pinned && !letGo);
}

// Point i's move from its pair with point j, as the core's PartOf: its
// share of their overlap along their line, less its share of their
// sliding since the substep's start up to friction times the overlap;
// none where they do not touch.
fn partOf(i: u32, j: u32, wi: f32, wj: f32, shift: ptr<function, vec3<f32>>) -> bool {
    let xi = placeOf(i);
    let xj = placeOf(j);
    let d = xi - xj;
    var square = 0.0;
    for (var k = 0u; k < 3u; k++) {
        square += d[k] * d[k];
    }
    let length = sqrt(square);
    let sum = wi + wj;
    let thickness = numbers.reach.x;
    if (!(length < thickness) || !(length > 0.0) || !(sum > 0.0)) {
        return false;
    }
    let overlap = thickness - length;
    // The weights are 0 or 1: their shares are exact.
    let si = select(0.0, select(1.0, 0.5, sum == 2.0), wi > 0.0);
    let sj = select(0.0, select(1.0, 0.5, sum == 2.0), wj > 0.0);
    var line = vec3<f32>(0.0);
    var slide = vec3<f32>(0.0);
    var along = 0.0;
    for (var k = 0u; k < 3u; k++) {
        line[k] = quotient(d[k], length);
        let pi = xi[k] + si * overlap * line[k];
        let pj = xj[k] - sj * overlap * line[k];
        slide[k] = (pi - scratchAt(k, i)) - (pj - scratchAt(k, j));
        along += slide[k] * line[k];
    }
    var tangent = 0.0;
    for (var k = 0u; k < 3u; k++) {
        slide[k] -= along * line[k];
        tangent += slide[k] * slide[k];
    }
    tangent = sqrt(tangent);
    let limit = numbers.reach.w * overlap;
    var share = 0.0;
    if (tangent > 0.0) {
        share = select(quotient(limit, tangent), 1.0, tangent < limit);
    }
    for (var k = 0u; k < 3u; k++) {
        (*shift)[k] = si * overlap * line[k] - si * share * slide[k];
    }
    return true;
}

// Part: each point's move, its touching pairs' mean, into scratch.
@compute @workgroup_size(64, 1, 1)
fn selfPart(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = atomicLoad(&count[COUNT_HELD]);
    for (var i = id.x; i < n; i += numbers.head.w * 64u) {
        if (isEnded(i)) {
            continue;
        }
        let wi = weightOf(i);
        var sum = vec3<f32>(0.0);
        var touching = 0u;
        for (var c = 0u; c < scratch[countAt(i)]; c++) {
            let j = scratch[candidateAt(i, c)];
            var shift = vec3<f32>(0.0);
            if (partOf(i, j, wi, weightOf(j), &shift)) {
                sum += shift;
                touching++;
            }
        }
        for (var k = 0u; k < 3u; k++) {
            var mean = 0.0;
            if (touching > 0u) {
                mean = quotient(sum[k], f32(touching));
            }
            setScratch(3u + k, i, mean);
        }
    }
}

// Commit: each point's place gains its move and is the next substep's
// start.
@compute @workgroup_size(64, 1, 1)
fn selfCommit(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = atomicLoad(&count[COUNT_HELD]);
    let first = numbers.words.x;
    for (var i = id.x; i < n; i += numbers.head.w * 64u) {
        if (isEnded(i)) {
            continue;
        }
        for (var k = 0u; k < 3u; k++) {
            let at = floatAt(first + k, i) + scratchAt(3u + k, i);
            particles[(first + k) * numbers.head.x + i] = bitcast<u32>(at);
            setScratch(k, i, at);
        }
    }
}
