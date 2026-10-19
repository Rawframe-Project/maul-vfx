// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's ribbons: its live particles chained by ribbon id, each
// chain of two or more points written into a buffer of the host's as the
// CPU path's mvfxInstance_WriteRibbons writes them, with the list of the
// chains written and the counts. ribbonKeys gives each live particle its
// id as a key for sort.wgsl's four radix passes, stable, so a chain's
// particles keep the pool's order (oldest first) and chains come in the
// ids' order; ribbonChains, in one workgroup, finds the chains, those
// written (the first that fit whole, as the CPU path stops at the first
// that does not), each point's distance walked along its chain and each
// chain's length; ribbonPoints writes a record a point.

// include: shared/record_fields.wgsl

// The ribbon fields of a layout (mvfxField).
const FIELD_TANGENT: u32 = 11u;
const FIELD_WIDTH: u32 = 12u;
const FIELD_LENGTH: u32 = 13u;
const FIELD_ALONG: u32 = 14u;
const FIELD_RIBBON: u32 = 15u;
const FIELD_SIDE: u32 = 16u;
const FIELD_FACE_NORMAL: u32 = 17u;
const FIELD_CORNER: u32 = 18u;

// The texture coordinate along (mvfxRibbonTexture) and the facing
// (mvfxRibbonFacing) the kernel tells apart.
const TEXTURE_PER_SEGMENT: u32 = 1u;
const TEXTURE_BY_DISTANCE: u32 = 2u;
const FACE_NORMAL: u32 = 1u;

struct Ribbons {
    // The ribbon id's and the distance's first words among a particle's
    // (NONE for none), the texture coordinate along, the facing.
    words: vec4<u32>,
    // The tile length, the scroll's offset at the clock (rounded from the
    // core's binary64 product), the width's value, unused.
    values: vec4<f32>,
    // The normal it faces across in the particles' space, unused.
    normal: vec4<f32>,
    // The width curve's first word and the colour gradient's in the grids
    // (NONE for none), the list's capacity in ribbons, the counts' first
    // word in the count buffer.
    tables: vec4<u32>,
    // The most pieces a segment is cut into, whether the bend decides
    // them, unused.
    cut: vec4<u32>,
    // The cosine of the turn k + 1 pieces allow, at bends[k / 4][k % 4].
    bends: array<vec4<f32>, 4>,
}

// The list of the ribbons written, four words each (mvfxRibbon: its first
// point, two words, its points, its id), from word shape.w of the
// records' numbers (unused by a record pass).
@group(0) @binding(8) var<storage, read_write> ribbonList: array<u32>;
@group(0) @binding(9) var<uniform> ribbon: Ribbons;

// What ribbonPieces and ribbonChains keep for ribbonPoints in the sort
// buffer after the sort's two sets (sortSpan() words each array): each
// element's chain, distance walked along it to its point and its first
// point in the chain; each chain's first element, first record (NONE for
// one not written), length, place in the list and points; each
// element's segment to the next point's pieces and length (ribbonPieces',
// for ribbons of more pieces than one).
const SCRATCH_CHAIN: u32 = 0u;
const SCRATCH_WALKED: u32 = 1u;
const SCRATCH_START: u32 = 2u;
const SCRATCH_FIRST: u32 = 3u;
const SCRATCH_LENGTH: u32 = 4u;
const SCRATCH_RIBBON: u32 = 5u;
const SCRATCH_COUNT: u32 = 6u;
const SCRATCH_POINT: u32 = 7u;
const SCRATCH_PIECES: u32 = 8u;
const SCRATCH_SPAN: u32 = 9u;

// The ribbons' counts in the count buffer: the points and ribbons there
// are, those written.
const COUNT_POINTS: u32 = 0u;
const COUNT_RIBBONS: u32 = 1u;
const COUNT_POINTS_WRITTEN: u32 = 2u;
const COUNT_RIBBONS_WRITTEN: u32 = 3u;

var<workgroup> runScan: array<u32, CHUNK>;
var<workgroup> runSums: array<f32, CHUNK>;
var<workgroup> chainTotal: u32;

fn sortSpan() -> u32 {
    return (params.head.x + BLOCK - 1u) / BLOCK * BLOCK;
}

fn scratchWord(array: u32, k: u32) -> u32 {
    return (4u + array) * sortSpan() + k;
}

