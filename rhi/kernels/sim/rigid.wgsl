// The sim kernel's rigid particles (spliced into sim.wgsl), as the core's
// mvfxStepRigid (src/rigid.c) for bodies that meet no other: an
// invocation a body (striding over them), the ended (marked by update)
// skipped, each of its substeps
//  - turning its angular velocity by the gyroscopic term about its own
//    axes, its momentum's size kept (an awake body);
//  - moving it as a particle over the substep (forces, drag, the speed
//    limit, the velocity modules, kills, its orientation by its angular
//    velocity with the angular drag), as the unit pass moves a point;
//  - holding a sleeping body at its start pose, woken where its forces
//    changed by more than the sleep speed over the sleep time;
//  - meeting the world, an awake body, when it collides
//    (sim/rigid_contacts.wgsl);
// then its still time over the step and, alone (its own island: it meets
// no other), its sleep once it has been still the sleep time.
// A body's mass and inertia come from its shape at its size at the step's
// end (taken each substep: a size over life at the speed changes with it),
// in binary32 where the core takes binary64, so they and the square roots
// differ from the core's within their rounding.

const RIGID_SPHERE: u32 = 0u;
const RIGID_CAPSULE: u32 = 1u;
const RIGID_BOX: u32 = 2u;
const RIGID_HULL: u32 = 3u;
// A body's sleep word asleep: before its first sleeping substep, after
// (the core's MVFX_SLEEP_NEW and MVFX_SLEEP_HELD).
const SLEEP_NEW: f32 = -1.0;
const SLEEP_HELD: f32 = -2.0;
// The input of a size over life at the speed (the core's mvfx_overSpeed).
const SIZE_AT_SPEED: u32 = 1u;

// A body's mass: its inertia and inverse inertia about its own axes (xx,
// yy, zz, xy, xz, yz), its inverse mass (1 for a shape of no volume).
struct Mass {
    inertia: array<f32, 6>,
    inverse: array<f32, 6>,
    inverseMass: f32,
}

// A size over life k (0 to 2) at an input, as the core's mvfxLifeAt.
fn rigidLifeAt(k: u32, t: f32) -> f32 {
    let life = params.rigidLives[k / 2u];
    let value = bitcast<f32>(life[2u * (k % 2u)]);
    let first = life[2u * (k % 2u) + 1u];
    if (first == NONE) {
        return value;
    }
    return value * curveAt(first, t);
}

// A rigid body as its substeps hold it: what a span's move changes of
// it, its stored size (ones for none), and its sleep words (its still
// time, or below 0 asleep; the acceleration its forces gave it as it fell
// asleep), none where it does not sleep.
struct Body {
    moving: Moving,
    size: vec3<f32>,
    sleep: f32,
    rest: vec3<f32>,
}

// Live body i of the current buffer.
fn loadBody(i: u32) -> Body {
    let c = params.counts.x;
    var b = Body();
    b.moving = loadMoving(0u, i);
    b.size = vec3<f32>(1.0);
    if (params.words.size != NONE) {
        b.size = readVector(0u, i, params.words.size);
    }
    if (params.rigid.z != NONE) {
        b.sleep = bitcast<f32>(source[params.rigid.z * c + i]);
        b.rest = readVector(0u, i, params.rigid.z + 1u);
    }
    return b;
}

fn storeBody(i: u32, b: Body) {
    let c = params.counts.x;
    storeMoving(0u, i, b.moving);
    if (params.rigid.z != NONE) {
        source[params.rigid.z * c + i] = bitcast<u32>(b.sleep);
        setVector(params.rigid.z + 1u, i, b.rest);
    }
}

// A body's size at the step's end, as the core's mvfxSizeOf: its stored
// size times its sizes over life at its age then or at its speed's share
// of their range.
fn rigidSizeOf(b: Body) -> vec3<f32> {
    var size = b.size;
    if (params.rigidSize.x == 0u) {
        return size;
    }
    var t = 0.0;
    if (params.rigidSize.y == SIZE_AT_SPEED) {
        let v = b.moving.velocity;
        var speed = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        let scale = select(1.0, params.toOrigin.w, params.start.w == 0u);
        if (scale != 1.0) {
            speed = quotient(speed, scale);
        }
        let range = params.rigidSizeRange;
        let share = quotient(speed - range.x, range.y - range.x);
        t = select(min(share, 1.0), 0.0, !(share > 0.0));
    } else {
        t = spanAge(planTick(params.counts.z), b.moving.birth, b.moving.inverseLife);
    }
    let scale = rigidLifeAt(0u, t);
    for (var k = 0u; k < 3u; k++) {
        size[k] *= select(scale, rigidLifeAt(k, t), params.rigidSize.z != 0u);
    }
    return size;
}

