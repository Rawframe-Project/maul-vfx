// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A surface gas's mesh, as the core's mvfxInstance_WriteSurfaceMesh
// (src/surface_mesh.c) writes it: marching cubes (squares in 2D) between
// the cells' centers, every number the CPU walk's (Z, then Y, then X).
// Every variant of the tables names exactly its case's crossing edges, so
// the cube that first names an edge's vertex is the first cube about the
// edge in the walk, and a sample's own vertex (it lies at the level) the
// first cube about a crossing edge from it: each cube knows the vertices
// it makes from the samples alone, and makes them where its primitives
// first name them, its centers too. Five passes over blocks of cubes in
// the walk's order:
//  - meshCount: each block's vertices made and primitives kept;
//  - meshOffsets, one workgroup: each block's first vertex and primitive,
//    the totals, the first refusal emptied;
//  - meshVertices: each cube's vertices numbered, their numbers into the
//    slots (four a sample: its edges' along X, Y and Z, then its own),
//    those below the capacity written;
//  - meshRefuse: the first kept primitive that does not fit both buffers;
//  - meshIndices: the primitives before it written, and the counts.
// A face's choice is the asymptotic decider's sign, which comes from a
// comparison of its two products and its sum in the core's order, so it
// is the core's on every device.

// include: shared/gas_numbers.wgsl

// include: shared/marching_words.wgsl

const NONE: u32 = 0xFFFFFFFFu;
const CHUNK: u32 = 256u;
// A key a cube names a vertex by: its edges, then its centers, then its
// corners' own.
const CENTER_KEY: u32 = 12u;
const SAMPLE_KEY: u32 = 16u;

// A mesh: the buffer current, its axes (3, or 2 for a 2D surface), its
// cubes, its blocks; the vertices' capacity, the indices', the positions'
// and the normals' first words (NONE for none); the indices' first word,
// the counts' (NONE for none), the block passes' workgroups, the slots'
// words; the box's turn into the world, its center less the host's origin
// and its scale; the level.
struct Mesh {
    head: vec4<u32>,
    caps: vec4<u32>,
    out: vec4<u32>,
    rotation: vec4<f32>,
    back: vec4<f32>,
    level: vec4<f32>,
}

@group(0) @binding(0) var<uniform> gas: Gas;
@group(0) @binding(1) var<storage, read> fields: array<f32>;
@group(0) @binding(2) var<uniform> mesh: Mesh;
@group(0) @binding(3) var<storage, read> marching: array<u32>;
// The slots, then each block's vertices and primitives, their offsets,
// the totals and the first refusal.
@group(0) @binding(4) var<storage, read_write> scratch: array<atomic<u32>>;
@group(0) @binding(5) var<storage, read_write> positions: array<f32>;
@group(0) @binding(6) var<storage, read_write> normals: array<f32>;
@group(0) @binding(7) var<storage, read_write> indices: array<u32>;
@group(0) @binding(8) var<storage, read_write> counts: array<u32>;

var<workgroup> meshScan: array<u32, CHUNK>;

fn sizes() -> vec3<u32> {
    return gas.size.xyz;
}

fn axes() -> u32 {
    return mesh.head.y;
}

// The cubes along each axis (one along Z in 2D).
fn cubeSizes() -> vec3<u32> {
    let n = sizes();
    return vec3<u32>(n.x - 1u, n.y - 1u, select(1u, n.z - 1u, axes() == 3u));
}

fn cubeIndex(c: vec3<u32>) -> u32 {
    let m = cubeSizes();
    return (c.z * m.y + c.y) * m.x + c.x;
}

fn cubeAt(i: u32) -> vec3<u32> {
    let m = cubeSizes();
    return vec3<u32>(i % m.x, i / m.x % m.y, i / (m.x * m.y));
}

fn sampleIndex(p: vec3<u32>) -> u32 {
    let n = sizes();
    return (p.z * n.y + p.y) * n.x + p.x;
}

