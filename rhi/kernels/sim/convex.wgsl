// The sim kernel's polytope searches (spliced into sim.wgsl), as the core's
// src/convex.c in binary32: GJK (Gilbert, Johnson and Keerthi
// 1988; the simplex's regions as Ericson's 5.1), EPA (van den Bergen 2001)
// and the face manifold, for rigid hulls kept out of spheres and boxes.
// The core's relative tolerances on squared lengths are scaled to
// binary32; its caps and ties hold.

// A polytope as the searches read it: a hull body (its vertices scaled
// and turned to its pose), a box collider (its corners) or a point; its
// vertices and faces' planes are found as read, as the core's
// mvfxHullPolytope and mvfxBoxPolytope make them.
struct Poly {
    kind: u32,
    x: vec3<f32>,
    q: vec4<f32>,
    scale: vec3<f32>,
    count: u32,
    faces: u32,
}

const POLY_HULL: u32 = 0u;
const POLY_BOX: u32 = 1u;
const POLY_POINT: u32 = 2u;

// The most steps a search takes (MVFX_CONVEX_STEPS), and EPA's most points
// and faces.
const CONVEX_STEPS: u32 = 64u;
const EPA_POINTS: u32 = 68u;
const EPA_FACES: u32 = 136u;

// The most points a face's polygon holds while clipped
// (MVFX_MAX_MANIFOLD_POINTS).
const MANIFOLD_POINTS: u32 = 128u;

// A box's face f's first corner in its loop, and the loop's corner j
// (bit k of a corner set for +k), each face counterclockwise seen from
// outside: +X, -X, +Y, -Y, +Z, -Z.
fn boxLoop(j: u32) -> u32 {
    var corners = array<u32, 24>(1u, 3u, 7u, 5u, 0u, 4u, 6u, 2u, 2u, 6u, 7u, 3u, 0u, 1u, 5u, 4u,
                                 4u, 5u, 7u, 6u, 0u, 2u, 3u, 1u);
    return corners[j];
}

fn polyFaceStart(p: Poly, f: u32) -> u32 {
    return select(4u * f, hullFaceStart(f), p.kind == POLY_HULL);
}

fn polyLoop(p: Poly, j: u32) -> u32 {
    return select(boxLoop(j), hullLoop(j), p.kind == POLY_HULL);
}

// A hull's face f's plane at a size of 1, after its starts and loop.
fn hullPlane(f: u32) -> vec4<f32> {
    let at = bitcast<u32>(params.rigidSizeRange.w) + hullFaceCount() + 1u +
             hullFaceStart(hullFaceCount()) + 4u * f;
    return vec4<f32>(tableAt(at), tableAt(at + 1u), tableAt(at + 2u), tableAt(at + 3u));
}

fn polyVertex(p: Poly, i: u32) -> vec3<f32> {
    if (p.kind == POLY_POINT) {
        return p.x;
    }
    if (p.kind == POLY_HULL) {
        return rotate(p.q, hullVertex(i, p.scale)) + p.x;
    }
    let bits = (vec3<u32>(i) >> vec3<u32>(0u, 1u, 2u)) & vec3<u32>(1u);
    return rotate(p.q, select(-p.scale, p.scale, bits != vec3<u32>(0u))) + p.x;
}

// A face's outward unit normal and offset (n . x = d on it): a box's axis
// turned, a hull's normal scaled by the inverse of the scale, made unit,
// then turned.
fn polyPlane(p: Poly, f: u32) -> vec4<f32> {
    if (p.kind == POLY_BOX) {
        var axis = vec3<f32>(0.0);
        axis[f / 2u] = select(-1.0, 1.0, f % 2u == 0u);
        let n = rotate(p.q, axis);
        return vec4<f32>(n, dot3(n, p.x) + p.scale[f / 2u]);
    }
    var n = hullPlane(f).xyz / max(p.scale, vec3<f32>(1e-30));
    let square = dot3(n, n);
    if (square > 0.0 && square <= LARGEST) {
        n = n * (1.0 / sqrt(square));
    }
    n = rotate(p.q, n);
    return vec4<f32>(n, dot3(n, polyVertex(p, polyLoop(p, polyFaceStart(p, f)))));
}

// The polytope's vertex furthest along d, the lowest on a tie.
fn furthest(p: Poly, d: vec3<f32>) -> u32 {
    var best = 0u;
    var most = dot3(polyVertex(p, 0u), d);
    for (var i = 1u; i < p.count; i++) {
        let along = dot3(polyVertex(p, i), d);
        if (along > most) {
            most = along;
            best = i;
        }
    }
    return best;
}

