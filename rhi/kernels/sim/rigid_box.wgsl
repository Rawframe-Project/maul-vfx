// The sim kernel's rigid boxes kept out of box colliders (spliced into
// sim.wgsl), as the core's mvfxBoxBoxContacts (src/rigid_box.c): the
// separating axes (Gottschalk, Lin and Manocha, "OBBTree", SIGGRAPH 1996),
// then a face clipped by the other's (as ODE's dBoxBox) or two edges'
// nearest points.

// The share of a clipping box's reach within which a point is on its
// plane.
const CLIP_TOLERANCE: f32 = 1e-4;

// A box body in a box collider's frame: the collider's half sizes, the
// body's, its center and its axes.
struct BoxPair {
    a: vec3<f32>,
    h: vec3<f32>,
    t: vec3<f32>,
    axes: array<vec3<f32>, 3>,
}

// A polygon of at most eight points.
struct Polygon {
    points: array<vec3<f32>, 8>,
    count: u32,
}

// The gap between the boxes along unit l (negative where they overlap).
fn separation(p: ptr<function, BoxPair>, l: vec3<f32>) -> f32 {
    var reach = 0.0;
    for (var k = 0u; k < 3u; k++) {
        reach += (*p).a[k] * abs(l[k]) + (*p).h[k] * abs(dot3((*p).axes[k], l));
    }
    return abs(dot3((*p).t, l)) - reach;
}

// The axis the boxes overlap least along (the collider's faces 0 to 2,
// the body's 3 to 5, edge pairs 6 on), unit and from the collider toward
// the body; false when one parts them by more than margin. Edges win only
// by a clear margin, so resting faces keep face contacts.
fn leastAxis(p: ptr<function, BoxPair>, margin: f32, n: ptr<function, vec3<f32>>,
             axis: ptr<function, u32>) -> bool {
    var best = -LARGEST;
    for (var i = 0u; i < 6u; i++) {
        var way = vec3<f32>(0.0);
        way[i % 3u] = 1.0;
        if (i >= 3u) {
            way = (*p).axes[i - 3u];
        }
        let gap = separation(p, way);
        if (gap > margin) {
            return false;
        }
        if (gap > best) {
            best = gap;
            *axis = i;
            *n = way;
        }
    }
    let face = best;
    for (var i = 0u; i < 9u; i++) {
        var e = vec3<f32>(0.0);
        e[i / 3u] = 1.0;
        var l = cross(e, (*p).axes[i % 3u]);
        let square = dot3(l, l);
        if (!(square > 1e-10) || !(square <= LARGEST)) {
            continue;
        }
        l /= sqrt(square);
        let gap = separation(p, l);
        if (gap > margin) {
            return false;
        }
        if (gap > 0.95 * face && gap > best) {
            best = gap;
            *axis = 6u + i;
            *n = l;
        }
    }
    *n *= select(1.0, -1.0, dot3((*p).t, *n) < 0.0);
    return true;
}

// Clips a polygon to dot(x, l) <= limit, the clipping box reaching reach
// along l, as the core's Clip: a point on the plane (within the
// tolerance) is kept and no crossing is added beside it.
fn clip(polygon: ptr<function, Polygon>, l: vec3<f32>, limit: f32, reach: f32) {
    var out = Polygon();
    let on = CLIP_TOLERANCE * reach;
    let count = (*polygon).count;
    for (var i = 0u; i < count; i++) {
        let a = (*polygon).points[i];
        let b = (*polygon).points[(i + 1u) % count];
        let da = dot3(a, l) - limit;
        let db = dot3(b, l) - limit;
        if (da <= on && out.count < 8u) {
            out.points[out.count] = a;
            out.count += 1u;
        }
        if (((da < -on && db > on) || (da > on && db < -on)) && out.count < 8u) {
            let f = da / (da - db);
            out.points[out.count] = a + f * (b - a);
            out.count += 1u;
        }
    }
    *polygon = out;
}

// A box's face (center, two half spans) as four corners.
fn faceOf(center: vec3<f32>, u: vec3<f32>, v: vec3<f32>) -> Polygon {
    var out = Polygon();
    out.points[0] = center + u + v;
    out.points[1] = center - u + v;
    out.points[2] = center - u - v;
    out.points[3] = center + u - v;
    out.count = 4u;
    return out;
}

// Adds the contact of the body's point at p and the collider's surface
// point s (both in the collider's frame) along n there.
fn pairContact(x: vec3<f32>, q: vec4<f32>, c: Collider, p: vec3<f32>, s: vec3<f32>,
               n: vec3<f32>, list: ptr<function, Contacts>) {
    let world = rotate(c.rotation, p) + c.center.xyz;
    let n0 = rotate(c.rotation, n);
    let point = rotate(conjugate(q), world - x);
    addContact(list, contactAt(point, world, n0, dot3(p - s, n)));
}

