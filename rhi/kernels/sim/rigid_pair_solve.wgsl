// The sim kernel's rigid pairs' solve (spliced into sim.wgsl beside
// sim/rigid_pairs.wgsl), as the core's src/rigid_pair.c: the pool's
// contacts, a run's passes pushing its two bodies apart and its
// restitution, and its patch's friction between them.

// A pair's contact in the pool, as the core's mvfxPairContact: its bodies
// (the lower index first), its points on each in their frames, its normal
// (from b toward a), its multipliers and its friction's sums.
struct PairContact {
    a: u32,
    b: u32,
    onA: vec3<f32>,
    onB: vec3<f32>,
    normal: vec3<f32>,
    lambda: f32,
    speed: f32,
    applied: f32,
    rubbed: f32,
    slowed: f32,
    twisted: f32,
    spun: f32,
}

const PAIR_WORDS: u32 = 18u;

fn storePair(at: u32, c: PairContact) {
    blocks[at] = c.a;
    blocks[at + 1u] = c.b;
    storeVector(at + 2u, c.onA);
    storeVector(at + 5u, c.onB);
    storeVector(at + 8u, c.normal);
    blocks[at + 11u] = bitcast<u32>(c.lambda);
    blocks[at + 12u] = bitcast<u32>(c.speed);
    blocks[at + 13u] = bitcast<u32>(c.applied);
    blocks[at + 14u] = bitcast<u32>(c.rubbed);
    blocks[at + 15u] = bitcast<u32>(c.slowed);
    blocks[at + 16u] = bitcast<u32>(c.twisted);
    blocks[at + 17u] = bitcast<u32>(c.spun);
}

fn loadPair(at: u32) -> PairContact {
    var c = PairContact();
    c.a = blocks[at];
    c.b = blocks[at + 1u];
    c.onA = loadVector(at + 2u);
    c.onB = loadVector(at + 5u);
    c.normal = loadVector(at + 8u);
    c.lambda = bitcast<f32>(blocks[at + 11u]);
    c.speed = bitcast<f32>(blocks[at + 12u]);
    c.applied = bitcast<f32>(blocks[at + 13u]);
    c.rubbed = bitcast<f32>(blocks[at + 14u]);
    c.slowed = bitcast<f32>(blocks[at + 15u]);
    c.twisted = bitcast<f32>(blocks[at + 16u]);
    c.spun = bitcast<f32>(blocks[at + 17u]);
    return c;
}

// A contact's offsets from both bodies' centers in the world and its depth,
// at the bodies' poses or their start poses, as the core's PairDepth.
fn pairDepth(c: PairContact, a: Solid, b: Solid, start: bool, ra: ptr<function, vec3<f32>>,
             rb: ptr<function, vec3<f32>>) -> f32 {
    *ra = rotate(select(a.q, a.q0, start), c.onA);
    *rb = rotate(select(b.q, b.q0, start), c.onB);
    let xa = select(a.x, a.x0, start);
    let xb = select(b.x, b.x0, start);
    return dot3(xa + *ra - xb - *rb, c.normal);
}

// The speed along the normal of a's point from b's, as the core's
// PairSpeed.
fn pairSpeed(c: PairContact, a: Solid, b: Solid, ra: vec3<f32>, rb: vec3<f32>) -> f32 {
    return dot3(pointVelocity(a, ra) - pointVelocity(b, rb), c.normal);
}

// A run's patch between two bodies, as the core's PairPatch: its points'
// offsets on each at the pose and the start pose, its normal, its load
// and impulses, its radius and each body's spread of contacts.
struct PairPatch {
    ra: vec3<f32>,
    rb: vec3<f32>,
    ra0: vec3<f32>,
    rb0: vec3<f32>,
    n: vec3<f32>,
    load: f32,
    pressed: f32,
    radius: f32,
    spreadA: array<vec3<f32>, 8>,
    spreadB: array<vec3<f32>, 8>,
    count: u32,
}