// A vertex of the polytopes' difference, a less b, with a's vertex and
// b's.
struct Point {
    w: vec3<f32>,
    a: u32,
    b: u32,
}

fn support(a: Poly, b: Poly, d: vec3<f32>) -> Point {
    let i = furthest(a, d);
    let j = furthest(b, -d);
    return Point(polyVertex(a, i) - polyVertex(b, j), i, j);
}

// A simplex of the difference and the weights of its point nearest the
// origin.
struct Simplex {
    p: array<Point, 4>,
    weight: array<f32, 4>,
    count: u32,
}

// Keeps n of the simplex's points, i, j and k in order, with weights.
fn keepPoints(s: ptr<function, Simplex>, i: u32, j: u32, k: u32, weight: vec3<f32>, n: u32) {
    let pi = (*s).p[i];
    let pj = (*s).p[j];
    let pk = (*s).p[k];
    (*s).p[0] = pi;
    (*s).p[1] = pj;
    (*s).p[2] = pk;
    (*s).weight[0] = weight.x;
    (*s).weight[1] = weight.y;
    (*s).weight[2] = weight.z;
    (*s).count = n;
}

// The nearest point to the origin of segment i j, kept.
fn segment(s: ptr<function, Simplex>, i: u32, j: u32) {
    let ab = (*s).p[j].w - (*s).p[i].w;
    let length = dot3(ab, ab);
    let t = select(0.0, -dot3((*s).p[i].w, ab) / length, length > 0.0);
    if (t <= 0.0) {
        keepPoints(s, i, i, i, vec3<f32>(1.0, 0.0, 0.0), 1u);
    } else if (t >= 1.0) {
        keepPoints(s, j, j, j, vec3<f32>(1.0, 0.0, 0.0), 1u);
    } else {
        keepPoints(s, i, j, j, vec3<f32>(1.0 - t, t, 0.0), 2u);
    }
}

// The nearest point to the origin of triangle i j k by its regions, kept.
fn triangle(s: ptr<function, Simplex>, i: u32, j: u32, k: u32) {
    let a = (*s).p[i].w;
    let b = (*s).p[j].w;
    let c = (*s).p[k].w;
    let ab = b - a;
    let ac = c - a;
    let d1 = -dot3(ab, a);
    let d2 = -dot3(ac, a);
    let d3 = -dot3(ab, b);
    let d4 = -dot3(ac, b);
    let d5 = -dot3(ab, c);
    let d6 = -dot3(ac, c);
    let vc = d1 * d4 - d3 * d2;
    let vb = d5 * d2 - d1 * d6;
    let va = d3 * d6 - d5 * d4;
    if (d1 <= 0.0 && d2 <= 0.0) {
        keepPoints(s, i, i, i, vec3<f32>(1.0, 0.0, 0.0), 1u);
    } else if (d3 >= 0.0 && d4 <= d3) {
        keepPoints(s, j, j, j, vec3<f32>(1.0, 0.0, 0.0), 1u);
    } else if (d6 >= 0.0 && d5 <= d6) {
        keepPoints(s, k, k, k, vec3<f32>(1.0, 0.0, 0.0), 1u);
    } else if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        segment(s, i, j);
    } else if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        segment(s, i, k);
    } else if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0) {
        segment(s, j, k);
    } else if (!(va + vb + vc > 0.0)) {
        // No area: an edge stands for it.
        segment(s, i, j);
    } else {
        let sum = va + vb + vc;
        keepPoints(s, i, j, k, vec3<f32>(va / sum, vb / sum, vc / sum), 3u);
    }
}

// The weighted point of the simplex.
fn pointOf(s: Simplex) -> vec3<f32> {
    var v = vec3<f32>(0.0);
    for (var i = 0u; i < s.count; i++) {
        v += s.weight[i] * s.p[i].w;
    }
    return v;
}

