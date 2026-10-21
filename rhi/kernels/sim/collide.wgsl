// The sim kernel's collisions (spliced into sim.wgsl): planes, spheres,
// boxes, signed distance grids and the depth image, the first contact
// of a segment and its bounce, and the facts it leaves.

// A contact: how far along the segment (0 for a start on the wrong side),
// where, and the surface's normal toward the allowed side.
struct Hit {
    t: f32,
    point: vec3<f32>,
    normal: vec3<f32>,
}

fn hitPlane(c: Collider, s: vec3<f32>, e: vec3<f32>, hit: ptr<function, Hit>) -> bool {
    let n = c.normal.xyz;
    let d0 = dot3(s - c.center.xyz, n);
    let d1 = dot3(e - c.center.xyz, n);
    (*hit).normal = n;
    if (d0 < 0.0) {
        (*hit).t = 0.0;
        (*hit).point = s - d0 * n;
        return true;
    }
    if (!(d1 < 0.0)) {
        return false;
    }
    (*hit).t = d0 / (d0 - d1);
    (*hit).point = s + (*hit).t * (e - s);
    return true;
}

// A sphere's surface point along a direction from its center (up when the
// direction is 0), the normal outward or inward.
fn spherePoint(c: Collider, toward: vec3<f32>, hit: ptr<function, Hit>) {
    var dir = toward - c.center.xyz;
    let length = sqrt(dot3(dir, dir));
    if (length > 0.0) {
        dir = dir / length;
    } else {
        dir = vec3<f32>(0.0, 0.0, 1.0);
    }
    (*hit).point = c.center.xyz + c.center.w * dir;
    (*hit).normal = select(1.0, -1.0, c.modes.y != 0u) * dir;
}

fn hitSphere(c: Collider, s: vec3<f32>, e: vec3<f32>, hit: ptr<function, Hit>) -> bool {
    let inside = c.modes.y != 0u;
    let f = s - c.center.xyz;
    let g = e - c.center.xyz;
    let d = e - s;
    let r2 = c.center.w * c.center.w;
    let ff = dot3(f, f);
    let outside = r2 < ff;
    if (select(ff < r2, outside, inside)) {
        (*hit).t = 0.0;
        spherePoint(c, s, hit);
        return true;
    }
    let crosses = !inside || dot3(g, g) > r2;
    let a = dot3(d, d);
    if (!crosses || !(a > 0.0)) {
        return false;
    }
    let b = dot3(f, d);
    let disc = b * b - a * (ff - r2);
    if (!(disc >= 0.0)) {
        return false;
    }
    let root = sqrt(disc);
    let t = select((-b - root) / a, (-b + root) / a, inside);
    if (!(t >= 0.0 && t <= 1.0)) {
        return false;
    }
    (*hit).t = t;
    spherePoint(c, s + t * d, hit);
    return true;
}

// The axis a start inside a box is nearest a face on, or outside it
// farthest past one.
fn pushAxis(h: vec3<f32>, a: vec3<f32>, inside: bool) -> u32 {
    var axis = 0u;
    var best = select(LARGEST, -LARGEST, inside);
    for (var k = 0u; k < 3u; k++) {
        let room = h[k] - abs(a[k]);
        let deeper = best < -room;
        if (select(room < best, deeper, inside)) {
            best = select(room, -room, inside);
            axis = k;
        }
    }
    return axis;
}

fn isInside(h: vec3<f32>, a: vec3<f32>) -> bool {
    return abs(a.x) <= h.x && abs(a.y) <= h.y && abs(a.z) <= h.z;
}