// The body's face most against n clipped by the collider's face k.
fn bodyFaceOnCollider(x: vec3<f32>, q: vec4<f32>, c: Collider, p: ptr<function, BoxPair>,
                      n: vec3<f32>, k: u32, list: ptr<function, Contacts>) {
    var j = 0u;
    for (var m = 1u; m < 3u; m++) {
        j = select(j, m, abs(dot3((*p).axes[m], n)) > abs(dot3((*p).axes[j], n)));
    }
    let side = select(1.0, -1.0, dot3((*p).axes[j], n) > 0.0);
    let center = (*p).t + side * (*p).h[j] * (*p).axes[j];
    let u = (*p).h[(j + 1u) % 3u] * (*p).axes[(j + 1u) % 3u];
    let v = (*p).h[(j + 2u) % 3u] * (*p).axes[(j + 2u) % 3u];
    var polygon = faceOf(center, u, v);
    for (var m = 1u; m < 3u; m++) {
        let at = (k + m) % 3u;
        var l = vec3<f32>(0.0);
        l[at] = 1.0;
        clip(&polygon, l, (*p).a[at], (*p).a[at]);
        l[at] = -1.0;
        clip(&polygon, l, (*p).a[at], (*p).a[at]);
    }
    for (var i = 0u; i < polygon.count; i++) {
        let point = polygon.points[i];
        let depth = dot3(point, n) - (*p).a[k];
        pairContact(x, q, c, point, point - depth * n, n, list);
    }
}

// The collider's face most along n clipped by the body's face j.
fn colliderFaceOnBody(x: vec3<f32>, q: vec4<f32>, c: Collider, p: ptr<function, BoxPair>,
                      n: vec3<f32>, j: u32, list: ptr<function, Contacts>) {
    var k = 0u;
    for (var m = 1u; m < 3u; m++) {
        k = select(k, m, abs(n[m]) > abs(n[k]));
    }
    var center = vec3<f32>(0.0);
    var u = vec3<f32>(0.0);
    var v = vec3<f32>(0.0);
    center[k] = select(-(*p).a[k], (*p).a[k], n[k] >= 0.0);
    u[(k + 1u) % 3u] = (*p).a[(k + 1u) % 3u];
    v[(k + 2u) % 3u] = (*p).a[(k + 2u) % 3u];
    var polygon = faceOf(center, u, v);
    for (var m = 1u; m < 3u; m++) {
        let l = (*p).axes[(j + m) % 3u];
        let reach = (*p).h[(j + m) % 3u];
        clip(&polygon, l, dot3((*p).t, l) + reach, reach);
        clip(&polygon, -l, dot3((*p).t, -l) + reach, reach);
    }
    for (var i = 0u; i < polygon.count; i++) {
        let point = polygon.points[i];
        let g = dot3(point - (*p).t, n) + (*p).h[j];
        pairContact(x, q, c, point - g * n, point, n, list);
    }
}

// The nearest points of the collider's edge along its axis i and the
// body's along its axis j that reach furthest toward each other along n.
fn edgeContact(x: vec3<f32>, q: vec4<f32>, c: Collider, p: ptr<function, BoxPair>, n: vec3<f32>,
               i: u32, j: u32, list: ptr<function, Contacts>) {
    var pa = vec3<f32>(0.0);
    var pb = (*p).t;
    var da = vec3<f32>(0.0);
    da[i] = 1.0;
    for (var k = 0u; k < 3u; k++) {
        pa[k] = select(select(-(*p).a[k], (*p).a[k], n[k] >= 0.0), 0.0, k == i);
    }
    for (var m = 0u; m < 3u; m++) {
        let way = select((*p).h[m], -(*p).h[m], dot3((*p).axes[m], n) >= 0.0);
        pb += select(way, 0.0, m == j) * (*p).axes[m];
    }
    let db = (*p).axes[j];
    let r = pb - pa;
    let e = dot3(da, db);
    let denominator = 1.0 - e * e;
    var s = 0.0;
    if (denominator > 1e-6) {
        s = (dot3(r, da) - e * dot3(r, db)) / denominator;
    }
    s = min(max(s, -(*p).a[i]), (*p).a[i]);
    let u = min(max(s * e - dot3(r, db), -(*p).h[j]), (*p).h[j]);
    s = min(max(dot3(r, da) + u * e, -(*p).a[i]), (*p).a[i]);
    pairContact(x, q, c, pb + u * db, pa + s * da, n, list);
}

// A box body kept out of a box collider: up to eight contacts where a face
// meets a face, one where edges meet.
fn boxBoxContacts(x: vec3<f32>, q: vec4<f32>, s: Shape, c: Collider,
                  list: ptr<function, Contacts>) {
    var p = BoxPair();
    p.a = c.halfExtents.xyz;
    p.h = s.half;
    let back = conjugate(c.rotation);
    p.t = rotate(back, x - c.center.xyz);
    for (var j = 0u; j < 3u; j++) {
        var e = vec3<f32>(0.0);
        e[j] = 1.0;
        p.axes[j] = rotate(back, rotate(q, e));
    }
    var n = vec3<f32>(0.0);
    var axis = 0u;
    if (!leastAxis(&p, (*list).margin, &n, &axis)) {
        return;
    }
    if (axis < 3u) {
        bodyFaceOnCollider(x, q, c, &p, n, axis, list);
    } else if (axis < 6u) {
        colliderFaceOnBody(x, q, c, &p, n, axis - 3u, list);
    } else {
        edgeContact(x, q, c, &p, n, (axis - 6u) / 3u, (axis - 6u) % 3u, list);
    }
}