// The patch of the pool's contacts first to end, as the core's PatchOf;
// false when none pushed.
fn pairPatchOf(a: Solid, b: Solid, first: u32, end: u32, h: f32,
               out: ptr<function, PairPatch>) -> bool {
    var area = PairContact();
    (*out).load = 0.0;
    (*out).pressed = 0.0;
    (*out).radius = 0.0;
    (*out).count = 0u;
    for (var i = first; i < end; i++) {
        let c = loadPair(poolAt(i));
        let lambda = max(c.lambda, 0.0);
        (*out).load += lambda;
        (*out).pressed += select(0.0, lambda / h + c.applied, lambda > 0.0);
        area.onA += lambda * c.onA;
        area.onB += lambda * c.onB;
        area.normal += lambda * c.normal;
    }
    var n = area.normal;
    if (!((*out).load > 0.0) || !normalized(&n)) {
        return false;
    }
    area.normal = n;
    area.onA /= (*out).load;
    area.onB /= (*out).load;
    (*out).n = n;
    var ra = vec3<f32>(0.0);
    var rb = vec3<f32>(0.0);
    _ = pairDepth(area, a, b, true, &ra, &rb);
    (*out).ra0 = ra;
    (*out).rb0 = rb;
    _ = pairDepth(area, a, b, false, &ra, &rb);
    (*out).ra = ra;
    (*out).rb = rb;
    let local = rotate(conjugate(a.q), n);
    for (var i = first; i < end; i++) {
        let c = loadPair(poolAt(i));
        let lambda = max(c.lambda, 0.0);
        let d = c.onA - area.onA;
        let along = dot3(d, local);
        (*out).radius += lambda * sqrt(max(dot3(d, d) - along * along, 0.0));
        if ((*out).count < MOST_CONTACTS) {
            (*out).spreadA[(*out).count] = rotate(a.q, c.onA);
            (*out).spreadB[(*out).count] = rotate(b.q, c.onB);
            (*out).count += 1u;
        }
    }
    (*out).radius /= (*out).load;
    return true;
}

// How far a body's spread reaches along unit u times sign, 0 at least.
fn spreadReach(spread: array<vec3<f32>, 8>, count: u32, u: vec3<f32>, sign: f32) -> f32 {
    var reach = 0.0;
    for (var i = 0u; i < count; i++) {
        reach = max(reach, sign * dot3(spread[i], u));
    }
    return reach;
}

// A pair patch's static friction, as the core's mvfxRubPatch with b.
fn rubPair(a: ptr<function, Solid>, b: ptr<function, Solid>, p: PairPatch,
           rubbed: ptr<function, f32>) {
    let slide = ((*a).x + p.ra - (*a).x0 - p.ra0) - ((*b).x + p.rb - (*b).x0 - p.rb0);
    let along = dot3(slide, p.n);
    let st = slide - along * p.n;
    let slid = sqrt(dot3(st, st));
    if (!(slid > 0.0) || !(p.load > 0.0)) {
        return;
    }
    let u = st / slid;
    let holding = slid / ((*a).inverseMass + (*b).inverseMass);
    if (!(*rubbed + holding <= params.rigidContact.y * p.load)) {
        return;
    }
    *rubbed += holding;
    let la = lever(p.ra, p.n, p.load * spreadReach(p.spreadA, p.count, u, 1.0), *rubbed);
    let lb = lever(p.rb, p.n, p.load * spreadReach(p.spreadB, p.count, u, -1.0), *rubbed);
    let lambda = slid / (bodyWeight(*a, la, u) + bodyWeight(*b, lb, u));
    bodyPush(a, la, -lambda * u, false);
    bodyPush(b, lb, lambda * u, false);
}