// A box's contact in its own frame, from local start a to local end b.
fn hitBoxLocal(h: vec3<f32>, inside: bool, a: vec3<f32>, b: vec3<f32>,
               hit: ptr<function, Hit>) -> bool {
    if (isInside(h, a) != inside) {
        // A start on the wrong side: put on the surface.
        (*hit).t = 0.0;
        let axis = pushAxis(h, a, inside);
        let sign = select(1.0, -1.0, a[axis] < 0.0);
        var point = select(a, min(max(a, -h), h), inside);
        point[axis] = sign * h[axis];
        var normal = vec3<f32>(0.0);
        normal[axis] = select(sign, -sign, inside);
        (*hit).point = point;
        (*hit).normal = normal;
        return true;
    }
    if (inside && isInside(h, b)) {
        return false;
    }
    var low = 0.0;
    var high = 1.0;
    var lowAxis = -1;
    var highAxis = -1;
    for (var k = 0; k < 3; k++) {
        let d = b[k] - a[k];
        if (d == 0.0) {
            if (abs(a[k]) > h[k]) {
                return false;
            }
            continue;
        }
        let t1 = (-h[k] - a[k]) / d;
        let t2 = (h[k] - a[k]) / d;
        let near = min(t1, t2);
        let far = max(t1, t2);
        lowAxis = select(lowAxis, k, near > low);
        low = max(low, near);
        highAxis = select(highAxis, k, far < high);
        high = min(high, far);
    }
    let axis = select(lowAxis, highAxis, inside);
    if (low > high || axis < 0) {
        return false;
    }
    (*hit).t = select(low, high, inside);
    (*hit).point = a + (*hit).t * (b - a);
    var normal = vec3<f32>(0.0);
    normal[axis] = select(1.0, -1.0, b[axis] - a[axis] > 0.0);
    (*hit).normal = normal;
    return true;
}

fn hitBox(c: Collider, s: vec3<f32>, e: vec3<f32>, hit: ptr<function, Hit>) -> bool {
    let q = c.rotation;
    let inverse = vec4<f32>(-q.xyz, q.w);
    let a = rotate(inverse, s - c.center.xyz);
    let b = rotate(inverse, e - c.center.xyz);
    var local: Hit;
    if (!hitBoxLocal(c.halfExtents.xyz, c.modes.y != 0u, a, b, &local)) {
        return false;
    }
    (*hit).t = local.t;
    (*hit).point = rotate(q, local.point) + c.center.xyz;
    (*hit).normal = rotate(q, local.normal);
    return true;
}

// The most samples a grid collider takes along a segment, and the
// halvings that find a crossing between two.
const FIELD_SAMPLES: u32 = 256u;
const FIELD_HALVINGS: u32 = 10u;

// A local place on the wrong side put back on the surface: straight up or
// down onto a heightfield, along the gradient out of a distance grid.
fn pushOutOfField(c: Collider, start: vec3<f32>) -> vec3<f32> {
    var local = start;
    for (var turn = 0u; turn < 4u; turn++) {
        let f = gridField(c, local);
        if (!(f < 0.0)) {
            break;
        }
        if (c.modes.x == COLLIDER_HEIGHTFIELD) {
            local.y += select(-f, f, c.modes.y != 0u);
            continue;
        }
        local -= f * gradientAt(c, local);
    }
    return local;
}

// The part [low, high] of the local segment a to b within the grid's box
// (a heightfield's X and Z); false when it misses it.
fn clipToBox(c: Collider, a: vec3<f32>, b: vec3<f32>, low: ptr<function, f32>,
             high: ptr<function, f32>) -> bool {
    *low = 0.0;
    *high = 1.0;
    for (var k = 0u; k < 3u; k++) {
        if (k == 1u && c.modes.x == COLLIDER_HEIGHTFIELD) {
            continue;
        }
        let h = c.halfExtents[k];
        let d = b[k] - a[k];
        if (d == 0.0) {
            if (abs(a[k]) > h) {
                return false;
            }
            continue;
        }
        let t1 = (-h - a[k]) / d;
        let t2 = (h - a[k]) / d;
        *low = max(*low, min(t1, t2));
        *high = min(*high, max(t1, t2));
    }
    return *low <= *high;
}

// The local segment's first crossing into a grid's wrong side, sampled
// every half cell: its share of the segment, on the allowed side.
fn crossField(c: Collider, a: vec3<f32>, b: vec3<f32>, t: ptr<function, f32>) -> bool {
    var low = 0.0;
    var high = 1.0;
    if (!clipToBox(c, a, b, &low, &high)) {
        return false;
    }
    let d = b - a;
    let length = sqrt(dot3(d, d)) * (high - low);
    let cell = gridSpan(c);
    let wanted = select(1.0, ceil(length / (0.5 * cell)), cell > 0.0);
    let n = u32(min(max(wanted, 1.0), f32(FIELD_SAMPLES)));
    var before = low;
    for (var i = 0u; i <= n; i++) {
        var at = low + (high - low) * f32(i) / f32(n);
        if (!(gridField(c, a + at * d) < 0.0)) {
            before = at;
            continue;
        }
        // Halve [before, at] toward the crossing, keeping the allowed end.
        for (var halving = 0u; halving < FIELD_HALVINGS && i > 0u; halving++) {
            let middle = 0.5 * (before + at);
            if (gridField(c, a + middle * d) < 0.0) {
                at = middle;
            } else {
                before = middle;
            }
        }
        *t = before;
        return true;
    }
    return false;
}

