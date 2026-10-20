// The sim kernel's rigid particles' contacts with the world (spliced into
// sim.wgsl), as the core's Meet for a body alone (src/rigid.c,
// src/rigid_body.c, src/rigid_contact.c): each substep, a body's contacts
// at its predicted pose with its emitter's colliders and the instance's
// (planes, spheres and boxes, outside or inside), at most MOST_CONTACTS,
// the shallowest past them dropped and counted; then, touched, its
// prediction as symplectic Euler's, three passes stabilizing its start
// pose, three pressing it out (static friction by patches from the
// second), its velocities from its pose's change, eight passes of
// restitution with dynamic friction, and its strike. Every step of it
// moves the body alone, so a body is an invocation's, in the core's
// order. Spheres, capsules (a capsule's core between its two end points)
// and boxes (their corners; kept out of a box by sim/rigid_box.wgsl).

const CONTACT_MARGIN: f32 = 0.02;
// The share of a capsule's reach by which its core's deepest point must lie
// deeper than both its ends to be met.
const DEEPEST_MARGIN: f32 = 1e-4;
const MOST_CONTACTS: u32 = 8u;
const POSITION_PASSES: u32 = 3u;
const RESTITUTION_PASSES: u32 = 8u;

// A body's contact: its point in its frame, the surface's point and unit
// normal in the world, its depth along the normal; its normal multipliers
// summed over the position passes, its normal speed at the predicted pose,
// its restitution's impulses summed; on a patch's first contact its static
// friction's multipliers, its dynamic friction's impulses, against its
// turning likewise; the collider it meets (8 an index, a box's face added
// when kept inside it).
struct Contact {
    body: vec3<f32>,
    surface: vec3<f32>,
    normal: vec3<f32>,
    depth: f32,
    lambda: f32,
    speed: f32,
    applied: f32,
    rubbed: f32,
    slowed: f32,
    twisted: f32,
    spun: f32,
    collider: u32,
}

// A body's contacts as gathered: at most MOST_CONTACTS, those past them
// counted; the collider those being added meet; how far apart a point may
// be and still be a contact.
struct Contacts {
    items: array<Contact, 8>,
    count: u32,
    capped: u32,
    collider: u32,
    margin: f32,
}

// A body's shape at its size: its kind, its points (a sphere's center,
// a capsule's core's ends, a box's corners), its radius (none for a box),
// a capsule's half core along Y or a box's half sizes.
struct Shape {
    kind: u32,
    count: u32,
    radius: f32,
    half: vec3<f32>,
}

// A body as its solve holds it (the core's mvfxBody): its pose, velocity
// and angular velocity, its start pose, its inverse mass and inverse
// inertia about its own axes.
struct Solid {
    x: vec3<f32>,
    v: vec3<f32>,
    w: vec3<f32>,
    x0: vec3<f32>,
    q: vec4<f32>,
    q0: vec4<f32>,
    inverseMass: f32,
    inverse: array<f32, 6>,
}

// A patch of a body's contacts with one collider: their center on the body
// in its frame and normal weighted by their normal multipliers, the
// multipliers' sum and their impulses' over the substep, the patch's
// radius, its contacts' offsets from the body's center in the world.
struct Patch {
    point: vec3<f32>,
    n: vec3<f32>,
    load: f32,
    pressed: f32,
    radius: f32,
    spread: array<vec3<f32>, 8>,
    spreadCount: u32,
}

fn shapeOf(size: vec3<f32>) -> Shape {
    let a = abs(size);
    var s = Shape(params.rigid.x, 1u, 0.5 * a.x, vec3<f32>(0.0));
    if (s.kind == RIGID_CAPSULE) {
        s.half.y = 0.5 * max(a.y - 2.0 * s.radius, 0.0);
        s.count = 2u;
    } else if (s.kind == RIGID_BOX) {
        s.radius = 0.0;
        s.count = 8u;
        s.half = 0.5 * a;
    }
    return s;
}

// A shape's point i in its frame, as the core's mvfxShapePoint.
fn shapePoint(s: Shape, i: u32) -> vec3<f32> {
    if (s.kind == RIGID_BOX) {
        // Corner i's bit k says which side along axis k.
        let bits = (vec3<u32>(i) >> vec3<u32>(0u, 1u, 2u)) & vec3<u32>(1u);
        return select(-s.half, s.half, bits != vec3<u32>(0u));
    }
    return vec3<f32>(0.0, select(-s.half.y, s.half.y, i == 0u), 0.0) *
           select(0.0, 1.0, s.kind == RIGID_CAPSULE);
}

fn conjugate(q: vec4<f32>) -> vec4<f32> {
    return vec4<f32>(-q.xyz, q.w);
}