// A hull's vertex v scaled by a size along its axes.
fn hullVertex(v: u32, scale: vec3<f32>) -> vec3<f32> {
    let at = params.rigidSize.w + 3u * v;
    return vec3<f32>(tableAt(at), tableAt(at + 1u), tableAt(at + 2u)) * scale;
}

// The radius of the least sphere about a body's center that holds its
// shape at a size, as the core's mvfxBodyReach.
fn rigidReach(size: vec3<f32>) -> f32 {
    let a = abs(size);
    switch (params.rigid.x) {
        case RIGID_HULL: {
            var reach = 0.0;
            for (var v = 0u; v < params.rigid.w; v++) {
                let p = hullVertex(v, a);
                reach = max(reach, dot(p, p));
            }
            return sqrt(reach);
        }
        case RIGID_BOX: {
            let half = 0.5 * a;
            return sqrt(dot(half, half));
        }
        case RIGID_CAPSULE: {
            let r = 0.5 * a.x;
            return 0.5 * max(a.y - 2.0 * r, 0.0) + r;
        }
        default: {
            return 0.5 * a.x;
        }
    }
}

// A shape's mass and its inertia about its own axes at a size, as the
// core's PrimitiveInertia and HullInertia (xx, yy, zz, xy, xz, yz).
fn shapeInertia(size: vec3<f32>, inertia: ptr<function, array<f32, 6>>) -> f32 {
    let a = abs(size);
    let density = params.rigidMass.x;
    let pi = 3.14159265358979;
    let r = 0.5 * a.x;
    var mass = density * 4.0 / 3.0 * pi * r * r * r;
    for (var k = 0u; k < 6u; k++) {
        (*inertia)[k] = select(0.0, 0.4 * mass * r * r, k < 3u);
    }
    switch (params.rigid.x) {
        case RIGID_HULL: {
            let volume = a.x * a.y * a.z;
            let m = params.rigidMoments;
            let moments = array<f32, 6>(m[0].x, m[0].y, m[0].z, m[0].w, m[1].x, m[1].y);
            var c = array<f32, 6>();
            for (var k = 0u; k < 6u; k++) {
                let row = select(select(0u, 1u, k == 5u), k, k < 3u);
                let column = select(select(2u, 1u, k == 3u), k, k < 3u);
                c[k] = density * volume * a[row] * a[column] * moments[k];
            }
            (*inertia)[0] = c[1] + c[2];
            (*inertia)[1] = c[0] + c[2];
            (*inertia)[2] = c[0] + c[1];
            (*inertia)[3] = -c[3];
            (*inertia)[4] = -c[4];
            (*inertia)[5] = -c[5];
            mass = density * volume * params.rigidMass.w;
        }
        case RIGID_BOX: {
            mass = density * a.x * a.y * a.z;
            (*inertia)[0] = mass * (a.y * a.y + a.z * a.z) / 12.0;
            (*inertia)[1] = mass * (a.x * a.x + a.z * a.z) / 12.0;
            (*inertia)[2] = mass * (a.x * a.x + a.y * a.y) / 12.0;
        }
        case RIGID_CAPSULE: {
            let length = max(a.y - 2.0 * r, 0.0);
            let tube = density * pi * r * r * length;
            let ends = mass;
            mass = tube + ends;
            (*inertia)[1] = tube * r * r / 2.0 + ends * 0.4 * r * r;
            (*inertia)[0] = tube * (length * length / 12.0 + r * r / 4.0) +
                            ends * (0.4 * r * r + length * length / 4.0 + 3.0 * length * r / 8.0);
            (*inertia)[2] = (*inertia)[0];
        }
        default: {
        }
    }
    return mass;
}