fn hitField(c: Collider, s: vec3<f32>, e: vec3<f32>, hit: ptr<function, Hit>) -> bool {
    let q = c.rotation;
    let inverse = vec4<f32>(-q.xyz, q.w);
    let a = rotate(inverse, s - c.center.xyz);
    let b = rotate(inverse, e - c.center.xyz);
    var local = a;
    var t = 0.0;
    if (gridField(c, a) < 0.0) {
        local = pushOutOfField(c, a);
    } else if (crossField(c, a, b, &t)) {
        local = a + t * (b - a);
    } else {
        return false;
    }
    (*hit).t = t;
    (*hit).point = rotate(q, local) + c.center.xyz;
    (*hit).normal = rotate(q, gradientAt(c, local));
    return true;
}

// A place projected into the depth image: its device coordinates and its
// pixel.
struct Projected {
    ndc: vec3<f32>,
    column: i32,
    row: i32,
}

// Whether a place in the particles' space projects into the image, in
// front of the camera, and where.
fn project(x: vec3<f32>, p: ptr<function, Projected>) -> bool {
    let clip = params.depth.toClip * vec4<f32>(x, 1.0);
    if (!(clip.w > 0.0)) {
        return false;
    }
    let ndc = clip.xyz / clip.w;
    if (!(abs(ndc.x) <= 1.0 && abs(ndc.y) <= 1.0 && ndc.z >= 0.0 && ndc.z <= 1.0)) {
        return false;
    }
    let w = i32(params.depth.head.y);
    let h = i32(params.depth.head.z);
    (*p).ndc = ndc;
    (*p).column = min(i32((ndc.x * 0.5 + 0.5) * f32(w)), w - 1);
    (*p).row = min(i32((0.5 - ndc.y * 0.5) * f32(h)), h - 1);
    return true;
}

// A pixel's device depth; false where it holds none.
fn depthAt(column: i32, row: i32, d: ptr<function, f32>) -> bool {
    if (column < 0 || row < 0 || column >= i32(params.depth.head.y) ||
        row >= i32(params.depth.head.z)) {
        return false;
    }
    *d = textureLoad(depthImage, vec2<i32>(column, row), 0);
    let far = select(1.0, 0.0, params.depth.modes.x != 0u);
    return *d >= 0.0 && *d <= 1.0 && *d != far;
}

// The particles' place at device x, y and depth; false at infinity.
fn unproject(x: f32, y: f32, d: f32, out: ptr<function, vec3<f32>>) -> bool {
    let h = params.depth.fromClip * vec4<f32>(x, y, d, 1.0);
    if (h.w == 0.0) {
        return false;
    }
    *out = h.xyz / h.w;
    return all(abs(*out) <= vec3<f32>(LARGEST));
}

// A pixel's surface place, at its center; false where it has none.
fn pixelPoint(column: i32, row: i32, out: ptr<function, vec3<f32>>) -> bool {
    var d = 0.0;
    if (!depthAt(column, row, &d)) {
        return false;
    }
    let x = (f32(column) + 0.5) / f32(params.depth.head.y) * 2.0 - 1.0;
    let y = 1.0 - (f32(row) + 0.5) / f32(params.depth.head.z) * 2.0;
    return unproject(x, y, d, out);
}

// The shorter of the differences to a pixel's two neighbors along an
// axis; false with neither.
fn across(center: vec3<f32>, column: i32, row: i32, dc: i32, dr: i32,
          out: ptr<function, vec3<f32>>) -> bool {
    var after = vec3<f32>(0.0);
    var before = vec3<f32>(0.0);
    let hasAfter = pixelPoint(column + dc, row + dr, &after);
    let hasBefore = pixelPoint(column - dc, row - dr, &before);
    let a = after - center;
    let b = center - before;
    if (!hasAfter && !hasBefore) {
        return false;
    }
    let useAfter = hasAfter && (!hasBefore || sqrt(dot3(a, a)) <= sqrt(dot3(b, b)));
    *out = select(b, a, useAfter);
    return true;
}