// The nearest point to the origin of the tetrahedron: of each face the
// origin is beyond, the nearest's, kept; false when the origin is inside.
fn tetrahedron(s: ptr<function, Simplex>) -> bool {
    var faces = array<vec4<u32>, 4>(vec4<u32>(0u, 1u, 2u, 3u), vec4<u32>(0u, 2u, 3u, 1u),
                                     vec4<u32>(0u, 3u, 1u, 2u), vec4<u32>(1u, 3u, 2u, 0u));
    var best = *s;
    var nearest = 0.0;
    var found = false;
    for (var f = 0u; f < 4u; f++) {
        let at = faces[f];
        let a = (*s).p[at.x].w;
        let n = cross((*s).p[at.y].w - a, (*s).p[at.z].w - a);
        let origin = -dot3(a, n);
        let other = dot3((*s).p[at.w].w - a, n);
        // Beyond the face: the origin and the fourth point on its two
        // sides (or a flat tetrahedron).
        if (origin * other <= 0.0) {
            var face = *s;
            triangle(&face, at.x, at.y, at.z);
            let v = pointOf(face);
            let distance = dot3(v, v);
            if (!found || distance < nearest) {
                nearest = distance;
                best = face;
                found = true;
            }
        }
    }
    if (!found) {
        return false;
    }
    *s = best;
    return true;
}

// The simplex's nearest point to the origin, the simplex cut to the points
// that weigh in it; false when it holds the origin.
fn nearestOf(s: ptr<function, Simplex>, v: ptr<function, vec3<f32>>) -> bool {
    var apart = true;
    if ((*s).count == 2u) {
        segment(s, 0u, 1u);
    } else if ((*s).count == 3u) {
        triangle(s, 0u, 1u, 2u);
    } else if ((*s).count == 4u) {
        apart = tetrahedron(s);
    }
    *v = pointOf(*s);
    return apart;
}

// GJK: the simplex nearest the origin of a's and b's difference and its
// nearest point v; false when they overlap. At most CONVEX_STEPS steps;
// capped when it stopped there. Its first round takes the first support
// (from the first vertices' difference), each later one a step.
fn gjk(a: Poly, b: Poly, s: ptr<function, Simplex>, v: ptr<function, vec3<f32>>,
       capped: ptr<function, bool>) -> bool {
    *capped = false;
    var way = polyVertex(a, 0u) - polyVertex(b, 0u);
    var reach = 0.0;
    for (var round = 0u; round <= CONVEX_STEPS; round++) {
        let vv = dot3(*v, *v);
        if (round > 0u) {
            if (vv <= 1e-12 * reach) {
                return false;
            }
            way = -*v;
        }
        let p = support(a, b, way);
        if (round == 0u) {
            (*s).p[0] = p;
            (*s).weight[0] = 1.0;
            (*s).count = 1u;
            *v = p.w;
            reach = dot3(p.w, p.w);
            continue;
        }
        reach = max(reach, dot3(p.w, p.w));
        if (vv - dot3(*v, p.w) <= 1e-6 * vv) {
            return true;
        }
        for (var i = 0u; i < (*s).count; i++) {
            let d = p.w - (*s).p[i].w;
            if (dot3(d, d) <= 1e-12 * reach) {
                return true;
            }
        }
        (*s).p[(*s).count] = p;
        (*s).count += 1u;
        if (!nearestOf(s, v)) {
            return false;
        }
        // No nearer than before: as near as rounding allows.
        if (!(dot3(*v, *v) < vv)) {
            return true;
        }
    }
    *capped = true;
    return true;
}

// The witnesses of a simplex's weighted point: a's and b's.
fn witnesses(a: Poly, b: Poly, s: Simplex, onA: ptr<function, vec3<f32>>,
             onB: ptr<function, vec3<f32>>) {
    *onA = vec3<f32>(0.0);
    *onB = vec3<f32>(0.0);
    for (var i = 0u; i < s.count; i++) {
        *onA += s.weight[i] * polyVertex(a, s.p[i].a);
        *onB += s.weight[i] * polyVertex(b, s.p[i].b);
    }
}

// How far w is from the simplex's span: its point, line or plane.
fn offSpan(s: Simplex, w: vec3<f32>) -> f32 {
    let d = w - s.p[0].w;
    if (s.count == 1u) {
        return sqrt(dot3(d, d));
    }
    let ab = s.p[1].w - s.p[0].w;
    if (s.count == 2u) {
        let n = cross(ab, d);
        let length = dot3(ab, ab);
        return select(0.0, sqrt(dot3(n, n) / length), length > 0.0);
    }
    let n = cross(ab, s.p[2].w - s.p[0].w);
    let area = sqrt(dot3(n, n));
    return select(0.0, abs(dot3(d, n)) / area, area > 0.0);
}