// A sample's value less the level.
fn valueAt(p: vec3<u32>) -> f32 {
    return fields[gas.cells[mesh.head.x][0] + sampleIndex(p)] - mesh.level.x;
}

fn unit(axis: u32) -> vec3<u32> {
    var e = vec3<u32>(0u);
    e[axis] = 1u;
    return e;
}

// Slots' words: the first block word past them.
fn blocksFirst() -> u32 {
    return mesh.out.w;
}

fn blockCount() -> u32 {
    return mesh.head.w;
}

// Scratch words: each block's vertices, primitives, their offsets, then
// the totals and the first refusal.
fn blockVertices(b: u32) -> u32 {
    return blocksFirst() + b;
}

fn blockPrimitives(b: u32) -> u32 {
    return blocksFirst() + blockCount() + b;
}

fn vertexOffset(b: u32) -> u32 {
    return blocksFirst() + 2u * blockCount() + b;
}

fn primitiveOffset(b: u32) -> u32 {
    return blocksFirst() + 3u * blockCount() + b;
}

fn totalsAt() -> u32 {
    return blocksFirst() + 4u * blockCount();
}

fn refusalAt() -> u32 {
    return totalsAt() + 2u;
}

fn word(i: u32) -> u32 {
    return atomicLoad(&scratch[i]);
}

fn setWord(i: u32, value: u32) {
    _ = atomicExchange(&scratch[i], value);
}

// Whether a face of four values (in a cycle) joins its inside corners, as
// the core's JoinsInside: its products compared (their difference's sign)
// and its sum in the core's order.
fn joinsInside(a: f32, b: f32, c: f32, d: f32) -> bool {
    let p = a * c;
    let q = b * d;
    let below = ((a + c) - b) - d;
    return p != q && (p < q) != (below < 0.0);
}

// The first cube about the edge from sample p along axis, in the walk:
// the lower one along each other axis where there is one.
fn edgeOwner(p: vec3<u32>, axis: u32) -> u32 {
    var c = p;
    for (var k = 0u; k < axes(); k++) {
        if (k != axis && c[k] > 0u) {
            c[k] -= 1u;
        }
    }
    return cubeIndex(c);
}

// The first cube about a crossing edge from sample s (at the level): the
// edges to its inside neighbours.
fn sampleOwner(s: vec3<u32>) -> u32 {
    let n = sizes();
    var first = NONE;
    for (var a = 0u; a < axes(); a++) {
        if (s[a] + 1u < n[a] && valueAt(s + unit(a)) < 0.0) {
            first = min(first, edgeOwner(s, a));
        }
        if (s[a] > 0u && valueAt(s - unit(a)) < 0.0) {
            first = min(first, edgeOwner(s - unit(a), a));
        }
    }
    return first;
}

// Cube c's edge e (its sample into p, its axis).
fn cubeEdge(c: vec3<u32>, e: u32, p: ptr<function, vec3<u32>>) -> u32 {
    if (axes() == 2u) {
        // Bottom, top, left, right: along X from row y or y + 1, along Y
        // from column x or x + 1.
        *p = c + vec3<u32>(select(0u, 1u, e == 3u), select(0u, 1u, e == 1u), 0u);
        return select(1u, 0u, e < 2u);
    }
    let axis = e / 4u;
    let j = e % 4u;
    let first = select(0u, 1u, axis == 0u);
    let second = select(2u, 1u, axis == 2u);
    var q = c;
    q[first] += j & 1u;
    q[second] += (j >> 1u) & 1u;
    *p = q;
    return axis;
}

// A cube's corner i (a square's in 2D).
fn corner(c: vec3<u32>, i: u32) -> vec3<u32> {
    return c + vec3<u32>(i & 1u, (i >> 1u) & 1u, select(0u, i >> 2u, axes() == 3u));
}

fn cornerOf(c: vec3<u32>, p: vec3<u32>) -> u32 {
    let d = p - c;
    return d.x | (d.y << 1u) | (d.z << 2u);
}