// The direction toward the camera along the ray at device x, y.
fn towardCamera(x: f32, y: f32) -> vec3<f32> {
    let reversed = params.depth.modes.x != 0u;
    var nearer = vec3<f32>(0.0);
    var farther = vec3<f32>(0.0);
    if (unproject(x, y, select(0.25, 0.75, reversed), &nearer) &&
        unproject(x, y, select(0.75, 0.25, reversed), &farther)) {
        return nearer - farther;
    }
    return rotate(params.depth.normalTurn, vec3<f32>(0.0, 0.0, 1.0));
}

// The surface's unit normal at a pixel, toward the camera: the normal
// image's turned, or the cross of the shorter neighbor differences.
fn depthNormal(p: Projected) -> vec3<f32> {
    let toward = towardCamera(p.ndc.x, p.ndc.y);
    var n = toward;
    var center = vec3<f32>(0.0);
    var dx = vec3<f32>(0.0);
    var dy = vec3<f32>(0.0);
    if (params.depth.modes.z != 0u) {
        let given = textureLoad(normalImage, vec2<i32>(p.column, p.row), 0).xyz;
        let finite = abs(given) <= vec3<f32>(LARGEST);
        n = rotate(params.depth.normalTurn, select(vec3<f32>(0.0), given, finite));
    } else if (pixelPoint(p.column, p.row, &center) && across(center, p.column, p.row, 1, 0, &dx) &&
               across(center, p.column, p.row, 0, 1, &dy)) {
        n = vec3<f32>(dx.y * dy.z - dx.z * dy.y, dx.z * dy.x - dx.x * dy.z,
                      dx.x * dy.y - dx.y * dy.x);
    }
    var length = sqrt(dot3(n, n));
    if (!(length > 0.0 && length <= LARGEST)) {
        n = toward;
        length = sqrt(dot3(n, n));
    }
    let sign = select(1.0, -1.0, dot3(n, toward) < 0.0);
    return sign * n / length;
}

// Whether a place meets the image: along its ray, within radius in front
// of the pixel's surface or less than the thickness behind it; the
// surface's place on that ray.
fn meetsDepth(place: vec3<f32>, radius: f32, p: ptr<function, Projected>,
              surface: ptr<function, vec3<f32>>) -> bool {
    var d = 0.0;
    if (!project(place, p) || !depthAt((*p).column, (*p).row, &d) ||
        !unproject((*p).ndc.x, (*p).ndc.y, d, surface)) {
        return false;
    }
    let toward = towardCamera((*p).ndc.x, (*p).ndc.y);
    let behind = -dot3(place - *surface, toward) / sqrt(dot3(toward, toward));
    return behind > -radius && behind < params.depth.sizes.y;
}

// The samples a segment takes: one a pixel it crosses and one each half
// thickness, at least one, at most the emitter's.
fn depthSamples(s: vec3<f32>, e: vec3<f32>) -> u32 {
    let d = e - s;
    var wanted = sqrt(dot3(d, d)) / (0.5 * params.depth.sizes.y);
    var ps: Projected;
    var pe: Projected;
    let most = f32(params.depth.head.w);
    if (!project(s, &ps) || !project(e, &pe)) {
        wanted = most;
    } else {
        let pixels = max(abs(f32(pe.column - ps.column)), abs(f32(pe.row - ps.row)));
        wanted = max(wanted, pixels);
    }
    return u32(min(max(ceil(wanted), 1.0), most));
}

// The halvings that find a crossing between two samples.
const DEPTH_HALVINGS: u32 = 10u;

// The first share of the segment that meets the image within radius,
// sampled and then halved toward from the sample before; -1 when none
// does.
fn firstMeeting(s: vec3<f32>, e: vec3<f32>, radius: f32, p: ptr<function, Projected>,
                surface: ptr<function, vec3<f32>>) -> f32 {
    let n = depthSamples(s, e);
    for (var i = 0u; i <= n; i++) {
        var hi = f32(i) / f32(n);
        if (!meetsDepth(s + hi * (e - s), radius, p, surface)) {
            continue;
        }
        var lo = select(hi, f32(i - 1u) / f32(n), i > 0u);
        for (var halving = 0u; halving < DEPTH_HALVINGS && i > 0u; halving++) {
            let middle = 0.5 * (lo + hi);
            var q: Projected;
            var at = vec3<f32>(0.0);
            if (meetsDepth(s + middle * (e - s), radius, &q, &at)) {
                hi = middle;
                *p = q;
                *surface = at;
            } else {
                lo = middle;
            }
        }
        return hi;
    }
    return -1.0;
}