// Grows a simplex holding the origin to a tetrahedron by points of the
// difference along the axes and across what it has, each new when off its
// span by a millionth of the difference's reach; false when the
// difference is flat.
fn blow(a: Poly, b: Poly, s: ptr<function, Simplex>) -> bool {
    for (var round = 0u; round < 3u && (*s).count < 4u; round++) {
        var across = vec3<f32>(0.0);
        var ways = 6u;
        if ((*s).count == 3u) {
            across = cross((*s).p[1].w - (*s).p[0].w, (*s).p[2].w - (*s).p[0].w);
            ways = 2u;
        }
        var grown = false;
        for (var w = 0u; w < ways && !grown; w++) {
            var way = vec3<f32>(0.0);
            way[w / 2u] = select(-1.0, 1.0, w % 2u == 0u);
            if (ways == 2u) {
                way = select(-across, across, w == 0u);
            }
            let p = support(a, b, way);
            var reach = sqrt(dot3(p.w, p.w));
            for (var i = 0u; i < (*s).count; i++) {
                reach = max(reach, sqrt(dot3((*s).p[i].w, (*s).p[i].w)));
            }
            if (offSpan(*s, p.w) > 1e-6 * reach) {
                (*s).p[(*s).count] = p;
                (*s).count += 1u;
                grown = true;
            }
        }
        if (!grown) {
            return false;
        }
    }
    return (*s).count == 4u;
}

// EPA's polytope: the difference's points (a's and b's vertices packed
// in a word) and triangles (the points' indices packed in a word, counter
// clockwise from outside), each with its outward unit normal and its
// distance from the origin.
struct Epa {
    w: array<vec3<f32>, EPA_POINTS>,
    vertices: array<u32, EPA_POINTS>,
    pointCount: u32,
    faces: array<u32, EPA_FACES>,
    normals: array<vec3<f32>, EPA_FACES>,
    distances: array<f32, EPA_FACES>,
    faceCount: u32,
}

fn epaPoint(e: ptr<function, Epa>, i: u32) -> Point {
    let v = (*e).vertices[i];
    return Point((*e).w[i], v & 0xFFu, v >> 8u);
}

fn faceCorner(e: ptr<function, Epa>, f: u32, k: u32) -> u32 {
    return ((*e).faces[f] >> (8u * k)) & 0xFFu;
}

// Adds triangle i j k; false for no area or no room.
fn addTriangle(e: ptr<function, Epa>, i: u32, j: u32, k: u32) -> bool {
    if ((*e).faceCount >= EPA_FACES) {
        return false;
    }
    let n = cross((*e).w[j] - (*e).w[i], (*e).w[k] - (*e).w[i]);
    let length = sqrt(dot3(n, n));
    if (!(length > 0.0)) {
        return false;
    }
    let f = (*e).faceCount;
    (*e).faceCount += 1u;
    (*e).faces[f] = i | (j << 8u) | (k << 16u);
    (*e).normals[f] = n / length;
    (*e).distances[f] = dot3((*e).normals[f], (*e).w[i]);
    return true;
}

// Starts EPA's polytope from a tetrahedron, its faces turned outward.
fn startEpa(e: ptr<function, Epa>, s: Simplex) -> bool {
    (*e).pointCount = 4u;
    (*e).faceCount = 0u;
    for (var i = 0u; i < 4u; i++) {
        (*e).w[i] = s.p[i].w;
        (*e).vertices[i] = s.p[i].a | (s.p[i].b << 8u);
    }
    let n = cross(s.p[1].w - s.p[0].w, s.p[2].w - s.p[0].w);
    let flip = dot3(n, s.p[3].w - s.p[0].w) > 0.0;
    let b = select(1u, 2u, flip);
    let c = select(2u, 1u, flip);
    return addTriangle(e, 0u, b, c) && addTriangle(e, 0u, 3u, b) && addTriangle(e, b, 3u, c) &&
           addTriangle(e, c, 3u, 0u);
}

// The face nearest the origin.
fn nearestFace(e: ptr<function, Epa>) -> u32 {
    var best = 0u;
    for (var f = 1u; f < (*e).faceCount; f++) {
        best = select(best, f, (*e).distances[f] < (*e).distances[best]);
    }
    return best;
}

// The horizon's edges, two to a word (a corner pair in 16 bits each).
fn edgeAt(edges: ptr<function, array<u32, 204>>, j: u32) -> u32 {
    return ((*edges)[j / 2u] >> (16u * (j % 2u))) & 0xFFFFu;
}