// A pair patch's dynamic friction, as the core's mvfxSlowPatch with b.
fn slowPair(a: ptr<function, Solid>, b: ptr<function, Solid>, p: PairPatch,
            slowed: ptr<function, f32>) {
    var v = pointVelocity(*a, p.ra) - pointVelocity(*b, p.rb);
    v -= dot3(v, p.n) * p.n;
    let speed = sqrt(dot3(v, v));
    if (!(speed > 0.0)) {
        return;
    }
    let u = v / speed;
    let limit = params.rigidContact.z * p.pressed;
    let la = lever(p.ra, p.n, p.pressed * spreadReach(p.spreadA, p.count, u, 1.0), limit);
    let lb = lever(p.rb, p.n, p.pressed * spreadReach(p.spreadB, p.count, u, -1.0), limit);
    let stop = speed / (bodyWeight(*a, la, u) + bodyWeight(*b, lb, u));
    let j = min(stop, max(limit - *slowed, 0.0));
    *slowed += j;
    bodyKick(a, la, -j * u);
    bodyKick(b, lb, j * u);
}

// A pair patch's twist held, as the core's mvfxTwistPatch with b.
fn twistPair(a: ptr<function, Solid>, b: ptr<function, Solid>, n: vec3<f32>, limit: f32,
             twisted: ptr<function, f32>) {
    let turn = turnAbout(*a, n) - turnAbout(*b, n);
    let weight = twistWeight(*a, n) + twistWeight(*b, n);
    if (turn == 0.0 || !(weight > 0.0)) {
        return;
    }
    let holding = abs(turn) / weight;
    if (!(*twisted + holding <= params.rigidContact.y * limit)) {
        return;
    }
    *twisted += holding;
    let l = -turn / weight * n;
    (*a).q = turnFirst((*a).q, inverseInertiaTimes(*a, l));
    (*b).q = turnFirst((*b).q, inverseInertiaTimes(*b, -l));
}

// A pair patch's spin slowed, as the core's mvfxUnspinPatch with b.
fn unspinPair(a: ptr<function, Solid>, b: ptr<function, Solid>, n: vec3<f32>, limit: f32,
              spun: ptr<function, f32>) {
    let spin = dot3((*a).w, n) - dot3((*b).w, n);
    let weight = twistWeight(*a, n) + twistWeight(*b, n);
    if (spin == 0.0 || !(weight > 0.0)) {
        return;
    }
    let j = min(abs(spin) / weight, max(limit - *spun, 0.0));
    *spun += j;
    let l = select(1.0, -1.0, spin > 0.0) * j * n;
    (*a).w += inverseInertiaTimes(*a, l);
    (*b).w += inverseInertiaTimes(*b, -l);
}

// Holds a run's contacts where post-stabilization found them, as the
// core's HoldStabilized: their slide across the mean normal at the
// contacts' mean point pushed back.
fn holdPair(a: ptr<function, Solid>, b: ptr<function, Solid>, first: u32, end: u32,
            onA: vec3<f32>, onB: vec3<f32>, before: vec3<f32>) {
    let after = (*a).x0 + rotate((*a).q0, onA) - (*b).x0 - rotate((*b).q0, onB);
    var n = vec3<f32>(0.0);
    for (var i = first; i < end; i++) {
        n += loadVector(poolAt(i) + 8u);
    }
    if (!normalized(&n)) {
        return;
    }
    var slide = after - before;
    slide -= dot3(slide, n) * n;
    let slid = sqrt(dot3(slide, slide));
    if (!(slid > 0.0)) {
        return;
    }
    let u = slide / slid;
    let ra = rotate((*a).q0, onA);
    let rb = rotate((*b).q0, onB);
    let lambda = slid / (bodyWeight(*a, ra, u) + bodyWeight(*b, rb, u));
    bodyPush(a, ra, -lambda * u, true);
    bodyPush(b, rb, lambda * u, true);
}