// A cube's variant (a square's in 2D), as the core's CubeVariant.
fn variantOf(c: vec3<u32>) -> u32 {
    var v: array<f32, 8>;
    var index = 0u;
    let count = select(4u, 8u, axes() == 3u);
    for (var i = 0u; i < count; i++) {
        v[i] = valueAt(corner(c, i));
        index |= select(0u, 1u << i, v[i] < 0.0);
    }
    if (axes() == 2u) {
        // The cycle 0, 1, 3, 2: ambiguous when 0 and 3 are alike, 1 and 2.
        let ambiguous = index == 6u || index == 9u;
        let join = ambiguous && joinsInside(v[0], v[1], v[3], v[2]);
        return marching[MARCHING_SQUARE_VARIANT_FIRST + index] + select(0u, 1u, join);
    }
    var choice = 0u;
    var bit = 0u;
    for (var f = 0u; f < 6u; f++) {
        let at = MARCHING_CUBE_FACES + 4u * f;
        let c0 = marching[at];
        let c1 = marching[at + 1u];
        let c2 = marching[at + 2u];
        let c3 = marching[at + 3u];
        let i0 = v[c0] < 0.0;
        let i1 = v[c1] < 0.0;
        if (i0 == (v[c2] < 0.0) && i1 == (v[c3] < 0.0) && i0 != i1) {
            choice |= select(0u, 1u << bit, joinsInside(v[c0], v[c1], v[c2], v[c3]));
            bit++;
        }
    }
    return marching[MARCHING_CUBE_VARIANT_FIRST + index] + choice;
}

// A variant's primitives: the first and one past the last, and each one's
// vertices.
fn primitivesOf(variant: u32) -> vec2<u32> {
    let first = select(MARCHING_CUBE_TRIANGLE_FIRST, MARCHING_SQUARE_SEGMENT_FIRST, axes() == 2u);
    return vec2<u32>(marching[first + variant], marching[first + variant + 1u]);
}

fn perPrimitive() -> u32 {
    return axes();
}

fn primitiveEdge(t: u32, i: u32) -> u32 {
    let first = select(MARCHING_CUBE_TRIANGLE_EDGES, MARCHING_SQUARE_SEGMENT_EDGES, axes() == 2u);
    return marching[first + perPrimitive() * t + i];
}

// The key cube c names a primitive's entry e by: an edge's, or its
// sample's at the level; a center's.
fn keyOf(c: vec3<u32>, e: u32) -> u32 {
    if (e >= CENTER_KEY) {
        return e;
    }
    var p: vec3<u32>;
    let axis = cubeEdge(c, e, &p);
    let q = p + unit(axis);
    if (valueAt(p) == 0.0) {
        return SAMPLE_KEY + cornerOf(c, p);
    }
    if (valueAt(q) == 0.0) {
        return SAMPLE_KEY + cornerOf(c, q);
    }
    return e;
}

// Whether cube c (index i) makes the vertex of key.
fn makes(c: vec3<u32>, i: u32, key: u32) -> bool {
    if (key >= SAMPLE_KEY) {
        return sampleOwner(corner(c, key - SAMPLE_KEY)) == i;
    }
    if (key >= CENTER_KEY) {
        return true;
    }
    var p: vec3<u32>;
    let axis = cubeEdge(c, key, &p);
    return edgeOwner(p, axis) == i;
}

// A primitive's keys, and whether they are all distinct (kept).
fn keysOf(c: vec3<u32>, t: u32, keys: ptr<function, array<u32, 3>>) -> bool {
    for (var k = 0u; k < perPrimitive(); k++) {
        (*keys)[k] = keyOf(c, primitiveEdge(t, k));
    }
    if (perPrimitive() == 2u) {
        return (*keys)[0] != (*keys)[1];
    }
    return (*keys)[0] != (*keys)[1] && (*keys)[1] != (*keys)[2] && (*keys)[0] != (*keys)[2];
}