// A body's mass at a size, as the core's mvfxBodyShapeOf: its inertia and
// the inverse (zero where it has none); a shape of no volume does not
// turn.
fn rigidMass(size: vec3<f32>) -> Mass {
    var m = array<f32, 6>();
    let mass = shapeInertia(size, &m);
    let c = array<f32, 6>(m[1] * m[2] - m[5] * m[5], m[0] * m[2] - m[4] * m[4],
                          m[0] * m[1] - m[3] * m[3], m[4] * m[5] - m[3] * m[2],
                          m[3] * m[5] - m[4] * m[1], m[3] * m[4] - m[0] * m[5]);
    let det = m[0] * c[0] + m[3] * c[3] + m[4] * c[4];
    var out = Mass();
    out.inverseMass = select(1.0, 1.0 / mass, mass > 0.0);
    for (var k = 0u; k < 6u; k++) {
        out.inertia[k] = select(0.0, m[k], mass > 0.0);
        out.inverse[k] = select(0.0, c[k] / det, mass > 0.0 && det > 0.0);
    }
    return out;
}

// A symmetric matrix (xx, yy, zz, xy, xz, yz) times v.
fn symmetricTimes(m: array<f32, 6>, v: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(m[0] * v.x + m[3] * v.y + m[4] * v.z, m[3] * v.x + m[1] * v.y + m[5] * v.z,
                     m[4] * v.x + m[5] * v.y + m[2] * v.z);
}

fn isAsleep(b: Body) -> bool {
    return params.rigid.z != NONE && b.sleep < 0.0;
}

// Turns a body's angular velocity by the gyroscopic term over h about its
// own axes, w += h I^-1 (I w x w), held to its momentum's size, as the
// core's Spin.
fn spinBody(b: ptr<function, Body>, mass: Mass, h: f32) {
    let q = (*b).moving.turning.orientation;
    let inverse = vec4<f32>(-q.xyz, q.w);
    var w = rotate(inverse, (*b).moving.turning.angular);
    var momentum = symmetricTimes(mass.inertia, w);
    let torque = cross(momentum, w);
    let before = sqrt(dot(momentum, momentum));
    w += h * symmetricTimes(mass.inverse, torque);
    momentum = symmetricTimes(mass.inertia, w);
    let after = sqrt(dot(momentum, momentum));
    if (after > 0.0) {
        w *= before / after;
    }
    (*b).moving.turning.angular = rotate(q, w);
}

// Holds a sleeping body at its start pose over a substep of h, its
// velocities none, as the core's mvfxHoldSleeping: its forces'
// acceleration (its velocity over h, from rest) taken as it first sleeps,
// and a change past the sleep speed over the sleep time wakes it.
fn holdSleeping(b: ptr<function, Body>, x0: vec3<f32>, q0: vec4<f32>, h: f32) {
    let change = quotient(params.rigidMass.y, params.rigidMass.z);
    var a = vec3<f32>(0.0);
    var moved = 0.0;
    for (var k = 0u; k < 3u; k++) {
        a[k] = quotient((*b).moving.velocity[k], h);
        let d = a[k] - (*b).rest[k];
        moved += d * d;
    }
    if ((*b).sleep == SLEEP_NEW) {
        (*b).rest = a;
        (*b).sleep = SLEEP_HELD;
    } else if (!(moved <= change * change)) {
        (*b).sleep = 0.0;
    }
    (*b).moving.place = x0;
    (*b).moving.velocity = vec3<f32>(0.0);
    (*b).moving.turning.angular = vec3<f32>(0.0);
    (*b).moving.turning.orientation = q0;
}

// Settles a body after the step's seconds, as the core's
// mvfxSettleBodies for a body alone: an awake one's still time grows
// while its farthest point is slower than the sleep speed, else is 0; one
// still the sleep time falls asleep, its velocities none.
fn settleBody(b: ptr<function, Body>, seconds: f32) {
    if (isAsleep(*b)) {
        return;
    }
    stillBody(b, seconds);
    if (!((*b).sleep < params.rigidMass.z)) {
        (*b).sleep = SLEEP_NEW;
        (*b).moving.velocity = vec3<f32>(0.0);
        (*b).moving.turning.angular = vec3<f32>(0.0);
    }
}

// An awake body's still time over the step's seconds, as the core's
// Still: grown while its farthest point is slower than the sleep speed,
// else 0.
fn stillBody(b: ptr<function, Body>, seconds: f32) {
    let v = (*b).moving.velocity;
    let w = (*b).moving.turning.angular;
    let speed = sqrt(dot(v, v)) + sqrt(dot(w, w)) * rigidReach(rigidSizeOf(*b));
    (*b).sleep = select(0.0, (*b).sleep + seconds, speed < params.rigidMass.y);
}