fn setEdge(edges: ptr<function, array<u32, 204>>, j: u32, edge: u32) {
    let shift = 16u * (j % 2u);
    (*edges)[j / 2u] = ((*edges)[j / 2u] & ~(0xFFFFu << shift)) | (edge << shift);
}

// Adds point p: the faces it sees removed and the horizon joined to it, in
// the core's order (a twin removed by the last edge); false when there is
// no room.
fn expand(e: ptr<function, Epa>, p: Point) -> bool {
    if ((*e).pointCount >= EPA_POINTS) {
        return false;
    }
    let at = (*e).pointCount;
    (*e).pointCount += 1u;
    (*e).w[at] = p.w;
    (*e).vertices[at] = p.a | (p.b << 8u);
    var edges = array<u32, 204>();
    var edgeCount = 0u;
    var kept = 0u;
    for (var f = 0u; f < (*e).faceCount; f++) {
        if (dot3((*e).normals[f], p.w) - (*e).distances[f] <=
            1e-6 * (abs((*e).distances[f]) + 1e-30)) {
            (*e).faces[kept] = (*e).faces[f];
            (*e).normals[kept] = (*e).normals[f];
            (*e).distances[kept] = (*e).distances[f];
            kept += 1u;
            continue;
        }
        for (var k = 0u; k < 3u; k++) {
            let start = faceCorner(e, f, k);
            let end = faceCorner(e, f, (k + 1u) % 3u);
            // An edge seen from both sides is inside the hole.
            var twin = false;
            for (var j = 0u; j < edgeCount && !twin; j++) {
                if (edgeAt(&edges, j) == (end | (start << 8u))) {
                    twin = true;
                    // Read, then step: maul-rhi's stale_reads falsely refuses the reverse.
                    let last = edgeCount - 1u;
                    setEdge(&edges, j, edgeAt(&edges, last));
                    edgeCount = last;
                }
            }
            if (!twin) {
                setEdge(&edges, edgeCount, start | (end << 8u));
                edgeCount += 1u;
            }
        }
    }
    (*e).faceCount = kept;
    for (var j = 0u; j < edgeCount; j++) {
        let edge = edgeAt(&edges, j);
        if (!addTriangle(e, edge & 0xFFu, edge >> 8u, at)) {
            return false;
        }
    }
    return true;
}

// How two polytopes meet (the core's mvfxConvexMeet): the unit normal from
// b toward a, their distance along it (below 0 where they overlap), the
// nearest or deepest points of each, whether a search stopped at its cap.
struct Meet {
    normal: vec3<f32>,
    distance: f32,
    onA: vec3<f32>,
    onB: vec3<f32>,
    capped: bool,
}

// EPA: of the difference holding the origin, the face nearest it, grown
// until its distance holds.
fn penetration(a: Poly, b: Poly, s: ptr<function, Simplex>) -> Meet {
    var out = Meet(vec3<f32>(0.0, 1.0, 0.0), 0.0, vec3<f32>(0.0), vec3<f32>(0.0), false);
    var onA = vec3<f32>(0.0);
    var onB = vec3<f32>(0.0);
    witnesses(a, b, *s, &onA, &onB);
    out.onA = onA;
    out.onB = onB;
    var e: Epa;
    if (!blow(a, b, s) || !startEpa(&e, *s)) {
        return out;
    }
    // The gain's floor: a millionth of the difference's reach (the core's
    // 1e-12 is absolute; binary32 rounds a gain by about 1e-7 of the
    // reach, so a shallow overlap's would never come under it).
    var reach = 0.0;
    for (var i = 0u; i < 4u; i++) {
        reach = max(reach, sqrt(dot3(e.w[i], e.w[i])));
    }
    var f = nearestFace(&e);
    var steps = 0u;
    for (; steps < CONVEX_STEPS; steps++) {
        let p = support(a, b, e.normals[f]);
        let gain = dot3(e.normals[f], p.w) - e.distances[f];
        if (gain <= 1e-6 * max(e.distances[f], reach) || !expand(&e, p) || e.faceCount == 0u) {
            break;
        }
        f = nearestFace(&e);
    }
    out.capped = steps >= CONVEX_STEPS;
    // The face's point nearest the origin, by its triangle's weights.
    var face: Simplex;
    face.count = 3u;
    for (var i = 0u; i < 3u; i++) {
        face.p[i] = epaPoint(&e, faceCorner(&e, f, i));
    }
    var v = vec3<f32>(0.0);
    _ = nearestOf(&face, &v);
    witnesses(a, b, face, &onA, &onB);
    out.onA = onA;
    out.onB = onB;
    out.normal = -e.normals[f];
    out.distance = -e.distances[f];
    return out;
}