// A cube's vertices made and primitives kept.
fn cubeCounts(i: u32) -> vec2<u32> {
    if (i >= mesh.head.z) {
        return vec2<u32>(0u);
    }
    let c = cubeAt(i);
    let span = primitivesOf(variantOf(c));
    var seen = 0u;
    var made = 0u;
    var kept = 0u;
    for (var t = span.x; t < span.y; t++) {
        var keys: array<u32, 3>;
        kept += select(0u, 1u, keysOf(c, t, &keys));
        for (var k = 0u; k < perPrimitive(); k++) {
            let bit = 1u << keys[k];
            if ((seen & bit) == 0u) {
                seen |= bit;
                made += select(0u, 1u, makes(c, i, keys[k]));
            }
        }
    }
    return vec2<u32>(made, kept);
}

// The sum of value over the invocations before this one.
fn exclusiveScan(local: u32, value: u32) -> u32 {
    meshScan[local] = value;
    workgroupBarrier();
    for (var offset = 1u; offset < CHUNK; offset <<= 1u) {
        var add = 0u;
        if (local >= offset) {
            add = meshScan[local - offset];
        }
        workgroupBarrier();
        meshScan[local] += add;
        workgroupBarrier();
    }
    return meshScan[local] - value;
}

// Each block's vertices and primitives.
@compute @workgroup_size(256, 1, 1)
fn meshCount(@builtin(local_invocation_index) local: u32,
             @builtin(workgroup_id) group: vec3<u32>) {
    for (var b = group.x; b < blockCount(); b += mesh.out.z) {
        let counts2 = cubeCounts(b * CHUNK + local);
        let made = exclusiveScan(local, counts2.x);
        if (local == CHUNK - 1u) {
            setWord(blockVertices(b), made + counts2.x);
        }
        let kept = exclusiveScan(local, counts2.y);
        if (local == CHUNK - 1u) {
            setWord(blockPrimitives(b), kept + counts2.y);
        }
    }
}

// Each block's first vertex and primitive, in one workgroup; the totals,
// and no refusal yet.
@compute @workgroup_size(256, 1, 1)
fn meshOffsets(@builtin(local_invocation_index) local: u32) {
    let blocks = blockCount();
    let per = (blocks + CHUNK - 1u) / CHUNK;
    let lo = min(local * per, blocks);
    let hi = min(lo + per, blocks);
    for (var which = 0u; which < 2u; which++) {
        var sum = 0u;
        for (var b = lo; b < hi; b++) {
            sum += word(select(blockVertices(b), blockPrimitives(b), which == 1u));
        }
        var at = exclusiveScan(local, sum);
        for (var b = lo; b < hi; b++) {
            let n = word(select(blockVertices(b), blockPrimitives(b), which == 1u));
            setWord(select(vertexOffset(b), primitiveOffset(b), which == 1u), at);
            at += n;
        }
        if (local == CHUNK - 1u) {
            setWord(totalsAt() + which, at);
        }
        workgroupBarrier();
    }
    if (local == 0u) {
        setWord(refusalAt(), NONE);
    }
}

fn rotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// The distance's gradient at a sample in the box's frame, as the core's
// GradientAt: central differences inside, one-sided at the faces.
fn gradientAt(p: vec3<u32>) -> vec3<f32> {
    let n = sizes();
    let cell = gas.lattice.x;
    var out = vec3<f32>(0.0);
    for (var k = 0u; k < axes(); k++) {
        var lo = p;
        var hi = p;
        lo[k] = select(p[k], p[k] - 1u, p[k] > 0u);
        hi[k] = select(p[k], p[k] + 1u, p[k] + 1u < n[k]);
        out[k] = (valueAt(hi) - valueAt(lo)) / (f32(hi[k] - lo[k]) * cell);
    }
    return out;
}

// A sample's place in the box's frame.
fn placeOf(p: vec3<f32>) -> vec3<f32> {
    let n = vec3<f32>(sizes());
    return (p + 0.5 - 0.5 * n) * gas.lattice.x;
}