// Substep s of the step's rigid substeps: its first tick and its ticks.
fn rigidSpan(s: u32) -> vec2<u32> {
    let ticks = params.counts.z;
    let substeps = params.rigid.y;
    let since = ticks * s / substeps;
    return vec2<u32>(since, ticks * (s + 1u) / substeps - since);
}

// A body's substep since a tick of the plan for ticks, as the core's
// Substep: its start pose kept, an awake body spun, it moved as a
// particle, a sleeping one held, an awake one meeting the world; its
// strike's fate.
fn rigidSubstep(b: ptr<function, Body>, since: u32, ticks: u32) -> Fate {
    let h = secondsOf(vec2<u32>(ticks, 0u));
    let x0 = (*b).moving.place;
    let q0 = (*b).moving.turning.orientation;
    if (!isAsleep(*b)) {
        spinBody(b, rigidMass(rigidSizeOf(*b)), h);
    }
    // Through a variable of its own: a pointer to a member is not passed.
    var moving = (*b).moving;
    _ = moveState(&moving, since, ticks);
    (*b).moving = moving;
    if (isAsleep(*b)) {
        holdSleeping(b, x0, q0, h);
        return Fate();
    }
    if (params.collisionHead.z == 0u) {
        return Fate();
    }
    return meetWorld(b, x0, q0, h, since + ticks);
}

// A body that struck the world at a tick of the plan, as the core's
// mvfxStrikeBody: counted, its event the step's first strike's (its
// velocity then), killed or its life shortened as the collision says.
fn strikeBody(b: ptr<function, Body>, c: Contact, tick: u32) -> Fate {
    var fate = Fate();
    fate.contacts = 1u;
    fate.contacted = true;
    fate.first = Hit(0.0, c.surface, c.normal);
    fate.tick = tick;
    fate.velocity = (*b).moving.velocity;
    let count = (*b).moving.contacts;
    (*b).moving.contacts = select(count + 1u, count, count == 0xFFFFFFFFu);
    let loss = params.response.z;
    if (params.collisionHead.y != 0u) {
        (*b).moving.killed = true;
    } else if (loss > 0.0) {
        // Its share of the life left, killed past its age then.
        (*b).moving.inverseLife = divide((*b).moving.inverseLife, 1.0 - loss);
        (*b).moving.death = deathAfter((*b).moving.birth, (*b).moving.inverseLife);
        (*b).moving.killed = (*b).moving.killed || !isAfter((*b).moving.death, planTick(tick));
    }
    return fate;
}

// An awake body meeting the world at its predicted pose over a substep of
// h from its start pose, ending at a tick of the plan, as the core's Meet
// for a body without pairs: its contacts with the colliders at its shape
// and size then (those past the most counted); touched, its prediction
// symplectic Euler's, then the stabilizing, pressing, its velocities from
// its pose's change, restitution and its strike, its first contact struck
// faster than twice gravity's speed over the substep.
fn meetWorld(b: ptr<function, Body>, x0: vec3<f32>, q0: vec4<f32>, h: f32, tick: u32) -> Fate {
    let size = rigidSizeOf(*b);
    let m = (*b).moving;
    var list = gatherContacts(m.place, m.turning.orientation, x0, q0, shapeOf(size),
                              rigidReach(size));
    var fate = Fate();
    fate.capped = list.capped;
    fate.searches = list.searches;
    if (list.count == 0u) {
        return fate;
    }
    let mass = rigidMass(size);
    var body = Solid(m.place, m.velocity, m.turning.angular, x0, m.turning.orientation, q0,
                     mass.inverseMass, mass.inverse);
    speedsOf(body, &list);
    body.x = x0 + body.v * h;
    stabilizePasses(&body, &list);
    pressPasses(&body, &list, h);
    correctBody(&body, h);
    let still = 2.0 * params.gravity.w * h;
    bouncePasses(&body, &list, h, still);
    (*b).moving.place = body.x;
    (*b).moving.velocity = body.v;
    (*b).moving.turning.angular = body.w;
    (*b).moving.turning.orientation = body.q;
    for (var j = 0u; j < list.count; j++) {
        let c = list.items[j];
        if (c.lambda > 0.0 && c.speed < -still) {
            let struck = strikeBody(b, c, tick);
            fate.contacts = struck.contacts;
            fate.contacted = true;
            fate.first = struck.first;
            fate.tick = struck.tick;
            fate.velocity = struck.velocity;
            break;
        }
    }
    return fate;
}