// The quaternion product a b, as the core's mvfxMultiplyRotations.
fn multiplyRotations(a: vec4<f32>, b: vec4<f32>) -> vec4<f32> {
    return vec4<f32>(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                     a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                     a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                     a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

// Adds a contact nearer than the margin, as the core's mvfxAddContact:
// past the most it takes the shallowest's place if deeper, counted.
fn addContact(list: ptr<function, Contacts>, c: Contact) {
    if (!(c.depth < (*list).margin)) {
        return;
    }
    var added = c;
    added.collider = (*list).collider;
    if ((*list).count < MOST_CONTACTS) {
        (*list).items[(*list).count] = added;
        (*list).count += 1u;
        return;
    }
    (*list).capped += 1u;
    var shallow = 0u;
    for (var j = 1u; j < (*list).count; j++) {
        shallow = select(shallow, j, (*list).items[j].depth > (*list).items[shallow].depth);
    }
    if (c.depth < (*list).items[shallow].depth) {
        (*list).items[shallow] = added;
    }
}

// A contact of the body's point (in its frame, its place world) meeting a
// surface along unit n at depth.
fn contactAt(point: vec3<f32>, world: vec3<f32>, n: vec3<f32>, depth: f32) -> Contact {
    var c = Contact();
    c.body = point;
    c.normal = n;
    c.surface = world - depth * n;
    c.depth = depth;
    return c;
}

// The world place of the body's point at the shape's surface along way,
// and that point in the body's frame.
fn surfacePoint(x: vec3<f32>, q: vec4<f32>, s: Shape, point: vec3<f32>, way: vec3<f32>,
                body: ptr<function, vec3<f32>>) -> vec3<f32> {
    *body = point + s.radius * rotate(conjugate(q), way);
    return x + rotate(q, point) + s.radius * way;
}

fn planeContacts(x: vec3<f32>, q: vec4<f32>, s: Shape, center: vec3<f32>, normal: vec3<f32>,
                 list: ptr<function, Contacts>) {
    for (var i = 0u; i < s.count; i++) {
        var body = vec3<f32>(0.0);
        let world = surfacePoint(x, q, s, shapePoint(s, i), -normal, &body);
        addContact(list, contactAt(body, world, normal, dot3(world - center, normal)));
    }
}

// The unit way from a to b, or up where they meet; and their distance.
fn wayFrom(a: vec3<f32>, b: vec3<f32>, way: ptr<function, vec3<f32>>) -> f32 {
    let d = b - a;
    let distance = sqrt(dot3(d, d));
    if (!(distance > 0.0)) {
        *way = vec3<f32>(0.0, 1.0, 0.0);
        return 0.0;
    }
    *way = d / distance;
    return distance;
}

// Keeping the shape's points within a sphere.
fn insideContacts(x: vec3<f32>, q: vec4<f32>, s: Shape, c: Collider,
                  list: ptr<function, Contacts>) {
    for (var i = 0u; i < s.count; i++) {
        let point = shapePoint(s, i);
        var out = vec3<f32>(0.0);
        let distance = wayFrom(c.center.xyz, x + rotate(q, point), &out);
        var body = vec3<f32>(0.0);
        let world = surfacePoint(x, q, s, point, out, &body);
        addContact(list, contactAt(body, world, -out, c.center.w - distance - s.radius));
    }
}

// A box's signed distance at a point in its frame (negative within), and
// the unit way out of it there.
fn boxDistance(h: vec3<f32>, p: vec3<f32>, way: ptr<function, vec3<f32>>) -> f32 {
    let near = min(max(p, -h), h);
    if (any(near != p)) {
        return wayFrom(near, p, way);
    }
    var face = 0u;
    for (var k = 1u; k < 3u; k++) {
        face = select(face, k, h[k] - abs(p[k]) < h[face] - abs(p[face]));
    }
    *way = vec3<f32>(0.0);
    (*way)[face] = select(-1.0, 1.0, p[face] >= 0.0);
    return -(h[face] - abs(p[face]));
}

fn coreAt(s: Shape, t: f32) -> vec3<f32> {
    return vec3<f32>(0.0, s.half.y * (2.0 * t - 1.0), 0.0);
}

// The signed distance of the core's point at t from a box collider, its
// world place and the way out in the world.
fn coreDistance(x: vec3<f32>, q: vec4<f32>, s: Shape, c: Collider, t: f32,
                world: ptr<function, vec3<f32>>, n: ptr<function, vec3<f32>>) -> f32 {
    *world = rotate(q, coreAt(s, t)) + x;
    var way = vec3<f32>(0.0);
    let distance = boxDistance(c.halfExtents.xyz,
                               rotate(conjugate(c.rotation), *world - c.center.xyz), &way);
    *n = rotate(c.rotation, way);
    return distance;
}

fn coreContact(x: vec3<f32>, q: vec4<f32>, s: Shape, c: Collider, t: f32,
               list: ptr<function, Contacts>) {
    var world = vec3<f32>(0.0);
    var n = vec3<f32>(0.0);
    let distance = coreDistance(x, q, s, c, t, &world, &n);
    let inward = -s.radius * n;
    let point = coreAt(s, t) + rotate(conjugate(q), inward);
    addContact(list, contactAt(point, world + inward, n, distance - s.radius));
}

// A sphere or a capsule kept out of a box: its core's ends and its
// deepest point between (a ternary search), and where the core crosses the
// box's faces' planes.
fn coreBoxContacts(x: vec3<f32>, q: vec4<f32>, s: Shape, c: Collider,
                   list: ptr<function, Contacts>) {
    if (s.kind == RIGID_SPHERE) {
        coreContact(x, q, s, c, 0.5, list);
        return;
    }
    var low = 0.0;
    var high = 1.0;
    var world = vec3<f32>(0.0);
    var n = vec3<f32>(0.0);
    for (var round = 0u; round < 32u; round++) {
        let t0 = low + (high - low) / 3.0;
        let t1 = high - (high - low) / 3.0;
        let lower = coreDistance(x, q, s, c, t0, &world, &n) <=
                    coreDistance(x, q, s, c, t1, &world, &n);
        high = select(high, t1, lower);
        low = select(t0, low, lower);
    }
    let deepest = 0.5 * (low + high);
    // Met only where clearly deeper than both ends, as the core's: along a
    // face the least distance is at an end.
    let endDepth = min(coreDistance(x, q, s, c, 0.0, &world, &n),
                       coreDistance(x, q, s, c, 1.0, &world, &n));
    let clear = coreDistance(x, q, s, c, deepest, &world, &n) <
                endDepth - DEEPEST_MARGIN * (s.half.y + s.radius);
    coreContact(x, q, s, c, 0.0, list);
    coreContact(x, q, s, c, 1.0, list);
    if (clear && deepest > 1e-3 && deepest < 1.0 - 1e-3) {
        coreContact(x, q, s, c, deepest, list);
    }
    var ends = array<vec3<f32>, 2>();
    for (var e = 0u; e < 2u; e++) {
        _ = coreDistance(x, q, s, c, f32(e), &world, &n);
        ends[e] = rotate(conjugate(c.rotation), world - c.center.xyz);
    }
    for (var f = 0u; f < 6u; f++) {
        let k = f / 2u;
        let span = ends[1][k] - ends[0][k];
        let plane = select(-c.halfExtents[k], c.halfExtents[k], f % 2u == 0u);
        var at = -1.0;
        if (span != 0.0) {
            at = (plane - ends[0][k]) / span;
        }
        if (at > 1e-3 && at < 1.0 - 1e-3) {
            coreContact(x, q, s, c, at, list);
        }
    }
}

// A shape kept inside a box: each face a plane, each its own patch.
fn insideBoxContacts(x: vec3<f32>, q: vec4<f32>, s: Shape, c: Collider,
                     list: ptr<function, Contacts>) {
    let area = (*list).collider;
    for (var f = 0u; f < 6u; f++) {
        let k = f / 2u;
        var axis = vec3<f32>(0.0);
        axis[k] = select(-1.0, 1.0, f % 2u == 0u);
        let out = rotate(c.rotation, axis);
        (*list).collider = area + f;
        planeContacts(x, q, s, c.center.xyz + c.halfExtents[k] * out, -out, list);
    }
    (*list).collider = area;
}

// The box's point nearest a sphere's center (in its frame) and the way out
// it is pushed; for a center inside, the nearest face's.
fn boxNearest(s: Shape, center: vec3<f32>, point: ptr<function, vec3<f32>>,
              way: ptr<function, vec3<f32>>) -> f32 {
    *point = min(max(center, -s.half), s.half);
    if (any(*point != center)) {
        return wayFrom(center, *point, way);
    }
    var face = 0u;
    for (var k = 1u; k < 3u; k++) {
        face = select(face, k, s.half[k] - abs(center[k]) < s.half[face] - abs(center[face]));
    }
    let side = select(-1.0, 1.0, center[face] >= 0.0);
    (*point)[face] = side * s.half[face];
    *way = vec3<f32>(0.0);
    (*way)[face] = -side;
    return -(s.half[face] - abs(center[face]));
}

// Keeping the shape out of a sphere: by the nearest point of a box, or of
// a sphere's or a capsule's core.
fn outsideContacts(x: vec3<f32>, q: vec4<f32>, s: Shape, c: Collider,
                   list: ptr<function, Contacts>) {
    let center = rotate(conjugate(q), c.center.xyz - x);
    var point = vec3<f32>(0.0, min(max(center.y, -s.half.y), s.half.y), 0.0);
    var local = vec3<f32>(0.0);
    var distance = 0.0;
    if (s.kind == RIGID_BOX) {
        distance = boxNearest(s, center, &point, &local);
    } else {
        distance = wayFrom(center, point, &local) - s.radius;
        point -= s.radius * local;
    }
    addContact(list, contactAt(point, x + rotate(q, point), rotate(q, local),
                               distance - c.center.w));
}

// A capsule's spans between its core's samples, at most its radius long
// (at most eight); none for a sphere.
fn coreSpans(s: Shape) -> u32 {
    if (s.kind != RIGID_CAPSULE || !(s.radius > 0.0)) {
        return 0u;
    }
    return u32(ceil(min(2.0 * s.half.y / s.radius, 8.0)));
}

// The most corners a shape meets a grid by: a capsule's core's points.
const MOST_CORNERS: u32 = 9u;

// How many points (in its frame) a shape meets a grid by, as spheres of
// its radius, and how many of them are corners, met plainly: a sphere's
// center; a capsule's core, its points at most its radius apart; a box's
// eight corners, then its faces' six centers.
fn sampleCount(s: Shape, corners: ptr<function, u32>) -> u32 {
    if (s.kind == RIGID_BOX) {
        *corners = 8u;
        return 14u;
    }
    *corners = coreSpans(s) + 1u;
    return *corners;
}

// A shape's sample i: a box's corners (bit k of i set for +k), then its
// faces' centers (8 + 2k for +k, 9 + 2k for -k); a core's points in
// order.
fn sampleOf(s: Shape, i: u32) -> vec3<f32> {
    if (s.kind == RIGID_BOX && i < 8u) {
        return shapePoint(s, i);
    }
    if (s.kind == RIGID_BOX) {
        let k = (i - 8u) / 2u;
        var out = vec3<f32>(0.0);
        out[k] = select(-s.half[k], s.half[k], (i - 8u) % 2u == 0u);
        return out;
    }
    let spans = coreSpans(s);
    return coreAt(s, select(0.5, f32(i) / f32(spans), spans > 0u));
}

// A grid collider's distance at a place and its unit normal there, as
// the core's mvfxFieldDistance: LARGEST outside its box; a heightfield's
// gap along its Y times the slope's cosine.
fn fieldDistance(c: Collider, world: vec3<f32>, n: ptr<function, vec3<f32>>) -> f32 {
    let local = rotate(conjugate(c.rotation), world - c.center.xyz);
    let f = gridField(c, local);
    if (!(f < LARGEST)) {
        return LARGEST;
    }
    let g = gradientAt(c, local);
    *n = rotate(c.rotation, g);
    return select(f, f * abs(g.y), c.modes.x == COLLIDER_HEIGHTFIELD);
}

// What samples meet, as the core's Probe: a grid collider's field, or
// the depth image seen from the start pose x0, q0.
struct Probe {
    c: Collider,
    depth: bool,
    x0: vec3<f32>,
    q0: vec4<f32>,
}

// Whether a sample meets the probe's surface, and its contact: of the
// shape's radius, along the normal at its center's distance. The depth
// image is met along the sample's way over the substep, within twice the
// shape's radius (the core's DepthContacts: off the camera's axis the
// image's depth is longer than across the surface), the distance across
// the surface from its point grown by that radius.
fn probeSample(x: vec3<f32>, q: vec4<f32>, s: Shape, probe: Probe, sample: vec3<f32>,
               out: ptr<function, Contact>) -> bool {
    let world = rotate(q, sample) + x;
    var n = vec3<f32>(0.0, 1.0, 0.0);
    var distance = LARGEST;
    if (probe.depth) {
        let radius = 2.0 * s.radius;
        var p: Projected;
        var surface = vec3<f32>(0.0);
        if (firstMeeting(rotate(probe.q0, sample) + probe.x0, world, radius, &p, &surface) < 0.0) {
            return false;
        }
        n = depthNormal(p);
        distance = dot3(world - (surface + radius * n), n) + radius;
    } else {
        distance = fieldDistance(probe.c, world, &n);
        if (!(distance < LARGEST)) {
            return false;
        }
    }
    let inward = -s.radius * n;
    *out = contactAt(sample + rotate(conjugate(q), inward), world + inward, n,
                     distance - s.radius);
    return true;
}

// A box's corners' depths and whether each met the grid.
struct Corners {
    depths: array<f32, MOST_CORNERS>,
    found: array<bool, MOST_CORNERS>,
}

// Whether a box's face center i at depth is met: only where the surface
// bulges under it, deeper than each corner of its face found.
fn centerMet(corners: ptr<function, Corners>, i: u32, depth: f32) -> bool {
    let k = (i - 8u) / 2u;
    let side = select(0u, 1u, (i - 8u) % 2u == 0u);
    var met = true;
    for (var c = 0u; met && c < 8u; c++) {
        let onFace = ((c >> k) & 1u) == side;
        met = !onFace || !(*corners).found[c] || depth < (*corners).depths[c];
    }
    return met;
}

// Adds each of a box's edges' deepest inner point (a quarter, a half and
// three quarters along) where the surface bulges under it, deeper than
// each of its corners found.
fn edgeContacts(x: vec3<f32>, q: vec4<f32>, s: Shape, probe: Probe,
                corners: ptr<function, Corners>, list: ptr<function, Contacts>) {
    for (var i = 0u; i < 8u; i++) {
        for (var k = 0u; k < 3u; k++) {
            let j = i | (1u << k);
            if (j == i) {
                continue;
            }
            let a = sampleOf(s, i);
            let b = sampleOf(s, j);
            var deepest = Contact();
            deepest.depth = LARGEST;
            var met = false;
            for (var quarter = 1u; quarter <= 3u; quarter++) {
                var contact = Contact();
                let point = a + 0.25 * f32(quarter) * (b - a);
                let hit = probeSample(x, q, s, probe, point, &contact) &&
                          contact.depth < deepest.depth;
                if (hit) {
                    deepest = contact;
                }
                met = met || hit;
            }
            met = met && (!(*corners).found[i] || deepest.depth < (*corners).depths[i]);
            met = met && (!(*corners).found[j] || deepest.depth < (*corners).depths[j]);
            if (met) {
                addContact(list, deepest);
            }
        }
    }
}

// A heightfield, a distance grid or the depth image, by the probe at the
// samples in order, corners first, as the core's SampleContacts: only the
// corners' depths are kept, for their faces' centers and their edges.
fn sampleContacts(x: vec3<f32>, q: vec4<f32>, s: Shape, probe: Probe,
                  list: ptr<function, Contacts>) {
    var corners = Corners();
    var cornerCount = 0u;
    let count = sampleCount(s, &cornerCount);
    for (var i = 0u; i < count; i++) {
        var contact = Contact();
        let hit = probeSample(x, q, s, probe, sampleOf(s, i), &contact);
        if (i < cornerCount) {
            corners.found[i] = hit;
            corners.depths[i] = select(LARGEST, contact.depth, hit);
        }
        if (hit && (i < cornerCount || centerMet(&corners, i, contact.depth))) {
            addContact(list, contact);
        }
    }
    if (s.kind == RIGID_BOX) {
        edgeContacts(x, q, s, probe, &corners, list);
    }
}

// A body's contacts with the colliders and the depth image at its pose x,
// q from its start pose x0, q0, as the core's mvfxGatherBodyContacts, its
// reach's share the margin (no scene plane: the GPU runs no scene
// queries).
fn gatherContacts(x: vec3<f32>, q: vec4<f32>, x0: vec3<f32>, q0: vec4<f32>, s: Shape,
                  reach: f32) -> Contacts {
    var list = Contacts();
    list.margin = CONTACT_MARGIN * reach;
    for (var i = 0u; i < params.collisionHead.x; i++) {
        let c = params.colliders[i];
        list.collider = 8u * i;
        let inside = c.modes.y != 0u;
        if (c.modes.x == COLLIDER_PLANE) {
            planeContacts(x, q, s, c.center.xyz, c.normal.xyz, &list);
        } else if (c.modes.x == COLLIDER_SPHERE && inside) {
            insideContacts(x, q, s, c, &list);
        } else if (c.modes.x == COLLIDER_SPHERE) {
            outsideContacts(x, q, s, c, &list);
        } else if (c.modes.x == COLLIDER_BOX && inside) {
            insideBoxContacts(x, q, s, c, &list);
        } else if (c.modes.x == COLLIDER_BOX && s.kind == RIGID_BOX) {
            boxBoxContacts(x, q, s, c, &list);
        } else if (c.modes.x == COLLIDER_BOX) {
            coreBoxContacts(x, q, s, c, &list);
        } else {
            sampleContacts(x, q, s, Probe(c, false, x0, q0), &list);
        }
    }
    if (params.depth.head.x != 0u) {
        list.collider = 8u * params.collisionHead.x + 8u;
        sampleContacts(x, q, s, Probe(params.colliders[0], true, x0, q0), &list);
    }
    return list;
}

// The inverse inertia times v, in the world.
fn inverseInertiaTimes(b: Solid, v: vec3<f32>) -> vec3<f32> {
    return rotate(b.q, symmetricTimes(b.inverse, rotate(conjugate(b.q), v)));
}

fn bodyWeight(b: Solid, r: vec3<f32>, n: vec3<f32>) -> f32 {
    let c = cross(r, n);
    return b.inverseMass + dot3(c, inverseInertiaTimes(b, c));
}

// Turns q by a turn vector to first order (q + [turn, 0] q / 2),
// normalized, as the core's TurnBy of a body.
fn turnFirst(q: vec4<f32>, turn: vec3<f32>) -> vec4<f32> {
    let out = q + 0.5 * multiplyRotations(vec4<f32>(turn, 0.0), q);
    return out / sqrt(out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w);
}

fn bodyPush(b: ptr<function, Solid>, r: vec3<f32>, p: vec3<f32>, both: bool) {
    let turn = inverseInertiaTimes(*b, cross(r, p));
    (*b).q = turnFirst((*b).q, turn);
    (*b).x += p * (*b).inverseMass;
    if (both) {
        (*b).q0 = turnFirst((*b).q0, turn);
        (*b).x0 += p * (*b).inverseMass;
    }
}

fn bodyKick(b: ptr<function, Solid>, r: vec3<f32>, j: vec3<f32>) {
    let turn = inverseInertiaTimes(*b, cross(r, j));
    (*b).v += j * (*b).inverseMass;
    (*b).w += turn;
}

fn pointVelocity(b: Solid, r: vec3<f32>) -> vec3<f32> {
    return cross(b.w, r) + b.v;
}

// Where a friction impulse along the patch acts on the body at offset r,
// as the core's Lever.
fn lever(r: vec3<f32>, n: vec3<f32>, balance: f32, impulse: f32) -> vec3<f32> {
    let along = dot3(r, n);
    let tipping = impulse * abs(along);
    var level = select(1.0, 0.0, tipping > 0.0);
    if (balance > 0.0) {
        level = min(max((1.05 * balance - tipping) / (0.1 * balance), 0.0), 1.0);
    }
    return r - level * along * n;
}

// How far a patch reaches from its center along unit u, 0 at least.
fn patchReach(p: Patch, u: vec3<f32>) -> f32 {
    var reach = 0.0;
    for (var i = 0u; i < p.spreadCount; i++) {
        reach = max(reach, dot3(p.spread[i], u));
    }
    return reach;
}

// A patch's static friction against the world, as the core's
// mvfxRubPatch with no second body.
fn rubPatch(b: ptr<function, Solid>, r: vec3<f32>, r0: vec3<f32>, p: Patch,
            rubbed: ptr<function, f32>) {
    let slide = (*b).x + r - (*b).x0 - r0;
    let along = dot3(slide, p.n);
    let st = slide - along * p.n;
    let slid = sqrt(dot3(st, st));
    if (!(slid > 0.0) || !(p.load > 0.0)) {
        return;
    }
    let u = st / slid;
    let holding = slid / (*b).inverseMass;
    if (!(*rubbed + holding <= params.rigidContact.y * p.load)) {
        return;
    }
    *rubbed += holding;
    let l = lever(r, p.n, p.load * patchReach(p, u), *rubbed);
    let lambda = slid / bodyWeight(*b, l, u);
    bodyPush(b, l, -lambda * u, false);
}

// A patch's dynamic friction, as the core's mvfxSlowPatch.
fn slowPatch(b: ptr<function, Solid>, r: vec3<f32>, p: Patch, slowed: ptr<function, f32>) {
    var v = pointVelocity(*b, r);
    v -= dot3(v, p.n) * p.n;
    let speed = sqrt(dot3(v, v));
    if (!(speed > 0.0)) {
        return;
    }
    let u = v / speed;
    let limit = params.rigidContact.z * p.pressed;
    let l = lever(r, p.n, p.pressed * patchReach(p, u), limit);
    let stop = speed / bodyWeight(*b, l, u);
    let j = min(stop, max(limit - *slowed, 0.0));
    *slowed += j;
    bodyKick(b, l, -j * u);
}

// The body's turn over the substep about unit n (to first order).
fn turnAbout(b: Solid, n: vec3<f32>) -> f32 {
    let dq = multiplyRotations(b.q, conjugate(b.q0));
    return select(2.0, -2.0, dq.w < 0.0) * dot3(dq.xyz, n);
}

fn twistWeight(b: Solid, n: vec3<f32>) -> f32 {
    return dot3(n, inverseInertiaTimes(b, n));
}

fn twistPatch(b: ptr<function, Solid>, n: vec3<f32>, limit: f32, twisted: ptr<function, f32>) {
    let turn = turnAbout(*b, n);
    let weight = twistWeight(*b, n);
    if (turn == 0.0 || !(weight > 0.0)) {
        return;
    }
    let holding = abs(turn) / weight;
    if (!(*twisted + holding <= params.rigidContact.y * limit)) {
        return;
    }
    *twisted += holding;
    (*b).q = turnFirst((*b).q, inverseInertiaTimes(*b, -turn / weight * n));
}

fn unspinPatch(b: ptr<function, Solid>, n: vec3<f32>, limit: f32, spun: ptr<function, f32>) {
    let spin = dot3((*b).w, n);
    let weight = twistWeight(*b, n);
    if (spin == 0.0 || !(weight > 0.0)) {
        return;
    }
    let j = min(abs(spin) / weight, max(limit - *spun, 0.0));
    *spun += j;
    (*b).w += inverseInertiaTimes(*b, select(1.0, -1.0, spin > 0.0) * j * n);
}

fn passIndex(count: u32, step: u32, backward: bool) -> u32 {
    return select(step, count - 1u - step, backward);
}

// A contact's depth at the body's pose, and its offset from the center in
// the world.
fn depthOf(b: Solid, c: Contact, r: ptr<function, vec3<f32>>) -> f32 {
    *r = rotate(b.q, c.body);
    return dot3(b.x + *r - c.surface, c.normal);
}

fn startDepthOf(b: Solid, c: Contact) -> f32 {
    return dot3(b.x0 + rotate(b.q0, c.body) - c.surface, c.normal);
}

// The patch of the contacts with contact first's collider, as the core's
// PatchOf; false when none pushed.
fn patchOf(list: ptr<function, Contacts>, first: u32, h: f32, q: vec4<f32>,
           area: ptr<function, Patch>) -> bool {
    let collider = (*list).items[first].collider;
    (*area).point = vec3<f32>(0.0);
    (*area).n = vec3<f32>(0.0);
    (*area).load = 0.0;
    (*area).pressed = 0.0;
    for (var i = first; i < (*list).count; i++) {
        let c = (*list).items[i];
        let lambda = select(0.0, c.lambda, c.collider == collider && c.lambda > 0.0);
        (*area).load += lambda;
        (*area).pressed += select(0.0, lambda / h + c.applied, lambda > 0.0);
        (*area).point += lambda * c.body;
        (*area).n += lambda * c.normal;
    }
    let square = dot3((*area).n, (*area).n);
    if (!((*area).load > 0.0) || !(square > 0.0 && square <= LARGEST)) {
        return false;
    }
    (*area).n /= sqrt(square);
    (*area).point /= (*area).load;
    // The contacts' mean distance from the center across the normal,
    // weighted by their multipliers.
    let n = rotate(conjugate(q), (*area).n);
    var radius = 0.0;
    for (var i = first; i < (*list).count; i++) {
        let c = (*list).items[i];
        let lambda = select(0.0, c.lambda, c.collider == collider && c.lambda > 0.0);
        let d = c.body - (*area).point;
        let along = dot3(d, n);
        radius += lambda * sqrt(max(dot3(d, d) - along * along, 0.0));
    }
    (*area).radius = radius / (*area).load;
    (*area).spreadCount = 0u;
    for (var i = first; i < (*list).count && (*area).spreadCount < MOST_CONTACTS; i++) {
        if ((*list).items[i].collider == collider) {
            (*area).spread[(*area).spreadCount] = rotate(q, (*list).items[i].body);
            (*area).spreadCount += 1u;
        }
    }
    return true;
}

fn firstOf(list: ptr<function, Contacts>, i: u32) -> bool {
    var first = true;
    for (var j = 0u; first && j < i; j++) {
        first = (*list).items[j].collider != (*list).items[i].collider;
    }
    return first;
}

// The static friction of each of the body's patches, in the order first
// met.
fn rub(b: ptr<function, Solid>, list: ptr<function, Contacts>, h: f32) {
    for (var i = 0u; i < (*list).count; i++) {
        var area = Patch();
        if (firstOf(list, i) && patchOf(list, i, h, (*b).q, &area)) {
            let r = rotate((*b).q, area.point);
            let r0 = rotate((*b).q0, area.point);
            var rubbed = (*list).items[i].rubbed;
            rubPatch(b, r, r0, area, &rubbed);
            (*list).items[i].rubbed = rubbed;
            var twisted = (*list).items[i].twisted;
            twistPatch(b, area.n, area.load * area.radius, &twisted);
            (*list).items[i].twisted = twisted;
        }
    }
}

// The dynamic friction of each of the body's patches.
fn slow(b: ptr<function, Solid>, list: ptr<function, Contacts>, h: f32) {
    for (var i = 0u; i < (*list).count; i++) {
        var area = Patch();
        if (firstOf(list, i) && patchOf(list, i, h, (*b).q, &area)) {
            let r = rotate((*b).q, area.point);
            var slowed = (*list).items[i].slowed;
            slowPatch(b, r, area, &slowed);
            (*list).items[i].slowed = slowed;
            var spun = (*list).items[i].spun;
            unspinPatch(b, area.n, params.rigidContact.z * area.pressed * area.radius, &spun);
            (*list).items[i].spun = spun;
        }
    }
}

// Pushes the body's start pose out of the depth its contacts had there,
// its pose by the same steps, as the core's Stabilize.
fn stabilize(b: ptr<function, Solid>, list: ptr<function, Contacts>, backward: bool) {
    for (var step = 0u; step < (*list).count; step++) {
        let c = (*list).items[passIndex((*list).count, step, backward)];
        let depth = startDepthOf(*b, c);
        if (depth < 0.0) {
            let r = rotate((*b).q0, c.body);
            bodyPush(b, r, -depth / bodyWeight(*b, r, c.normal) * c.normal, true);
        }
    }
}

// A pass pushing the body out of its contacts, then with friction each
// patch's static friction, as the core's SolvePositions.
fn solvePositions(b: ptr<function, Solid>, list: ptr<function, Contacts>, h: f32, friction: bool,
                  backward: bool) {
    for (var step = 0u; step < (*list).count; step++) {
        let at = passIndex((*list).count, step, backward);
        var r = vec3<f32>(0.0);
        let depth = depthOf(*b, (*list).items[at], &r);
        if (depth < 0.0) {
            let n = (*list).items[at].normal;
            let lambda = -depth / bodyWeight(*b, r, n);
            (*list).items[at].lambda += lambda;
            bodyPush(b, r, lambda * n, false);
        }
    }
    if (friction) {
        rub(b, list, h);
    }
}

// The speed into a surface a contact bounces back to, none below a strike.
fn bounceTarget(speed: f32, still: f32) -> f32 {
    let bounce = select(0.0, params.rigidContact.x, speed < -still);
    return -bounce * min(speed, 0.0);
}

// A pass of restitution by sequential impulses, then the patches' dynamic
// friction, as the core's SolveVelocities.
fn solveVelocities(b: ptr<function, Solid>, list: ptr<function, Contacts>, h: f32, still: f32,
                   backward: bool) {
    for (var step = 0u; step < (*list).count; step++) {
        let at = passIndex((*list).count, step, backward);
        let c = (*list).items[at];
        if (!(c.lambda > 0.0)) {
            continue;
        }
        let r = rotate((*b).q, c.body);
        let normal = dot3(pointVelocity(*b, r), c.normal);
        let goal = bounceTarget(c.speed, still);
        let total = max(c.applied + (goal - normal) / bodyWeight(*b, r, c.normal), -c.lambda / h);
        let j = total - c.applied;
        (*list).items[at].applied = total;
        bodyKick(b, r, j * c.normal);
    }
    slow(b, list, h);
}

// Each contact's normal speed at the predicted pose, before it is met.
fn speedsOf(b: Solid, list: ptr<function, Contacts>) {
    for (var i = 0u; i < (*list).count; i++) {
        let c = (*list).items[i];
        (*list).items[i].speed = dot3(pointVelocity(b, rotate(b.q, c.body)), c.normal);
    }
}

// The velocities from the pose's change over h, as the core's
// mvfxCorrectBody.
fn correctBody(b: ptr<function, Solid>, h: f32) {
    let dq = multiplyRotations((*b).q, conjugate((*b).q0));
    let sign = select(1.0, -1.0, dq.w < 0.0);
    (*b).v = ((*b).x - (*b).x0) / h;
    (*b).w = sign * 2.0 * dq.xyz / h;
}

// The passes of a stage over the body's contacts with the world, forward
// and backward by turns, as the core's Passes without pairs; pressing,
// the passes after the first add the static friction.
fn stabilizePasses(b: ptr<function, Solid>, list: ptr<function, Contacts>) {
    for (var round = 0u; round < POSITION_PASSES; round++) {
        stabilize(b, list, round % 2u == 1u);
    }
}

fn pressPasses(b: ptr<function, Solid>, list: ptr<function, Contacts>, h: f32) {
    for (var round = 0u; round < POSITION_PASSES; round++) {
        solvePositions(b, list, h, round > 0u, round % 2u == 1u);
    }
}

fn bouncePasses(b: ptr<function, Solid>, list: ptr<function, Contacts>, h: f32, still: f32) {
    for (var round = 0u; round < RESTITUTION_PASSES; round++) {
        solveVelocities(b, list, h, still, round % 2u == 1u);
    }
}