// Where an edge crosses the level and the gradient there, as the core's
// EdgePoint; into place and gradient.
fn edgePoint(p: vec3<u32>, axis: u32, place: ptr<function, vec3<f32>>,
             gradient: ptr<function, vec3<f32>>) {
    let q = p + unit(axis);
    let a = valueAt(p);
    let t = a / (a - valueAt(q));
    let ga = gradientAt(p);
    let gb = gradientAt(q);
    var at = vec3<f32>(p);
    at[axis] += t;
    *place = placeOf(at);
    *gradient = ga + t * (gb - ga);
}

// Writes vertex id at place with its gradient's direction, when below the
// capacity.
fn emit(id: u32, place: vec3<f32>, gradient: vec3<f32>) {
    if (id >= mesh.caps.x) {
        return;
    }
    let world = rotate(mesh.rotation, place) * mesh.back.w + mesh.back.xyz;
    for (var k = 0u; k < 3u; k++) {
        positions[mesh.caps.z + 3u * id + k] = world[k];
    }
    if (mesh.caps.w == NONE) {
        return;
    }
    let turned = rotate(mesh.rotation, gradient) * mesh.back.w;
    let length = sqrt(turned.x * turned.x + turned.y * turned.y + turned.z * turned.z);
    for (var k = 0u; k < 3u; k++) {
        normals[mesh.caps.w + 3u * id + k] = select(0.0, turned[k] / length, length > 0.0);
    }
}

// Variant v's center k of cube c: the mean of its edges' points.
fn centerVertex(id: u32, c: vec3<u32>, v: u32, k: u32) {
    var at = MARCHING_CUBE_CENTER_EDGES + marching[MARCHING_CUBE_CENTER_FIRST + v];
    for (var skip = 0u; skip < k; skip++) {
        at += 1u + marching[at];
    }
    let count = marching[at];
    var place = vec3<f32>(0.0);
    var gradient = vec3<f32>(0.0);
    for (var i = 0u; i < count; i++) {
        var p: vec3<u32>;
        let axis = cubeEdge(c, marching[at + 1u + i], &p);
        var x: vec3<f32>;
        var g: vec3<f32>;
        edgePoint(p, axis, &x, &g);
        place += x / f32(count);
        gradient += g / f32(count);
    }
    emit(id, place, gradient);
}

// Makes the vertex of key as number id: its slot, and its place.
fn makeVertex(c: vec3<u32>, v: u32, key: u32, id: u32) {
    if (key >= SAMPLE_KEY) {
        let s = corner(c, key - SAMPLE_KEY);
        setWord(4u * sampleIndex(s) + 3u, id);
        emit(id, placeOf(vec3<f32>(s)), gradientAt(s));
        return;
    }
    if (key >= CENTER_KEY) {
        centerVertex(id, c, v, key - CENTER_KEY);
        return;
    }
    var p: vec3<u32>;
    let axis = cubeEdge(c, key, &p);
    setWord(4u * sampleIndex(p) + axis, id);
    var place: vec3<f32>;
    var gradient: vec3<f32>;
    edgePoint(p, axis, &place, &gradient);
    emit(id, place, gradient);
}

// The vertex number of a key cube c names, made by another cube: from the
// slots.
fn slotOf(c: vec3<u32>, key: u32) -> u32 {
    if (key >= SAMPLE_KEY) {
        return word(4u * sampleIndex(corner(c, key - SAMPLE_KEY)) + 3u);
    }
    var p: vec3<u32>;
    let axis = cubeEdge(c, key, &p);
    return word(4u * sampleIndex(p) + axis);
}

// A cube's first vertex and primitive: its block's, and those of the
// block's cubes before it.
fn cubeFirsts(local: u32, b: u32, counts2: vec2<u32>) -> vec2<u32> {
    let made = exclusiveScan(local, counts2.x);
    let kept = exclusiveScan(local, counts2.y);
    return vec2<u32>(word(vertexOffset(b)) + made, word(primitiveOffset(b)) + kept);
}