// A step's fate gaining a substep's: its strikes and drops summed, its
// first strike kept.
fn joinFates(fate: ptr<function, Fate>, moved: Fate) {
    (*fate).contacts += moved.contacts;
    (*fate).capped += moved.capped;
    (*fate).searches += moved.searches;
    if (moved.contacted && !(*fate).contacted) {
        (*fate).contacted = true;
        (*fate).first = moved.first;
        (*fate).tick = moved.tick;
        (*fate).velocity = moved.velocity;
    }
}

// A body's step's drops past the most and its searches stopped at their
// cap in a word, as offsets sums them (MVFX_GPU_RIGID_CAPPED and
// MVFX_GPU_RIGID_SEARCHES): the drops in the low 24 bits, the searches
// in the high 8, each held at its most.
fn rigidCounts(fate: Fate) -> u32 {
    return min(fate.capped, 0xFFFFFFu) | (min(fate.searches, 0xFFu) << 24u);
}

// Rigid: each live body's substeps and its settling, the ended (marked by
// update) skipped; its first strike noted for its event, and the contacts
// it dropped past the most in its alive word of the other buffer, free
// until the scatter, which offsets sums (MVFX_GPU_RIGID_CAPPED).
@compute @workgroup_size(64, 1, 1)
fn rigid(@builtin(global_invocation_id) id: vec3<u32>) {
    let c = params.counts.x;
    let live = counts[0];
    for (var i = id.x; i < live; i += params.modes.z * 64u) {
        moved[params.words.alive * c + i] = 0u;
        if (source[params.words.alive * c + i] == LIVE_ENDED) {
            continue;
        }
        var b = loadBody(i);
        // Its event's life the step's first: only a strike shortens it.
        let inverseLife = b.moving.inverseLife;
        var fate = Fate();
        for (var s = 0u; s < params.rigid.y; s++) {
            let span = rigidSpan(s);
            if (span.y > 0u) {
                joinFates(&fate, rigidSubstep(&b, span.x, span.y));
            }
        }
        if (params.rigid.z != NONE) {
            settleBody(&b, secondsOf(vec2<u32>(params.counts.z, 0u)));
        }
        if (fate.contacted) {
            noteUnitContact(i, fate.tick, fate.first, fate.velocity, inverseLife);
        }
        moved[params.words.alive * c + i] = rigidCounts(fate);
        storeBody(i, b);
    }
}

// A rigid body born at a tick of the plan as birth j draws it, at its
// place and velocity at its tick: its substeps from its birth, each from
// the later of its start and the birth, as the core's
// mvfxIntegrateRigidNewborn (awake, meeting the world alone), the step's
// fate of its strikes. One born at the plan's end takes none, as the core's newborn
// there moves over no time.
fn rigidNewborn(j: u32, place: vec3<f32>, velocity: vec3<f32>, b: ptr<function, Body>) -> Fate {
    let b0 = birthWord(j, 0u);
    let b1 = birthWord(j, 1u);
    (*b).moving.place = place;
    (*b).moving.velocity = velocity;
    (*b).moving.turning = bornTurning(j);
    var birth = vec2<u32>(0u);
    var death = vec2<u32>(0u);
    birthTicks(b0, b1, &birth, &death);
    (*b).moving.birth = birth;
    (*b).moving.death = death;
    (*b).moving.inverseLife = bitcast<f32>(b1.y);
    (*b).moving.dragScale = dragScaleOf(j);
    (*b).size = bornSize(j);
    var fate = Fate();
    for (var s = 0u; s < params.rigid.y; s++) {
        let span = rigidSpan(s);
        let since = max(span.x, b0.z);
        let to = span.x + span.y;
        if (to > since) {
            joinFates(&fate, rigidSubstep(b, since, to - since));
        }
    }
    fate.killed = (*b).moving.killed;
    fate.inverseLife = (*b).moving.inverseLife;
    fate.death = (*b).moving.death;
    return fate;
}