// How a meets b, as the core's mvfxConvexMeets (deep, b a box: false where
// they part by the margin or more, unless the search stopped at its cap;
// overlapping, EPA's depth) or mvfxPolytopeNearest (b a point: a's point
// nearest it as onA at the distance, the point itself at 0 within a).
fn convexSearch(a: Poly, b: Poly, margin: f32, deep: bool, out: ptr<function, Meet>) -> bool {
    var s: Simplex;
    var v = vec3<f32>(0.0);
    var capped = false;
    if (!gjk(a, b, &s, &v, &capped)) {
        if (deep) {
            *out = penetration(a, b, &s);
        } else {
            *out = Meet(vec3<f32>(0.0, 1.0, 0.0), 0.0, b.x, b.x, false);
        }
        return true;
    }
    let distance = sqrt(dot3(v, v));
    if (deep && !(distance < margin) && !capped) {
        return false;
    }
    var onA = vec3<f32>(0.0);
    var onB = vec3<f32>(0.0);
    witnesses(a, b, s, &onA, &onB);
    let normal = select(vec3<f32>(0.0, 1.0, 0.0), v / distance, distance > 0.0);
    *out = Meet(normal, distance, onA, onB, capped);
    return true;
}

// The face of p whose normal is furthest along d, and how far.
fn faceAlong(p: Poly, d: vec3<f32>, along: ptr<function, f32>) -> u32 {
    var best = 0u;
    *along = dot3(polyPlane(p, 0u).xyz, d);
    for (var f = 1u; f < p.faces; f++) {
        let a = dot3(polyPlane(p, f).xyz, d);
        if (a > *along) {
            *along = a;
            best = f;
        }
    }
    return best;
}

// Clips a polygon to dot(x, l) <= limit, keeping points on the plane and
// adding no crossing beside them.
fn clipPolygon(polygon: ptr<function, array<vec3<f32>, MANIFOLD_POINTS>>, count: u32,
               l: vec3<f32>, limit: f32, on: f32) -> u32 {
    var out = array<vec3<f32>, MANIFOLD_POINTS>();
    var kept = 0u;
    for (var i = 0u; i < count; i++) {
        let a = (*polygon)[i];
        let b = (*polygon)[(i + 1u) % count];
        let da = dot3(a, l) - limit;
        let db = dot3(b, l) - limit;
        if (da <= on && kept < MANIFOLD_POINTS) {
            out[kept] = a;
            kept += 1u;
        }
        if (((da < -on && db > on) || (da > on && db < -on)) && kept < MANIFOLD_POINTS) {
            let f = da / (da - db);
            out[kept] = a + f * (b - a);
            kept += 1u;
        }
    }
    *polygon = out;
    return kept;
}

// Clips the incident face of other by the side planes of reference's
// face through its edges; the points left, counted.
fn clipFace(reference: Poly, face: u32, other: Poly, incident: u32,
            polygon: ptr<function, array<vec3<f32>, MANIFOLD_POINTS>>) -> u32 {
    var count = 0u;
    for (var j = polyFaceStart(other, incident); j < polyFaceStart(other, incident + 1u); j++) {
        (*polygon)[count] = polyVertex(other, polyLoop(other, j));
        count += 1u;
    }
    let n = polyPlane(reference, face).xyz;
    let first = polyFaceStart(reference, face);
    let corners = polyFaceStart(reference, face + 1u) - first;
    for (var j = 0u; j < corners && count > 0u; j++) {
        let p = polyVertex(reference, polyLoop(reference, first + j));
        let q = polyVertex(reference, polyLoop(reference, first + (j + 1u) % corners));
        let edge = q - p;
        var side = vec3<f32>(edge.y * n.z - edge.z * n.y, edge.z * n.x - edge.x * n.z,
                             edge.x * n.y - edge.y * n.x);
        let length = sqrt(dot3(side, side));
        if (!(length > 0.0)) {
            continue;
        }
        side /= length;
        count = clipPolygon(polygon, count, side, dot3(side, p), 1e-4 * sqrt(dot3(edge, edge)));
    }
    return count;
}