// The segment's first meeting with the depth image: the surface grown by
// the radius along its normal.
fn hitDepth(s: vec3<f32>, e: vec3<f32>, hit: ptr<function, Hit>) -> bool {
    var p: Projected;
    var surface = vec3<f32>(0.0);
    let at = firstMeeting(s, e, params.depth.sizes.x, &p, &surface);
    if (at < 0.0) {
        return false;
    }
    let normal = depthNormal(p);
    (*hit).t = at;
    (*hit).normal = normal;
    (*hit).point = surface + params.depth.sizes.x * normal;
    return true;
}

// The segment's earliest contact with the depth image or any collider.
fn firstHit(s: vec3<f32>, e: vec3<f32>, best: ptr<function, Hit>) -> bool {
    var found = params.depth.head.x != 0u && hitDepth(s, e, best);
    for (var i = 0u; i < params.collisionHead.x; i++) {
        let c = params.colliders[i];
        var hit: Hit;
        var met = false;
        switch c.modes.x {
            case COLLIDER_PLANE: {
                met = hitPlane(c, s, e, &hit);
            }
            case COLLIDER_SPHERE: {
                met = hitSphere(c, s, e, &hit);
            }
            case COLLIDER_BOX: {
                met = hitBox(c, s, e, &hit);
            }
            case COLLIDER_HEIGHTFIELD, COLLIDER_DISTANCE: {
                met = hitField(c, s, e, &hit);
            }
            default: {
            }
        }
        if (met && (!found || hit.t < (*best).t)) {
            *best = hit;
            found = true;
        }
    }
    return found;
}

// A vector bounced off a surface: its part into it reversed by
// restitution, its part along it cut by friction.
fn bounce(n: vec3<f32>, v: vec3<f32>, response: vec4<f32>) -> vec3<f32> {
    let into = dot3(v, n);
    let normal = select(into, -response.x * into, into < 0.0);
    let keep = 1.0 - response.y;
    return normal * n + keep * (v - into * n);
}

// A move from start to place met with the colliders under the emitter's
// response: the place and the velocity after its contacts (at most
// MAX_CONTACTS), which it returns.
fn collide(start: vec3<f32>, place: ptr<function, vec3<f32>>,
           velocity: ptr<function, vec3<f32>>, first: ptr<function, Hit>) -> u32 {
    return collideWith(start, place, velocity, first, params.response);
}

// A move met with the colliders under a response (restitution, friction,
// unused, rest speed), as the core's mvfxCollide.
fn collideWith(start: vec3<f32>, place: ptr<function, vec3<f32>>,
               velocity: ptr<function, vec3<f32>>, first: ptr<function, Hit>,
               response: vec4<f32>) -> u32 {
    var s = start;
    var contacts = 0u;
    var hit: Hit;
    while (contacts < MAX_CONTACTS && firstHit(s, *place, &hit)) {
        if (contacts == 0u) {
            *first = hit;
        }
        var rest = (*place - s) * (1.0 - hit.t);
        *velocity = bounce(hit.normal, *velocity, response);
        rest = bounce(hit.normal, rest, response);
        // Off the surface by a hair, so the next test does not meet it again.
        let p = hit.point;
        let hair = 1e-6 * (1.0 + abs(p.x) + abs(p.y) + abs(p.z));
        let stops = dot3(*velocity, *velocity) < response.w * response.w;
        s = p + hair * hit.normal;
        if (stops) {
            *velocity = vec3<f32>(0.0);
            *place = s;
            return contacts + 1u;
        }
        *place = s + rest;
        // Counted last: HLSL that reads a stepped count later in the loop
        // trips Maul RHI's check for SPIRV-Cross's stale loop reads.
        contacts += 1u;
    }
    if (contacts == MAX_CONTACTS && firstHit(s, *place, &hit)) {
        // Out of contacts with a path still into a surface: stop at it.
        *place = hit.point;
    }
    return contacts;
}

// The first tick a particle born at birth (two words) reaches its life at
// inverse life inverse, as the core's.
fn deathAfter(birth: vec2<u32>, inverse: f32) -> vec2<u32> {
    return add64(birth, lifeTicks(inverse));
}