// Element e of the sorted chain order: its key (the id) and particle.
fn keyAt(e: u32) -> u32 {
    return sortWords[e];
}

fn particleOf(e: u32) -> u32 {
    return sortWords[sortSpan() + e];
}

fn isHead(e: u32) -> bool {
    return e == 0u || keyAt(e) != keyAt(e - 1u);
}

// Adds v over the workgroup: each invocation's sum of those before it.
fn exclusiveSum(local: u32, v: u32) -> u32 {
    runScan[local] = v;
    workgroupBarrier();
    for (var offset = 1u; offset < CHUNK; offset <<= 1u) {
        var add = 0u;
        if (local >= offset) {
            add = runScan[local - offset];
        }
        workgroupBarrier();
        runScan[local] += add;
        workgroupBarrier();
    }
    let before = runScan[local] - v;
    workgroupBarrier();
    return before;
}

// A segmented sum over the workgroup: each invocation's sum of the values
// after the latest flagged one before it (its own excluded), runs
// combining as a run with a flag starting the sum afresh.
fn segmentedBefore(local: u32, flagged: bool, sum: f32) -> f32 {
    runScan[local] = select(0u, 1u, flagged);
    runSums[local] = sum;
    workgroupBarrier();
    for (var offset = 1u; offset < CHUNK; offset <<= 1u) {
        var flag = 0u;
        var add = 0.0;
        if (local >= offset) {
            flag = runScan[local - offset];
            add = runSums[local - offset];
        }
        workgroupBarrier();
        if (runScan[local] == 0u) {
            runSums[local] += add;
            runScan[local] = flag;
        }
        workgroupBarrier();
    }
    var before = 0.0;
    if (local > 0u) {
        before = runSums[local - 1u];
    }
    workgroupBarrier();
    return before;
}