// Adds a body's contact at its point onA (in the world) of a polytope
// search, along n at depth.
fn polytopeContact(x: vec3<f32>, q: vec4<f32>, onA: vec3<f32>, n: vec3<f32>, depth: f32,
                   list: ptr<function, Contacts>) {
    addContact(list, contactAt(rotate(conjugate(q), onA - x), onA, n, depth));
}

// Adds the contact points of the face of one of a (the body's) and b most
// along the meet's normal clipped by the other's face most against it,
// nearer than the margin, as the core's FaceContacts and
// mvfxConvexManifold; how many. None where no face is within 45 degrees
// of the normal (edges crossing).
fn faceContacts(x: vec3<f32>, q: vec4<f32>, a: Poly, b: Poly, meet: Meet,
                list: ptr<function, Contacts>) -> u32 {
    var alongB = 0.0;
    var alongA = 0.0;
    let faceB = faceAlong(b, meet.normal, &alongB);
    let faceA = faceAlong(a, -meet.normal, &alongA);
    if (!(max(alongA, alongB) >= 0.7)) {
        return 0u;
    }
    // A face of b by a clear margin, as boxes' faces.
    let onB = alongB >= 0.98 * alongA;
    var reference = a;
    var other = b;
    if (onB) {
        reference = b;
        other = a;
    }
    let face = select(faceA, faceB, onB);
    let plane = polyPlane(reference, face);
    var along = 0.0;
    let incident = faceAlong(other, -plane.xyz, &along);
    var polygon = array<vec3<f32>, MANIFOLD_POINTS>();
    let clipped = clipFace(reference, face, other, incident, &polygon);
    let normal = select(-plane.xyz, plane.xyz, onB);
    var count = 0u;
    for (var i = 0u; i < clipped; i++) {
        let s = dot3(plane.xyz, polygon[i]) - plane.w;
        if (!(s < (*list).margin)) {
            continue;
        }
        count += 1u;
        let under = polygon[i] - s * plane.xyz;
        polytopeContact(x, q, select(under, polygon[i], onB), normal, s, list);
    }
    return count;
}

// A hull body's polytope at its pose.
fn hullPoly(x: vec3<f32>, q: vec4<f32>, s: Shape) -> Poly {
    return Poly(POLY_HULL, x, q, s.half, s.count, hullFaceCount());
}

// A hull kept out of a sphere or a box. Of a box, as two polytopes (the
// core's HullBoxContacts): their manifold, or their one meeting point. Of
// a sphere (HullSphereContacts): its point nearest the center, or for a
// center inside it its face nearest the center.
fn hullPolyContacts(x: vec3<f32>, q: vec4<f32>, s: Shape, c: Collider,
                    list: ptr<function, Contacts>) {
    let hull = hullPoly(x, q, s);
    let box = c.modes.x == COLLIDER_BOX;
    let center = c.center.xyz;
    var other = Poly(POLY_POINT, center, vec4<f32>(0.0, 0.0, 0.0, 1.0), vec3<f32>(0.0), 1u, 0u);
    if (box) {
        other = Poly(POLY_BOX, center, c.rotation, c.halfExtents.xyz, 8u, 6u);
    }
    var meet: Meet;
    if (!convexSearch(hull, other, (*list).margin, box, &meet)) {
        return;
    }
    if (box) {
        (*list).searches += select(0u, 1u, meet.capped);
        if (faceContacts(x, q, hull, other, meet, list) == 0u) {
            polytopeContact(x, q, meet.onA, meet.normal, meet.distance, list);
        }
        return;
    }
    let distance = meet.distance;
    var near = meet.onA;
    var n = vec3<f32>(0.0, 1.0, 0.0);
    var depth = distance - c.center.w;
    if (distance > 0.0) {
        n = (near - center) / distance;
    } else {
        // Pushed out through the face the center is nearest, against its
        // normal.
        var face = 0u;
        var above = 0.0;
        for (var f = 0u; f < hull.faces; f++) {
            let plane = polyPlane(hull, f);
            let off = dot3(plane.xyz, center) - plane.w;
            face = select(face, f, f == 0u || off > above);
            above = select(max(above, off), off, f == 0u);
        }
        let plane = polyPlane(hull, face);
        n = -plane.xyz;
        near = center - above * plane.xyz;
        depth = above - c.center.w;
    }
    polytopeContact(x, q, near, n, depth, list);
}
