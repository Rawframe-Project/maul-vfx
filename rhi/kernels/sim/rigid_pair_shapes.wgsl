// The sim kernel's rigid pairs' contacts of capsules and hulls (spliced
// into sim.wgsl beside sim/rigid_pairs.wgsl), as the core's
// mvfxGatherPairContacts: two capsules by their cores, two hulls as two
// polytopes.

// The share along p0 to p1 nearest q, held to the segment, as the core's
// NearestShare.
fn nearestShare(p0: vec3<f32>, p1: vec3<f32>, q: vec3<f32>) -> f32 {
    let d = p1 - p0;
    let square = dot3(d, d);
    return select(0.0, min(max(dot3(q - p0, d) / square, 0.0), 1.0), square > 0.0);
}

// The shares of two segments' nearest points, as the core's
// NearestShares (Ericson, "Real-Time Collision Detection", 2005, 5.1.9).
fn nearestShares(a0: vec3<f32>, a1: vec3<f32>, b0: vec3<f32>, b1: vec3<f32>) -> vec2<f32> {
    let d1 = a1 - a0;
    let d2 = b1 - b0;
    let r = a0 - b0;
    let e1 = dot3(d1, d1);
    let e2 = dot3(d2, d2);
    let f = dot3(d2, r);
    let c = dot3(d1, r);
    let m = dot3(d1, d2);
    let denominator = e1 * e2 - m * m;
    var s = select(0.0, min(max((m * f - c * e2) / denominator, 0.0), 1.0), denominator > 0.0);
    var t = select(0.0, (m * s + f) / e2, e2 > 0.0);
    if (t < 0.0 || t > 1.0) {
        t = min(max(t, 0.0), 1.0);
        s = select(0.0, min(max((m * t - c) / e1, 0.0), 1.0), e1 > 0.0);
    }
    return vec2<f32>(s, t);
}

// Two capsules, as the core's CapsulePair: their cores' nearest points,
// and each core's ends with the other's nearest, each pair of shares once
// (CoreToCore each).
fn capsulePair(x: vec3<f32>, q: vec4<f32>, sa: Shape, xb: vec3<f32>, qb: vec4<f32>, sb: Shape,
               list: ptr<function, Contacts>) {
    let a0 = x + rotate(q, coreAt(sa, 0.0));
    let a1 = x + rotate(q, coreAt(sa, 1.0));
    let b0 = xb + rotate(qb, coreAt(sb, 0.0));
    let b1 = xb + rotate(qb, coreAt(sb, 1.0));
    var shares = array<vec2<f32>, 5>(nearestShares(a0, a1, b0, b1),
                                     vec2<f32>(0.0, nearestShare(b0, b1, a0)),
                                     vec2<f32>(1.0, nearestShare(b0, b1, a1)),
                                     vec2<f32>(nearestShare(a0, a1, b0), 0.0),
                                     vec2<f32>(nearestShare(a0, a1, b1), 1.0));
    for (var i = 0u; i < 5u; i++) {
        var fresh = true;
        for (var j = 0u; fresh && j < i; j++) {
            fresh = abs(shares[i].x - shares[j].x) > 1e-4 || abs(shares[i].y - shares[j].y) > 1e-4;
        }
        if (!fresh) {
            continue;
        }
        let pa = a0 + shares[i].x * (a1 - a0);
        let pb = b0 + shares[i].y * (b1 - b0);
        var n = vec3<f32>(0.0);
        let distance = wayFrom(pb, pa, &n);
        let world = pa - sa.radius * n;
        let point = rotate(conjugate(q), world - x);
        addContact(list, contactAt(point, world, n, distance - sa.radius - sb.radius));
    }
}

// Two hulls, as the core's PolytopeContacts: their manifold, or their one
// meeting point.
fn hullPair(x: vec3<f32>, q: vec4<f32>, sa: Shape, xb: vec3<f32>, qb: vec4<f32>, sb: Shape,
            list: ptr<function, Contacts>) {
    let hull = hullPoly(x, q, sa);
    let other = hullPoly(xb, qb, sb);
    var meet: Meet;
    if (!convexSearch(hull, other, (*list).margin, true, &meet)) {
        return;
    }
    (*list).searches += select(0u, 1u, meet.capped);
    if (faceContacts(x, q, hull, other, meet, list) == 0u) {
        polytopeContact(x, q, meet.onA, meet.normal, meet.distance, list);
    }
}