// What a move's contacts did to a particle: its contacts, whether one
// kills it, its inverse life and death tick after the lives they took.
struct Fate {
    contacts: u32,
    killed: bool,
    inverseLife: f32,
    death: vec2<u32>,
    // Whether its contacts make an event (none when leaving the view
    // kills it), the first of them.
    contacted: bool,
    first: Hit,
    // A rigid body's first strike: its tick of the plan (0 for a move's
    // contact, whose tick is its share of the move) and its velocity
    // then; the contacts its substeps dropped past the most, and its
    // polytope searches stopped at their cap.
    tick: u32,
    velocity: vec3<f32>,
    capped: u32,
    searches: u32,
}

// A move from start met with the colliders, for a particle born at birth
// with inverse life inverse, dying at death.
fn meet(start: vec3<f32>, place: ptr<function, vec3<f32>>, velocity: ptr<function, vec3<f32>>,
        birth: vec2<u32>, inverse: f32, death: vec2<u32>) -> Fate {
    var fate = Fate();
    fate.inverseLife = inverse;
    fate.death = death;
    if (params.collisionHead.z == 0u) {
        return fate;
    }
    // Through a variable of its own: naga's SPIR-V of a pointer to a
    // struct member passed on fails.
    var first = Hit(0.0, vec3<f32>(0.0), vec3<f32>(0.0));
    fate.contacts = collide(start, place, velocity, &first);
    fate.first = first;
    var seen: Projected;
    if (params.depth.modes.y != 0u && !project(*place, &seen)) {
        // Out of the view, where leaving it kills.
        fate.killed = true;
        return fate;
    }
    if (fate.contacts == 0u) {
        return fate;
    }
    fate.contacted = true;
    if (params.collisionHead.y != 0u) {
        fate.killed = true;
        return fate;
    }
    let loss = params.response.z;
    if (loss > 0.0) {
        // Each contact takes its share of the life left.
        for (var n = 0u; n < fate.contacts; n++) {
            fate.inverseLife = divide(fate.inverseLife, 1.0 - loss);
        }
        fate.death = deathAfter(birth, fate.inverseLife);
    }
    return fate;
}

// The words of a live particle's facts in the events buffer, and its
// flags: its life ended within the plan; its contacts make an event; the
// contact is a unit point's first of its substeps, its tick of the plan
// in place of its share of the move and its velocity then kept; a unit
// point's edge across or down tore (its facts after the spawn region,
// units.wgsl's noteTear).
const EVENT_FACTS: u32 = 12u;
const EVENT_ENDED: u32 = 1u;
const EVENT_CONTACT: u32 = 2u;
const EVENT_SUBSTEP: u32 = 4u;
const EVENT_TEAR_ACROSS: u32 = 8u;
const EVENT_TEAR_DOWN: u32 = 16u;

// Notes a live particle's facts of the step for its events, when the
// emitter writes any: its flags, its first contact, the inverse life it
// met it with.
fn noteFacts(i: u32, flags: u32, first: Hit, inverse: f32) {
    if (params.fateHead.y == 0u) {
        return;
    }
    let at = EVENT_FACTS * i;
    events[at] = flags;
    events[at + 1u] = bitcast<u32>(first.t);
    for (var k = 0u; k < 3u; k++) {
        events[at + 2u + k] = bitcast<u32>(first.point[k]);
        events[at + 5u + k] = bitcast<u32>(first.normal[k]);
    }
    events[at + 8u] = bitcast<u32>(inverse);
}

// Notes a unit point's contact at a tick of the plan for its events, when
// the emitter writes any and the point noted none this step: the
// contact, the velocity it left and the inverse life it met it with.
fn noteUnitContact(i: u32, tick: u32, first: Hit, velocity: vec3<f32>, inverse: f32) {
    if (params.fateHead.y == 0u || (events[EVENT_FACTS * i] & EVENT_CONTACT) != 0u) {
        return;
    }
    // Its tears of earlier substeps kept.
    let tore = events[EVENT_FACTS * i] & (EVENT_TEAR_ACROSS | EVENT_TEAR_DOWN);
    noteFacts(i, EVENT_CONTACT | EVENT_SUBSTEP | tore, first, inverse);
    let at = EVENT_FACTS * i;
    events[at + 1u] = tick;
    for (var k = 0u; k < 3u; k++) {
        events[at + 9u + k] = bitcast<u32>(velocity[k]);
    }
}
