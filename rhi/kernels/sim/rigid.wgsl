// The sim kernel's rigid particles (spliced into sim.wgsl), as the core's
// mvfxStepRigid (src/rigid.c) for bodies that meet nothing: an invocation
// a body (striding over them), the ended (marked by update) skipped, each
// of its substeps
//  - turning its angular velocity by the gyroscopic term about its own
//    axes, its momentum's size kept (an awake body);
//  - moving it as a particle over the substep (forces, drag, the speed
//    limit, the velocity modules, kills, its orientation by its angular
//    velocity with the angular drag), as the unit pass moves a point;
//  - holding a sleeping body at its start pose, woken where its forces
//    changed by more than the sleep speed over the sleep time;
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
// yy, zz, xy, xz, yz).
struct Mass {
    inertia: array<f32, 6>,
    inverse: array<f32, 6>,
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

// Body i of a buffer's size at the step's end, as the core's
// mvfxSizeOf: its stored size (ones for none) times its sizes over life
// at its age then or at its speed's share of their range.
fn rigidSizeOf(buffer: u32, i: u32) -> vec3<f32> {
    let c = params.counts.x;
    var size = vec3<f32>(1.0);
    if (params.words.size != NONE) {
        size = readVector(buffer, i, params.words.size);
    }
    if (params.rigidSize.x == 0u) {
        return size;
    }
    var t = 0.0;
    if (params.rigidSize.y == SIZE_AT_SPEED) {
        let v = readVector(buffer, i, params.words.velocity);
        var speed = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        let scale = select(1.0, params.toOrigin.w, params.start.w == 0u);
        if (scale != 1.0) {
            speed = quotient(speed, scale);
        }
        let range = params.rigidSizeRange;
        let share = quotient(speed - range.x, range.y - range.x);
        t = select(min(share, 1.0), 0.0, !(share > 0.0));
    } else {
        let birth = vec2<u32>(wordIn(buffer, params.words.birth * c + i),
                              wordIn(buffer, (params.words.birth + 1u) * c + i));
        let inverseLife = bitcast<f32>(wordIn(buffer, params.words.inverseLife * c + i));
        t = spanAge(planTick(params.counts.z), birth, inverseLife);
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

fn orientationOfBody(buffer: u32, i: u32) -> vec4<f32> {
    let c = params.counts.x;
    var q = vec4<f32>(0.0);
    for (var k = 0u; k < 4u; k++) {
        q[k] = bitcast<f32>(wordIn(buffer, (params.words.orientation + k) * c + i));
    }
    return q;
}

fn sleepOf(i: u32) -> f32 {
    return bitcast<f32>(source[params.rigid.z * params.counts.x + i]);
}

fn setSleep(i: u32, value: f32) {
    source[params.rigid.z * params.counts.x + i] = bitcast<u32>(value);
}

fn isAsleep(i: u32) -> bool {
    return params.rigid.z != NONE && sleepOf(i) < 0.0;
}

// Turns body i of a buffer's angular velocity by the gyroscopic term
// over h about its own axes, w += h I^-1 (I w x w), held to its
// momentum's size, as the core's Spin.
fn spinBody(buffer: u32, i: u32, mass: Mass, h: f32) {
    let q = orientationOfBody(buffer, i);
    let inverse = vec4<f32>(-q.xyz, q.w);
    var w = rotate(inverse, readVector(buffer, i, params.words.angular));
    var momentum = symmetricTimes(mass.inertia, w);
    let torque = cross(momentum, w);
    let before = sqrt(dot(momentum, momentum));
    w += h * symmetricTimes(mass.inverse, torque);
    momentum = symmetricTimes(mass.inertia, w);
    let after = sqrt(dot(momentum, momentum));
    if (after > 0.0) {
        w *= before / after;
    }
    setVectorIn(buffer, params.words.angular, i, rotate(q, w));
}

// Holds a sleeping body i at its start pose over a substep of h, its
// velocities none, as the core's mvfxHoldSleeping: its forces'
// acceleration (its velocity over h, from rest) taken as it first sleeps,
// and a change past the sleep speed over the sleep time wakes it.
fn holdSleeping(i: u32, x0: vec3<f32>, q0: vec4<f32>, h: f32) {
    let c = params.counts.x;
    let change = quotient(params.rigidMass.y, params.rigidMass.z);
    let rest = params.rigid.z + 1u;
    var a = vec3<f32>(0.0);
    var moved = 0.0;
    let v = readVector(0u, i, params.words.velocity);
    for (var k = 0u; k < 3u; k++) {
        a[k] = quotient(v[k], h);
        let d = a[k] - bitcast<f32>(source[(rest + k) * c + i]);
        moved += d * d;
    }
    if (sleepOf(i) == SLEEP_NEW) {
        setVector(rest, i, a);
        setSleep(i, SLEEP_HELD);
    } else if (!(moved <= change * change)) {
        setSleep(i, 0.0);
    }
    setVector(params.words.place, i, x0);
    setVector(params.words.velocity, i, vec3<f32>(0.0));
    setVector(params.words.angular, i, vec3<f32>(0.0));
    for (var k = 0u; k < 4u; k++) {
        source[(params.words.orientation + k) * c + i] = bitcast<u32>(q0[k]);
    }
}

// Settles body i after the step's seconds, as the core's
// mvfxSettleBodies for a body alone: an awake one's still time grows
// while its farthest point is slower than the sleep speed, else is 0; one
// still the sleep time falls asleep, its velocities none.
fn settleBody(i: u32, size: vec3<f32>, seconds: f32) {
    if (isAsleep(i)) {
        return;
    }
    let v = readVector(0u, i, params.words.velocity);
    let w = readVector(0u, i, params.words.angular);
    let speed = sqrt(dot(v, v)) + sqrt(dot(w, w)) * rigidReach(size);
    let still = select(0.0, sleepOf(i) + seconds, speed < params.rigidMass.y);
    setSleep(i, still);
    if (!(still < params.rigidMass.z)) {
        setSleep(i, SLEEP_NEW);
        setVector(params.words.velocity, i, vec3<f32>(0.0));
        setVector(params.words.angular, i, vec3<f32>(0.0));
    }
}

// Substep s of the step's rigid substeps: its first tick and its ticks.
fn rigidSpan(s: u32) -> vec2<u32> {
    let ticks = params.counts.z;
    let substeps = params.rigid.y;
    let since = ticks * s / substeps;
    return vec2<u32>(since, ticks * (s + 1u) / substeps - since);
}

// Rigid: each live body's substeps and its settling, the ended (marked by
// update) skipped.
@compute @workgroup_size(64, 1, 1)
fn rigid(@builtin(global_invocation_id) id: vec3<u32>) {
    let c = params.counts.x;
    let live = counts[0];
    for (var i = id.x; i < live; i += params.modes.z * 64u) {
        if (source[params.words.alive * c + i] == LIVE_ENDED) {
            continue;
        }
        for (var s = 0u; s < params.rigid.y; s++) {
            let span = rigidSpan(s);
            if (span.y == 0u) {
                continue;
            }
            let h = secondsOf(vec2<u32>(span.y, 0u));
            let x0 = readVector(0u, i, params.words.place);
            let q0 = orientationOfBody(0u, i);
            if (!isAsleep(i)) {
                spinBody(0u, i, rigidMass(rigidSizeOf(0u, i)), h);
            }
            moveSpan(0u, i, span.x, span.y);
            if (isAsleep(i)) {
                holdSleeping(i, x0, q0, h);
            }
        }
        if (params.rigid.z != NONE) {
            settleBody(i, rigidSizeOf(0u, i), secondsOf(vec2<u32>(params.counts.z, 0u)));
        }
    }
}

// A rigid body born at a tick of the plan, at place at of the other
// buffer, its values drawn: its substeps from its birth, each from the
// later of its start and the birth, as the core's
// mvfxIntegrateRigidNewborn (awake, meeting nothing); the place it ends
// at. One born at the plan's end takes none, as the core's newborn there
// moves over no time.
fn rigidNewborn(at: u32, born: u32) -> vec3<f32> {
    for (var s = 0u; s < params.rigid.y; s++) {
        let span = rigidSpan(s);
        let since = max(span.x, born);
        let to = span.x + span.y;
        if (to > since) {
            let ticks = to - since;
            spinBody(1u, at, rigidMass(rigidSizeOf(1u, at)), secondsOf(vec2<u32>(ticks, 0u)));
            moveSpan(1u, at, since, ticks);
        }
    }
    return readVector(1u, at, params.words.place);
}