// A pass over the run first to end, as the core's SolvePair: stabilizing
// (start) or pushing apart, then with friction the run's static friction.
fn solvePair(first: u32, end: u32, start: bool, friction: bool, backward: bool, h: f32) {
    let head = loadPair(poolAt(first));
    var a = loadSolid(head.a);
    var b = loadSolid(head.b);
    var onA = vec3<f32>(0.0);
    var onB = vec3<f32>(0.0);
    let count = f32(end - first);
    for (var i = first; i < end; i++) {
        onA += loadVector(poolAt(i) + 2u) / count;
        onB += loadVector(poolAt(i) + 5u) / count;
    }
    let before = a.x0 + rotate(a.q0, onA) - b.x0 - rotate(b.q0, onB);
    for (var step = 0u; step < end - first; step++) {
        let at = poolAt(first + passIndex(end - first, step, backward));
        var c = loadPair(at);
        var ra = vec3<f32>(0.0);
        var rb = vec3<f32>(0.0);
        let depth = pairDepth(c, a, b, start, &ra, &rb);
        if (!(depth < 0.0)) {
            continue;
        }
        let lambda = -depth / (bodyWeight(a, ra, c.normal) + bodyWeight(b, rb, c.normal));
        if (!start) {
            blocks[at + 11u] = bitcast<u32>(c.lambda + lambda);
        }
        bodyPush(&a, ra, lambda * c.normal, start);
        bodyPush(&b, rb, -lambda * c.normal, start);
    }
    if (start) {
        holdPair(&a, &b, first, end, onA, onB, before);
    }
    var area = PairPatch();
    if (friction && pairPatchOf(a, b, first, end, h, &area)) {
        let at = poolAt(first);
        var rubbed = bitcast<f32>(blocks[at + 14u]);
        rubPair(&a, &b, area, &rubbed);
        blocks[at + 14u] = bitcast<u32>(rubbed);
        var twisted = bitcast<f32>(blocks[at + 16u]);
        twistPair(&a, &b, area.n, area.load * area.radius, &twisted);
        blocks[at + 16u] = bitcast<u32>(twisted);
    }
    storeSolid(head.a, a);
    storeSolid(head.b, b);
}

// A pass of restitution over the run first to end, then its patch's
// dynamic friction, as the core's BouncePair.
fn bouncePair(first: u32, end: u32, backward: bool, h: f32, still: f32) {
    let head = loadPair(poolAt(first));
    var a = loadSolid(head.a);
    var b = loadSolid(head.b);
    for (var step = 0u; step < end - first; step++) {
        let at = poolAt(first + passIndex(end - first, step, backward));
        let c = loadPair(at);
        if (!(c.lambda > 0.0)) {
            continue;
        }
        var ra = vec3<f32>(0.0);
        var rb = vec3<f32>(0.0);
        _ = pairDepth(c, a, b, false, &ra, &rb);
        let normal = pairSpeed(c, a, b, ra, rb);
        let weight = bodyWeight(a, ra, c.normal) + bodyWeight(b, rb, c.normal);
        let bounce = select(0.0, params.rigidContact.x, c.speed < -still);
        let aim = -bounce * min(c.speed, 0.0);
        let total = max(c.applied + (aim - normal) / weight, -c.lambda / h);
        let j = total - c.applied;
        blocks[at + 13u] = bitcast<u32>(total);
        bodyKick(&a, ra, j * c.normal);
        bodyKick(&b, rb, -j * c.normal);
    }
    var area = PairPatch();
    if (pairPatchOf(a, b, first, end, h, &area)) {
        let at = poolAt(first);
        var slowed = bitcast<f32>(blocks[at + 15u]);
        slowPair(&a, &b, area, &slowed);
        blocks[at + 15u] = bitcast<u32>(slowed);
        var spun = bitcast<f32>(blocks[at + 17u]);
        unspinPair(&a, &b, area.n, params.rigidContact.z * area.pressed * area.radius, &spun);
        blocks[at + 17u] = bitcast<u32>(spun);
    }
    storeSolid(head.a, a);
    storeSolid(head.b, b);
}