// Ribbon keys: each live particle's id and index in the first set, a block
// a workgroup, and each block's counts of the lowest digit, as keys.
@compute @workgroup_size(256, 1, 1)
fn ribbonKeys(@builtin(local_invocation_index) local: u32,
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
                let key = wordAt(ribbon.words.x, i);
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

// Ribbon chains, in one workgroup: each invocation walks runs of the
// elements (then of the chains), the runs joined by scans.
@compute @workgroup_size(256, 1, 1)
fn ribbonChains(@builtin(local_invocation_index) local: u32) {
    if (local == 0u) {
        liveCount = statistics[0];
    }
    let live = workgroupUniformLoad(&liveCount);
    findChains(local, live);
    storageBarrier();
    walkChains(local, live);
    storageBarrier();
    let chains = workgroupUniformLoad(&chainTotal);
    takeChains(local, live, chains);
}

// Each element's chain and each chain's first element; their count in
// chainTotal.
fn findChains(local: u32, live: u32) {
    let per = (live + CHUNK - 1u) / CHUNK;
    let lo = min(local * per, live);
    let hi = min(lo + per, live);
    var heads = 0u;
    for (var e = lo; e < hi; e++) {
        heads += select(0u, 1u, isHead(e));
    }
    // The chain of the run's first elements, before any head in it.
    var chain = exclusiveSum(local, heads) - 1u;
    for (var e = lo; e < hi; e++) {
        if (isHead(e)) {
            chain++;
            sortWords[scratchWord(SCRATCH_START, chain)] = e;
        }
        sortWords[scratchWord(SCRATCH_CHAIN, e)] = chain;
    }
    if (local == CHUNK - 1u) {
        chainTotal = chain + 1u;
    }
}

// Each chain's points (those of one point dropped, as the CPU path drops
// them), its place in the list and first record where it is written: the
// chains kept in order while they fit whole, the list and the counts.
fn takeChains(local: u32, live: u32, chains: u32) {
    let per = (chains + CHUNK - 1u) / CHUNK;
    let lo = min(local * per, chains);
    let hi = min(lo + per, chains);
    var kept = 0u;
    var points = 0u;
    for (var c = lo; c < hi; c++) {
        let n = chainPoints(c);
        kept += select(0u, 1u, n >= 2u);
        points += select(0u, n, n >= 2u);
    }
    var place = exclusiveSum(local, kept);
    var first = exclusiveSum(local, points);
    let capacity = params.head.y;
    let listCapacity = ribbon.tables.z;
    var written = 0u;
    var writtenPoints = 0u;
    for (var c = lo; c < hi; c++) {
        let n = chainPoints(c);
        let fits = n >= 2u && place < listCapacity && first + n <= capacity;
        sortWords[scratchWord(SCRATCH_FIRST, c)] = select(NONE, first, fits);
        sortWords[scratchWord(SCRATCH_RIBBON, c)] = select(NONE, place, fits);
        if (fits) {
            let at = params.shape.w + 4u * place;
            ribbonList[at] = first;
            ribbonList[at + 1u] = 0u;
            ribbonList[at + 2u] = n;
            ribbonList[at + 3u] = keyAt(sortWords[scratchWord(SCRATCH_START, c)]);
            written++;
            writtenPoints += n;
        }
        if (n >= 2u) {
            place++;
            first += n;
        }
    }
    let writtenBefore = exclusiveSum(local, written);
    let writtenPointsBefore = exclusiveSum(local, writtenPoints);
    if (local == CHUNK - 1u) {
        let counts = ribbon.tables.w;
        count[counts + COUNT_POINTS] = first;
        count[counts + COUNT_RIBBONS] = place;
        count[counts + COUNT_POINTS_WRITTEN] = writtenPointsBefore + writtenPoints;
        count[counts + COUNT_RIBBONS_WRITTEN] = writtenBefore + written;
    }
}

// Chain c's points: those of its segments and its last particle's (one
// for a chain of one particle).
fn chainPoints(c: u32) -> u32 {
    return sortWords[scratchWord(SCRATCH_COUNT, c)];
}

// The world offset from particle a's place to particle b's: their
// difference taken in the particles' space, where near places subtract
// exactly, then turned and scaled into the world's.
fn offsetBetween(a: u32, b: u32) -> vec3<f32> {
    let d = vectorAt(params.words.place, b) - vectorAt(params.words.place, a);
    return rotate(params.turn, d) / params.scales.x;
}

// Whether element e is its chain's last.
fn isLast(e: u32, live: u32) -> bool {
    return e + 1u == live || isHead(e + 1u);
}

// The pieces and world length of element e's segment to the next point
// of its chain: one piece, the distance to it, where no pass cut them.
fn piecesOf(e: u32) -> u32 {
    if (ribbon.cut.x <= 1u) {
        return 1u;
    }
    return sortWords[scratchWord(SCRATCH_PIECES, e)];
}

fn spanOf(e: u32) -> f32 {
    if (ribbon.cut.x <= 1u) {
        let d = offsetBetween(particleOf(e), particleOf(e + 1u));
        return sqrt(dot(d, d));
    }
    return bitcast<f32>(sortWords[scratchWord(SCRATCH_SPAN, e)]);
}

// Each element's distance walked along its chain and first point in it,
// and each chain's length and points at its last element, by segmented
// sums over the runs (the points' counts exact in binary32).
fn walkChains(local: u32, live: u32) {
    let per = (live + CHUNK - 1u) / CHUNK;
    let lo = min(local * per, live);
    let hi = min(lo + per, live);
    var flagged = false;
    var tail = 0.0;
    var tailPoints = 0.0;
    for (var e = lo; e < hi; e++) {
        if (isHead(e)) {
            flagged = true;
            tail = 0.0;
            tailPoints = 0.0;
        } else {
            tail += spanOf(e - 1u);
            tailPoints += f32(piecesOf(e - 1u));
        }
    }
    var walked = segmentedBefore(local, flagged, tail);
    var point = u32(segmentedBefore(local, flagged, tailPoints));
    for (var e = lo; e < hi; e++) {
        if (isHead(e)) {
            walked = 0.0;
            point = 0u;
        } else {
            walked += spanOf(e - 1u);
            point += piecesOf(e - 1u);
        }
        sortWords[scratchWord(SCRATCH_WALKED, e)] = bitcast<u32>(walked);
        sortWords[scratchWord(SCRATCH_POINT, e)] = point;
        if (isLast(e, live)) {
            let chain = sortWords[scratchWord(SCRATCH_CHAIN, e)];
            sortWords[scratchWord(SCRATCH_LENGTH, chain)] = bitcast<u32>(walked);
            sortWords[scratchWord(SCRATCH_COUNT, chain)] = point + 1u;
        }
    }
}

// Particle j's place less particle i's, in the particles' space, where
// near places subtract exactly.
fn placeLess(j: u32, i: u32) -> vec3<f32> {
    return vectorAt(params.words.place, j) - vectorAt(params.words.place, i);
}

// The four places about element e's segment (e - 1 to e + 2) less e's
// particle's, in the particles' space: the spline's control points, past
// either end of the chain the end's mirrored through it from its
// neighbour (the core's PlaceOf).
fn controlsOf(e: u32, live: u32) -> array<vec3<f32>, 4> {
    let i = particleOf(e);
    var p: array<vec3<f32>, 4>;
    p[1] = vec3<f32>(0.0);
    p[2] = placeLess(particleOf(e + 1u), i);
    if (isHead(e)) {
        p[0] = -p[2];
    } else {
        p[0] = placeLess(particleOf(e - 1u), i);
    }
    if (isLast(e + 1u, live)) {
        p[3] = 2.0 * p[2];
    } else {
        p[3] = placeLess(particleOf(e + 2u), i);
    }
    return p;
}

// The centripetal Catmull-Rom spline's place a fraction f along the
// segment from p[1] to p[2] (Barry and Goldman's pyramid with knots at
// square roots of distance, the scale's square root common to all and
// cancelled); the straight line where two of its points meet.
fn splineAt(p: array<vec3<f32>, 4>, f: f32) -> vec3<f32> {
    var t: array<f32, 4>;
    t[0] = 0.0;
    for (var k = 1; k < 4; k++) {
        let d = p[k] - p[k - 1];
        t[k] = t[k - 1] + sqrt(sqrt(dot(d, d)));
    }
    if (t[1] == t[0] || t[2] == t[1] || t[3] == t[2]) {
        return p[1] + (p[2] - p[1]) * f;
    }
    let u = t[1] + (t[2] - t[1]) * f;
    let a1 = ((t[1] - u) * p[0] + (u - t[0]) * p[1]) / (t[1] - t[0]);
    let a2 = ((t[2] - u) * p[1] + (u - t[1]) * p[2]) / (t[2] - t[1]);
    let a3 = ((t[3] - u) * p[2] + (u - t[2]) * p[3]) / (t[3] - t[2]);
    let b1 = ((t[2] - u) * a1 + (u - t[0]) * a2) / (t[2] - t[0]);
    let b2 = ((t[3] - u) * a2 + (u - t[1]) * a3) / (t[3] - t[1]);
    return ((t[2] - u) * b1 + (u - t[1]) * b2) / (t[2] - t[1]);
}

// The pieces a segment's control points cut it into: all of them, or
// with a bend the fewest whose turn each allows the turn from the way
// into it to the way out of the next (the core's PiecesOf; the cosine in
// binary32, where the core rounds its binary64 one).
fn cutOf(p: array<vec3<f32>, 4>) -> u32 {
    let most = ribbon.cut.x;
    if (ribbon.cut.y == 0u) {
        return most;
    }
    let way = p[1] - p[0];
    let out = p[3] - p[2];
    let lengths = sqrt(dot(way, way)) * sqrt(dot(out, out));
    if (lengths == 0.0) {
        return 1u;
    }
    let cosine = dot(way, out) / lengths;
    for (var k = 1u; k < most; k++) {
        if (cosine >= ribbon.bends[(k - 1u) / 4u][(k - 1u) % 4u]) {
            return k;
        }
    }
    return most;
}

// Ribbon pieces, for ribbons of more pieces than one: each element's
// segment to the next point of its chain cut into pieces, its length the
// sum of theirs, the workgroups striding over the elements (0 for a
// chain's last).
@compute @workgroup_size(64, 1, 1)
fn ribbonPieces(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = statistics[0];
    for (var e = id.x; e < live; e += params.shape.y * 64u) {
        var pieces = 0u;
        var span = 0.0;
        if (!isLast(e, live)) {
            let p = controlsOf(e, live);
            pieces = cutOf(p);
            var before = p[1];
            for (var q = 1u; q <= pieces; q++) {
                var here = p[2];
                if (q < pieces) {
                    here = splineAt(p, f32(q) / f32(pieces));
                }
                span += length(here - before);
                before = here;
            }
            span /= params.scales.x;
        }
        sortWords[scratchWord(SCRATCH_PIECES, e)] = pieces;
        sortWords[scratchWord(SCRATCH_SPAN, e)] = bitcast<u32>(span);
    }
}

// A vector made unit length; zero for zero, as the core's Normalize.
fn unitOf(v: vec3<f32>) -> vec3<f32> {
    let length = sqrt(dot(v, v));
    return select(vec3<f32>(0.0), v / length, length > 0.0);
}

// A point of a ribbon: its frame, its share of the length, its coordinate
// along, its width and colour over the length, its place in the list.
struct Point {
    tangent: vec3<f32>,
    side: vec3<f32>,
    face: vec3<f32>,
    length: f32,
    along: f32,
    width: f32,
    color: vec4<f32>,
    ribbon: u32,
}

// Ribbon points: each element's points (its particle's, then its
// segment's pieces; a chain's last its particle's alone) of each chain
// written, the workgroups striding over the elements.
@compute @workgroup_size(64, 1, 1)
fn ribbonPoints(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = statistics[0];
    for (var e = id.x; e < live; e += params.shape.y * 64u) {
        let chain = sortWords[scratchWord(SCRATCH_CHAIN, e)];
        let first = sortWords[scratchWord(SCRATCH_FIRST, chain)];
        if (first == NONE) {
            continue;
        }
        writeSegment(e, live, chain, first);
    }
}

// The place of element e's point before it in its chain (its segment's
// last piece) less e's particle's, in the particles' space.
fn pointBefore(e: u32, live: u32) -> vec3<f32> {
    let i = particleOf(e - 1u);
    let back = placeLess(i, particleOf(e));
    let pieces = piecesOf(e - 1u);
    if (pieces <= 1u) {
        return back;
    }
    return back + splineAt(controlsOf(e - 1u, live), f32(pieces - 1u) / f32(pieces));
}

// Element e's points: its particle's and its segment's pieces, or a
// chain's last particle's; each with its frame from the places before
// and after it, its distance walked along the chain.
fn writeSegment(e: u32, live: u32, chain: u32, first: u32) {
    let i = particleOf(e);
    let last = isLast(e, live);
    let pieces = select(piecesOf(e), 1u, last);
    var p: array<vec3<f32>, 4>;
    if (!last && pieces > 1u) {
        p = controlsOf(e, live);
    }
    var next = particleOf(min(e + 1u, live - 1u));
    if (last) {
        next = i;
    }
    // The places before, at and after the point, less e's particle's.
    var before = vec3<f32>(0.0);
    if (!isHead(e)) {
        before = pointBefore(e, live);
    }
    var here = vec3<f32>(0.0);
    // A chain's last point's after is itself, as the core's walk ends.
    var after = vec3<f32>(0.0);
    if (!last) {
        after = select(placeLess(next, i), splineAt(p, 1.0 / f32(pieces)), pieces > 1u);
    }
    var walked = bitcast<f32>(sortWords[scratchWord(SCRATCH_WALKED, e)]);
    let total = bitcast<f32>(sortWords[scratchWord(SCRATCH_LENGTH, chain)]);
    let n = sortWords[scratchWord(SCRATCH_COUNT, chain)];
    let g = sortWords[scratchWord(SCRATCH_POINT, e)];
    let s = e - sortWords[scratchWord(SCRATCH_START, chain)];
    let origin = placeOf(i);
    for (var q = 0u; q < pieces; q++) {
        if (q > 0u) {
            walked += length(here - before) / params.scales.x;
        }
        let fraction = f32(q) / f32(pieces);
        var point: Point;
        point.length = select(f32(g + q) / f32(n - 1u), walked / total, total > 0.0);
        point.along = alongOf(i, next, s, fraction, point.length);
        point.width = ribbon.values.z;
        if (ribbon.tables.x != NONE) {
            point.width *= curveAt(ribbon.tables.x, point.length);
        }
        point.color = vec4<f32>(1.0);
        if (ribbon.tables.y != NONE) {
            point.color = gradientAt(ribbon.tables.y, point.length);
        }
        point.ribbon = sortWords[scratchWord(SCRATCH_RIBBON, chain)];
        let world = origin + rotate(params.turn, here) / params.scales.x;
        var across = params.eye.xyz - world;
        if (ribbon.words.w == FACE_NORMAL) {
            across = rotate(params.turn, ribbon.normal.xyz) / params.scales.x;
        }
        point.tangent = unitOf(rotate(params.turn, after - before) / params.scales.x);
        point.side = unitOf(cross(point.tangent, across));
        point.face = unitOf(cross(point.side, point.tangent));
        writePoint(first + g + q, i, select(next, i, q == 0u), fraction, world, point);
        before = here;
        here = after;
        if (q + 2u < pieces) {
            after = splineAt(p, f32(q + 2u) / f32(pieces));
        } else if (q + 2u == pieces) {
            after = placeLess(next, i);
        }
    }
}

// A point's texture coordinate along, a fraction of its segment from
// particle i to particle next, segment s of its chain, at a share t of
// its length: t, its segment's place, or its distance over the tile;
// scrolled.
fn alongOf(i: u32, next: u32, s: u32, fraction: f32, t: f32) -> f32 {
    var along = t;
    if (ribbon.words.z == TEXTURE_PER_SEGMENT) {
        along = f32(s) + fraction;
    } else if (ribbon.words.z == TEXTURE_BY_DISTANCE) {
        let own = floatAt(ribbon.words.y, i);
        var ahead = own;
        if (fraction > 0.0) {
            ahead = floatAt(ribbon.words.y, next);
        }
        along = (own + (ahead - own) * fraction) / ribbon.values.x;
    }
    return along + ribbon.values.y;
}

// A field's values of particle i at a ribbon point: its ribbon fields
// the point's, its colour times the ribbon's, the rest the particle's.
fn pointValues(field: vec4<u32>, i: u32, point: Point, words: ptr<function, vec4<u32>>) -> vec4<f32> {
    let age = ageOf(i);
    var values = vec4<f32>(0.0);
    switch field.x {
        case FIELD_TANGENT: {
            values = vec4<f32>(point.tangent, 0.0);
        }
        case FIELD_WIDTH: {
            values.x = sizeOf(i, age).x / params.scales.x * point.width;
        }
        case FIELD_LENGTH: {
            values.x = point.length;
        }
        case FIELD_ALONG: {
            values.x = point.along;
        }
        case FIELD_RIBBON: {
            (*words).x = point.ribbon;
            values.x = f32(point.ribbon);
        }
        case FIELD_SIDE: {
            values = vec4<f32>(point.side, 0.0);
        }
        case FIELD_FACE_NORMAL: {
            values = vec4<f32>(point.face, 0.0);
        }
        case FIELD_CORNER: {
        }
        case FIELD_COLOR: {
            values = valuesOf(field, i, age, words) * point.color;
        }
        default: {
            values = valuesOf(field, i, age, words);
        }
    }
    return values;
}

// Whether a field is a count, the nearer particle's at a piece (the
// core's IsCounted).
fn isCounted(field: u32) -> bool {
    return field == FIELD_FRAME || field == FIELD_SERIAL || field == FIELD_MESH ||
        field == FIELD_RIBBON;
}

// Point record r at a place a fraction of the way from particle a to b:
// its position the place, its counts the nearer particle's, its other
// values blended, an orientation along the shorter way and renormalized
// (the core's mvfxWriteRibbonRecord).
fn writePoint(r: u32, a: u32, b: u32, fraction: f32, place: vec3<f32>, point: Point) {
    let record = params.head.z + r * params.head.w;
    let nearer = select(b, a, fraction < 0.5);
    for (var f = 0u; f < params.shape.x; f++) {
        let field = params.fields[f];
        var words = vec4<u32>(0u);
        var values = vec4<f32>(0.0);
        if (field.x == FIELD_POSITION) {
            values = vec4<f32>(place, 0.0);
        } else if (isCounted(field.x) || fraction == 0.0 || a == b) {
            values = pointValues(field, nearer, point, &words);
        } else {
            values = pointValues(field, a, point, &words);
            let other = pointValues(field, b, point, &words);
            var sign = 1.0;
            if (field.x == FIELD_ORIENTATION && dot(values, other) < 0.0) {
                sign = -1.0;
            }
            values += (sign * other - values) * fraction;
            let square = dot(values, values);
            if (field.x == FIELD_ORIENTATION && square > 0.0) {
                values /= sqrt(square);
            }
        }
        let bytes = formatBytes(field.y);
        let srgb = ((field.z >> 16u) & 1u) != 0u;
        for (var k = 0u; k < (field.z & 0xFFu); k++) {
            let bits = bitsOf(field.y, srgb && k < 3u, values[k], words[k]);
            writeBytes(record, field.w + k * bytes, bytes, bits);
        }
    }
}