// Each cube's vertices, numbered where its primitives first name them.
@compute @workgroup_size(256, 1, 1)
fn meshVertices(@builtin(local_invocation_index) local: u32,
                @builtin(workgroup_id) group: vec3<u32>) {
    for (var b = group.x; b < blockCount(); b += mesh.out.z) {
        let i = b * CHUNK + local;
        let firsts = cubeFirsts(local, b, cubeCounts(i));
        if (i >= mesh.head.z) {
            continue;
        }
        let c = cubeAt(i);
        let v = variantOf(c);
        let span = primitivesOf(v);
        var seen = 0u;
        var next = firsts.x;
        for (var t = span.x; t < span.y; t++) {
            var keys: array<u32, 3>;
            _ = keysOf(c, t, &keys);
            for (var k = 0u; k < perPrimitive(); k++) {
                let bit = 1u << keys[k];
                if ((seen & bit) == 0u) {
                    seen |= bit;
                    if (makes(c, i, keys[k])) {
                        makeVertex(c, v, keys[k], next);
                        next++;
                    }
                }
            }
        }
    }
}

// Each kept primitive of cube i in turn: its number among them all, its
// vertices' numbers (its own by rank, the rest from the slots), whether
// it is the last; calls visit through a mode: 0 the refusal, 1 the write.
fn walkPrimitives(i: u32, firsts: vec2<u32>, mode: u32) {
    let c = cubeAt(i);
    let span = primitivesOf(variantOf(c));
    var seen = 0u;
    var next = firsts.x;
    var mine: array<u32, 24>;
    var number = firsts.y;
    let per = perPrimitive();
    let refusal = word(refusalAt());
    for (var t = span.x; t < span.y; t++) {
        var keys: array<u32, 3>;
        let kept = keysOf(c, t, &keys);
        var ids: array<u32, 3>;
        var most = 0u;
        for (var k = 0u; k < per; k++) {
            let key = keys[k];
            let bit = 1u << key;
            if ((seen & bit) == 0u) {
                seen |= bit;
                mine[key] = select(NONE, next, makes(c, i, key));
                next += select(0u, 1u, mine[key] != NONE);
            }
            ids[k] = select(slotOf(c, key), mine[key], mine[key] != NONE);
            most = max(most, ids[k]);
        }
        if (!kept) {
            continue;
        }
        if (mode == 0u) {
            if (most >= mesh.caps.x || per * (number + 1u) > mesh.caps.y) {
                _ = atomicMin(&scratch[refusalAt()], number);
            }
        } else if (number < refusal) {
            for (var k = 0u; k < per; k++) {
                indices[mesh.out.x + per * number + k] = ids[k];
            }
        }
        number++;
    }
}

// The first kept primitive that does not fit both buffers.
@compute @workgroup_size(256, 1, 1)
fn meshRefuse(@builtin(local_invocation_index) local: u32,
              @builtin(workgroup_id) group: vec3<u32>) {
    for (var b = group.x; b < blockCount(); b += mesh.out.z) {
        let i = b * CHUNK + local;
        let firsts = cubeFirsts(local, b, cubeCounts(i));
        if (i < mesh.head.z) {
            walkPrimitives(i, firsts, 0u);
        }
    }
}

// The primitives before the first refusal, and the counts: the vertices
// and indices there are, then those written.
@compute @workgroup_size(256, 1, 1)
fn meshIndices(@builtin(local_invocation_index) local: u32,
               @builtin(workgroup_id) group: vec3<u32>) {
    for (var b = group.x; b < blockCount(); b += mesh.out.z) {
        let i = b * CHUNK + local;
        let firsts = cubeFirsts(local, b, cubeCounts(i));
        if (i < mesh.head.z) {
            walkPrimitives(i, firsts, 1u);
        }
    }
    if (group.x == 0u && local == 0u && mesh.out.y != NONE) {
        let vertices = word(totalsAt());
        let primitives = word(totalsAt() + 1u);
        let written = min(primitives, word(refusalAt()));
        counts[mesh.out.y] = vertices;
        counts[mesh.out.y + 1u] = perPrimitive() * primitives;
        counts[mesh.out.y + 2u] = min(vertices, mesh.caps.x);
        counts[mesh.out.y + 3u] = perPrimitive() * written;
    }
}
