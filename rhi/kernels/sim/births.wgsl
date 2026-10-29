// The sim kernel's births (spliced into sim.wgsl after sim/step.wgsl):
// each birth of the plan drawn, placed and moved to the plan's end, its
// values stored, and killBirths.

// The instance's place at a tick of the plan, between its places at the
// plan's ends: origins from the anchor after, rotation and scale.
fn placeAt(tick: u32, origin: ptr<function, vec3<f32>>, rotation: ptr<function, vec4<f32>>) -> f32 {
    let t = select(1.0, f32(tick) / f32(params.counts.z), params.counts.z > 0u);
    *origin = mix(params.fromOrigin.xyz, params.toOrigin.xyz, t);
    let a = params.fromRotation;
    let b = params.toRotation * select(1.0, -1.0, dot(params.fromRotation, params.toRotation) < 0.0);
    *rotation = normalize(mix(a, b, t));
    return mix(params.fromOrigin.w, params.toOrigin.w, t);
}

// A birth's tick and death tick made absolute (two words each).
fn birthTicks(b0: vec4<u32>, b1: vec4<u32>, birth: ptr<function, vec2<u32>>,
              death: ptr<function, vec2<u32>>) {
    let birthLow = params.start.x + b0.z;
    *birth = vec2<u32>(birthLow, params.start.y + select(0u, 1u, birthLow < params.start.x));
    let deathLow = params.start.x + b0.w;
    *death = vec2<u32>(deathLow,
                       params.start.y + b1.x + select(0u, 1u, deathLow < params.start.x));
}

// A birth of the plan drawn, placed, and moved from its tick to the plan's
// end under its forces at its birth place and met with the colliders: its
// place and velocity then, and its fate, killed too when an attractor
// kills it on the way.
fn bornAt(j: u32, place: ptr<function, vec3<f32>>, velocity: ptr<function, vec3<f32>>) -> Fate {
    var born = vec3<f32>(0.0);
    var bornVelocity = vec3<f32>(0.0);
    var turning = Turning();
    return bornFacts(j, &born, &bornVelocity, place, velocity, &turning);
}

// A birth's place at its tick, before its move, and its velocity then
// when withVelocity is set (drawn: none otherwise).
fn bornPlace(j: u32, withVelocity: bool, place: ptr<function, vec3<f32>>,
             velocity: ptr<function, vec3<f32>>) {
    let b0 = birthWord(j, 0u);
    let k = birthKey(j);
    var normal = vec3<f32>(0.0);
    let shapeWords = draw(SHAPE_SALT, k.x, k.y);
    let local_place = sampleShape(j, shapeWords, &normal);
    let drawn = local_place + params.shapeOffset.xyz;
    var v = vec3<f32>(0.0);
    if (withVelocity) {
        v = drawVelocity(draw(VELOCITY_SALT, k.x, k.y), local_place, normal);
    }
    var p = drawn;
    let host = birthWord(j, 2u).y;
    if (host != NONE) {
        // The host's particle: its place and velocity as the core gives them.
        let record = hostBirthAt(host);
        p = record.place.xyz;
        v = record.velocity.xyz;
    } else if (params.start.w == 0u) {
        var origin = vec3<f32>(0.0);
        var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
        let scale = placeAt(b0.z, &origin, &rotation);
        p = origin + rotate(rotation, drawn * scale);
        v = rotate(rotation, v * scale) + params.shift.w * params.instanceVelocity.xyz;
    }
    if (unitPoints() > 1u) {
        // A unit's point across and down from its root, by steps turned
        // and scaled with the instance at its tick in the world's space.
        var across = params.unitAcross.xyz;
        var down = params.unitDown.xyz;
        if (params.start.w == 0u) {
            var origin = vec3<f32>(0.0);
            var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
            let scale = placeAt(b0.z, &origin, &rotation);
            across = rotate(rotation, across * scale);
            down = rotate(rotation, down * scale);
        }
        let at = unitPlace(j);
        p = p + across * at.x + down * at.y;
    }
    *place = p;
    *velocity = v;
}

// bornAt with the birth's place and velocity at its tick, before its
// move, as its birth event gives them; a rigid body's turn after its
// substeps (the birth's as drawn for other particles, turned after) and
// its velocity held, as the core's.
fn bornFacts(j: u32, born: ptr<function, vec3<f32>>, bornVelocity: ptr<function, vec3<f32>>,
             place: ptr<function, vec3<f32>>, velocity: ptr<function, vec3<f32>>,
             turning: ptr<function, Turning>) -> Fate {
    let b0 = birthWord(j, 0u);
    let b1 = birthWord(j, 1u);
    var p = vec3<f32>(0.0);
    var v = vec3<f32>(0.0);
    bornPlace(j, true, &p, &v);
    *born = p;
    *bornVelocity = v;
    if (params.rigid.y > 0u) {
        // A rigid body takes the rest of its step's substeps.
        var b = Body();
        let fate = rigidNewborn(j, p, v, &b);
        *place = b.moving.place;
        *velocity = holdSpeed(b.moving.velocity);
        *turning = b.moving.turning;
        return fate;
    }
    *turning = bornTurning(j);
    var a = vec3<f32>(0.0);
    var wind = 0.0;
    forcesAt(p, secondsOf(vec2<u32>(b0.z, 0u)), &a, &wind);
    var touch = Touch(false, vec3<f32>(0.0), 0.0);
    if (params.water.x != NONE) {
        touch = waterDrag(p, &a, &wind);
    }
    let seconds = remainderOf(j);
    let f = factorsFor(wind, dragScaleOf(j), seconds, false);
    // Born at age 0, its speed limit's decay its own.
    v = limitSpeed(v, 0.0, factors(bitcast<f32>(params.turns.w), seconds).x);
    waterLift(touch, v, f.y, f.z, &a);
    *place = moveByModules(p, p + v * f.y + a * f.z, 0.0, seconds);
    *velocity = v * f.x + a * f.y;
    var birth = vec2<u32>(0u);
    var death = vec2<u32>(0u);
    birthTicks(b0, b1, &birth, &death);
    var fate = meet(p, place, velocity, birth, bitcast<f32>(b1.y), death);
    *velocity = holdSpeed(*velocity);
    let age = vec2<u32>(params.counts.z - b0.z, 0u);
    fate.killed = fate.killed || killed(p, *place, *velocity, age, fate.contacts);
    return fate;
}

// A host's particle of the plan after the births' room (twice the
// capacity's, three words each): its place in the particles' space and
// what it gives (HOST_GIVES_COLOR, HOST_GIVES_SIZE); its velocity; its
// colour; its size.
struct HostBirth {
    place: vec4<f32>,
    velocity: vec4<f32>,
    color: vec4<f32>,
    size: vec4<f32>,
}

fn hostBirthAt(h: u32) -> HostBirth {
    let at = 6u * params.counts.x + 4u * h;
    return HostBirth(bitcast<vec4<f32>>(births[at]), bitcast<vec4<f32>>(births[at + 1u]),
                     bitcast<vec4<f32>>(births[at + 2u]), bitcast<vec4<f32>>(births[at + 3u]));
}

const HOST_GIVES_COLOR: u32 = 1u;
const HOST_GIVES_SIZE: u32 = 2u;

// A birth written to place at of the other buffer; its place.
fn spawnAt(j: u32, at: u32) -> vec3<f32> {
    let c = params.counts.x;
    let b0 = birthWord(j, 0u);
    let b1 = birthWord(j, 1u);
    var moved_place = vec3<f32>(0.0);
    var moved_velocity = vec3<f32>(0.0);
    var born = vec3<f32>(0.0);
    var bornVelocity = vec3<f32>(0.0);
    var turning = Turning();
    let fate = bornFacts(j, &born, &bornVelocity, &moved_place, &moved_velocity, &turning);
    if (params.unitArrays.x != NONE && isPinned(j % unitPoints())) {
        // A pinned point stays where it was born, as the core's AppendPoint
        // (its move's contacts and kills still count).
        moved_place = born;
        moved_velocity = vec3<f32>(0.0);
    }
    var birth = vec2<u32>(0u);
    var death = vec2<u32>(0u);
    birthTicks(b0, b1, &birth, &death);
    for (var k = 0u; k < 3u; k++) {
        moved[(params.words.place + k) * c + at] = bitcast<u32>(moved_place[k]);
        moved[(params.words.velocity + k) * c + at] = bitcast<u32>(moved_velocity[k]);
    }
    moved[params.words.serial * c + at] = birthSerial(j);
    moved[params.words.birth * c + at] = birth.x;
    moved[(params.words.birth + 1u) * c + at] = birth.y;
    moved[params.words.death * c + at] = fate.death.x;
    moved[(params.words.death + 1u) * c + at] = fate.death.y;
    moved[params.words.inverseLife * c + at] = bitcast<u32>(fate.inverseLife);
    if (params.words.collisions != NONE) {
        moved[params.words.collisions * c + at] = fate.contacts;
    }
    if (params.unitArrays.x != NONE) {
        storeAnchor(j, at, born);
    }
    if (params.unitTears.x != NONE) {
        // Born whole, as the core's AppendPoint.
        moved[params.unitTears.x * c + at] = 0u;
    }
    storeValues(j, at);
    // A liquid gas's particle is born with no affine motion, as the core's.
    if (params.liquid.w != NONE) {
        for (var k = 0u; k < 9u; k++) {
            moved[(params.liquid.w + k) * c + at] = 0u;
        }
    }
    if (params.rigid.y > 0u) {
        // Turned by its substeps, awake, as the core's births.
        storeTurning(1u, at, turning);
        for (var k = 0u; params.rigid.z != NONE && k < 4u; k++) {
            moved[(params.rigid.z + k) * c + at] = 0u;
        }
    } else {
        // Born at age 0, turned over the rest of the plan.
        let angular = factors(bitcast<f32>(params.turns.y), remainderOf(j)).xy;
        turn(1u, at, angular, lifeAt(LIFE_ROTATION, 0.0));
    }
    storeBirthPlaces(j, born, moved_place);
    return moved_place;
}

// A born unit point's anchor, its place at birth, as the core's
// AppendPoint; for units held to the instance, a pinned point's in the
// instance's space at the plan's end, as the core's AnchorInInstance; for
// units held to frames, a pinned point's in its unit's new frame, which
// the unit's first point stores (bornFrame).
fn storeAnchor(j: u32, at: u32, born: vec3<f32>) {
    let c = params.counts.x;
    var anchor = born;
    if (params.unitPins.y == ATTACH_INSTANCE && isPinned(j % unitPoints())) {
        let q = params.toRotation;
        anchor = rotate(vec4<f32>(-q.xyz, q.w), born - params.toOrigin.xyz) * (1.0 / params.toOrigin.w);
    }
    if (isFramed()) {
        let frame = bornFrame(j, at);
        if (isPinned(j % unitPoints())) {
            let q = frame[1];
            anchor = rotate(vec4<f32>(-q.xyz, q.w), born - frame[0].xyz);
        }
    }
    for (var k = 0u; k < 3u; k++) {
        moved[(params.unitArrays.x + k) * c + at] = bitcast<u32>(anchor[k]);
    }
}

// A born unit's frame at the plan's end, as the core's NewFrame: for the
// host's frames the one its birth names (frame 0 for the emitter's own),
// or held at no turn where the host set no such frame; for a source's
// particles, one born of an event the frame of the event's particle (its
// serial in its host record's last word; at no turn when that is gone,
// let go at the next step's start), any other held at no turn. Its first
// point stores the frame, its flags and its frame's index or particle's
// serial.
fn bornFrame(j: u32, at: u32) -> array<vec4<f32>, 2> {
    let c = params.counts.x;
    let host = birthWord(j, 2u).y;
    var f = 0u;
    var frame = array<vec4<f32>, 2>(vec4<f32>(0.0), vec4<f32>(0.0, 0.0, 0.0, 1.0));
    var flags = UNIT_HELD;
    if (params.unitPins.y == ATTACH_PARTICLES) {
        if (host != NONE && host >= params.spawnHead.z) {
            f = bitcast<u32>(hostBirthAt(host).size.w);
            flags = UNIT_EVENT;
            let i = findSource(f);
            if (i != NONE) {
                frame = sourceFrame(i);
            }
        }
    } else {
        if (host != NONE) {
            f = bitcast<u32>(hostBirthAt(host).velocity.w);
        }
        if (f < params.unitFrames.y) {
            frame = hostFrame(f);
            flags = 0u;
        }
    }
    if (j % unitPoints() == 0u) {
        for (var k = 0u; k < 3u; k++) {
            moved[(params.unitArrays.y + k) * c + at] = bitcast<u32>(frame[0][k]);
        }
        for (var k = 0u; k < 4u; k++) {
            moved[(params.unitArrays.z + k) * c + at] = bitcast<u32>(frame[1][k]);
        }
        moved[params.unitArrays.w * c + at] = flags;
        moved[params.unitPins.z * c + at] = f;
    }
    return frame;
}

// The seconds from a birth of the plan to the plan's end.
fn remainderOf(j: u32) -> f32 {
    return secondsOf(vec2<u32>(params.counts.z - birthWord(j, 0u).z, 0u));
}

// A birth's drag scale, as the core draws it (1 when the emitter has
// none).
fn dragScaleOf(j: u32) -> f32 {
    if (params.words.drag == NONE) {
        return 1.0;
    }
    let k = birthKey(j);
    let age = bitcast<f32>(birthWord(j, 2u).x);
    return drawVector(SOURCE_DRAG, age, draw(DRAG_SALT, k.x, k.y)).x;
}

// A live particle's normalized age at the plan's start, as the core's:
// its seconds since birth times its inverse life, held to [0, 1].
fn ageAt(birth: vec2<u32>, inverseLife: f32) -> f32 {
    let age = secondsOf(subtract64(params.start.xy, birth)) * inverseLife;
    return select(min(age, 1.0), 0.0, !(age > 0.0));
}

// A value over life at a normalized age, as the core's mvfxLifeAt.
fn lifeAt(k: u32, age: f32) -> f32 {
    let life = params.lives[k / 2u];
    let value = bitcast<f32>(life[2u * (k % 2u)]);
    let first = life[2u * (k % 2u) + 1u];
    if (first == NONE) {
        return value;
    }
    return value * curveAt(first, age);
}

// The fastest a moved particle goes, in the emitter's units a second
// (MVFX_MAX_SPEED).
const MAX_SPEED: f32 = 1e9;

// A velocity's speed held within MAX_SPEED in the particles' space,
// keeping its heading (shared/speed.wgsl's heldSpeed).
fn holdSpeed(v: vec3<f32>) -> vec3<f32> {
    return heldSpeed(v, MAX_SPEED * params.modules[0].w);
}

// A velocity held to the speed limit at a normalized age: past it, its
// speed falls toward it by decay, as the core's mvfxLimitSpeed.
fn limitSpeed(v: vec3<f32>, age: f32, decay: f32) -> vec3<f32> {
    if ((params.turns.z & LIMITS_SPEED) == 0u) {
        return v;
    }
    let limit = max(lifeAt(LIFE_SPEED_LIMIT, age), 0.0) * params.modules[0].w;
    let speed = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (!(speed > limit)) {
        return v;
    }
    return v * ((limit + (speed - limit) * decay) / speed);
}

// A place moved over seconds from start by the velocity modules at a
// normalized age: the motion scaled, the added and radial speeds' moves,
// then the orbit, as the core's mvfxMoveByModules.
fn moveByModules(start: vec3<f32>, place: vec3<f32>, age: f32, h: f32) -> vec3<f32> {
    if ((params.turns.z & MODULES_MOVE) == 0u) {
        return place;
    }
    let sigma = params.modules[0].w;
    let scale = lifeAt(LIFE_MOTION_SCALE, age);
    let added = lifeAt(LIFE_ADDED_SPEED, age) * sigma * h;
    let radial = lifeAt(LIFE_RADIAL_SPEED, age) * sigma * h;
    let out = start - params.modules[3].xyz;
    let square = dot3(out, out);
    let away = square > 0.0 && isFinite(square);
    let unit = out * inverseSqrt(square);
    var p = start + scale * (place - start) + added * params.modules[0].xyz;
    if (away) {
        p += radial * unit;
    }
    let angle = lifeAt(LIFE_ORBIT_RATE, age) * h;
    if (angle != 0.0) {
        let center = params.modules[2].xyz;
        let half = sinCos(0.5 * angle);
        let q = vec4<f32>(params.modules[1].xyz * half.x, half.y);
        p = center + rotate(q, p - center);
    }
    return p;
}

// A word of buffer (0 the current, 1 the other) at an index, and its
// write.
fn wordIn(buffer: u32, at: u32) -> u32 {
    if (buffer == 0u) {
        return source[at];
    }
    return moved[at];
}

fn setWordIn(buffer: u32, at: u32, value: u32) {
    if (buffer == 0u) {
        source[at] = value;
    } else {
        moved[at] = value;
    }
}

// What a particle's turn changes: its angle and spin, its orientation and
// angular velocity (as stored: zero where the emitter stores none).
struct Turning {
    angle: f32,
    spin: f32,
    orientation: vec4<f32>,
    angular: vec3<f32>,
}

fn loadTurning(buffer: u32, i: u32) -> Turning {
    let c = params.counts.x;
    let w = params.words;
    var t = Turning(0.0, 0.0, vec4<f32>(0.0, 0.0, 0.0, 1.0), vec3<f32>(0.0));
    if (w.angle != NONE) {
        t.angle = bitcast<f32>(wordIn(buffer, w.angle * c + i));
        t.spin = bitcast<f32>(wordIn(buffer, w.spin * c + i));
    }
    if (w.orientation != NONE) {
        for (var k = 0u; k < 4u; k++) {
            t.orientation[k] = bitcast<f32>(wordIn(buffer, (w.orientation + k) * c + i));
        }
        t.angular = readVector(buffer, i, w.angular);
    }
    return t;
}

fn storeTurning(buffer: u32, i: u32, t: Turning) {
    let c = params.counts.x;
    let w = params.words;
    if (w.angle != NONE) {
        setWordIn(buffer, w.angle * c + i, bitcast<u32>(t.angle));
        setWordIn(buffer, w.spin * c + i, bitcast<u32>(t.spin));
    }
    if (w.orientation != NONE) {
        for (var k = 0u; k < 4u; k++) {
            setWordIn(buffer, (w.orientation + k) * c + i, bitcast<u32>(t.orientation[k]));
        }
        for (var k = 0u; k < 3u; k++) {
            setWordIn(buffer, (w.angular + k) * c + i, bitcast<u32>(t.angular[k]));
        }
    }
}

// A turn as the core's MoveAngular: the angle by the spin and the
// orientation by the angular velocity, over the angular factors (decay,
// carry), the carry scaled by the rotation speed; the spin and angular
// velocity decayed.
fn turnState(t: Turning, f: vec2<f32>, scale: f32) -> Turning {
    var out = t;
    out.angle = t.angle + t.spin * (f.y * scale);
    out.spin = t.spin * f.x;
    out.orientation = turnBy(t.orientation, t.angular * (f.y * scale));
    out.angular = t.angular * f.x;
    return out;
}

// Turns particle i of a buffer (0 the current, 1 the other) by turnState.
fn turn(buffer: u32, i: u32, f: vec2<f32>, scale: f32) {
    storeTurning(buffer, i, turnState(loadTurning(buffer, i), f, scale));
}

// A unit quaternion turned by a rotation vector (the turn's axis times
// its angle) and normalized, as the core's TurnBy.
fn turnBy(q: vec4<f32>, theta: vec3<f32>) -> vec4<f32> {
    let angle = sqrt(theta.x * theta.x + theta.y * theta.y + theta.z * theta.z);
    if (!(angle > 0.0)) {
        return q;
    }
    let half = sinCos(0.5 * angle);
    let r = vec4<f32>(theta * (half.x / angle), half.y);
    let out = vec4<f32>(r.w * q.x + r.x * q.w + r.y * q.z - r.z * q.y,
                        r.w * q.y - r.x * q.z + r.y * q.w + r.z * q.x,
                        r.w * q.z + r.x * q.y - r.y * q.x + r.z * q.w,
                        r.w * q.w - r.x * q.x - r.y * q.y - r.z * q.z);
    let length = sqrt(out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w);
    return out / length;
}

// Writes a value's components (those it has, from its first word) at
// place at of the other buffer; nothing for a value not stored.
fn storeValue(first: u32, components: u32, at: u32, value: vec4<f32>) {
    let c = params.counts.x;
    if (first == NONE) {
        return;
    }
    for (var k = 0u; k < components; k++) {
        moved[(first + k) * c + at] = bitcast<u32>(value[k]);
    }
}

// A birth's orientation from its words, as the core draws it: random, by
// Euler angles from its source (about X, then Y, then Z), or none.
fn orientationOf(age: f32, words: vec4<u32>) -> vec4<f32> {
    let mode = params.turns.x;
    if (mode == ORIENTATION_RANDOM) {
        let u = unitFloat(words.x);
        let low = sqrt(1.0 - u);
        let high = sqrt(u);
        let a1 = TWO_PI * unitFloat(words.y);
        let a2 = TWO_PI * unitFloat(words.z);
        return vec4<f32>(low * sin(a1), low * cos(a1), high * sin(a2), high * cos(a2));
    }
    if (mode == ORIENTATION_EULER) {
        let half = 0.5 * drawVector(SOURCE_ORIENTATION, age, words).xyz;
        let s = sin(half);
        let c = cos(half);
        return vec4<f32>(s.x * c.y * c.z - c.x * s.y * s.z, c.x * s.y * c.z + s.x * c.y * s.z,
                         c.x * c.y * s.z - s.x * s.y * c.z, c.x * c.y * c.z + s.x * s.y * s.z);
    }
    return vec4<f32>(0.0, 0.0, 0.0, 1.0);
}

// A birth's angle, spin, orientation and angular velocity as drawn, those
// the emitter stores, as the core's AppendValues draws them.
fn bornTurning(j: u32) -> Turning {
    let k = birthKey(j);
    let age = bitcast<f32>(birthWord(j, 2u).x);
    let w = params.words;
    var t = Turning(0.0, 0.0, vec4<f32>(0.0, 0.0, 0.0, 1.0), vec3<f32>(0.0));
    if (w.angle != NONE) {
        t.angle = drawVector(SOURCE_ANGLE, age, draw(ANGLE_SALT, k.x, k.y)).x;
        t.spin = drawVector(SOURCE_SPIN, age, draw(SPIN_SALT, k.x, k.y)).x;
    }
    if (w.orientation != NONE) {
        t.orientation = orientationOf(age, draw(ORIENTATION_SALT, k.x, k.y));
        t.angular = drawVector(SOURCE_ANGULAR, age, draw(ANGULAR_SALT, k.x, k.y)).xyz;
    }
    return t;
}

// A birth's drawn values, each the emitter stores, as the core's
// AppendValues and StoreWords draw them: size, colour, angle and spin,
// orientation and angular velocity, drag scale and custom values over
// the birth's source age, frame and mesh index.
fn storeValues(j: u32, at: u32) {
    let c = params.counts.x;
    let k = birthKey(j);
    let age = bitcast<f32>(birthWord(j, 2u).x);
    let w = params.words;
    if (w.size != NONE) {
        storeValue(w.size, 3u, at, vec4<f32>(bornSize(j), 0.0));
    }
    if (w.color != NONE) {
        storeValue(w.color, 4u, at, bornColor(j));
    }
    if (w.frame != NONE) {
        moved[w.frame * c + at] = drawIndex(params.draws[0], FRAME_SALT, k.x, k.y);
    }
    if (w.mesh != NONE) {
        moved[w.mesh * c + at] = drawIndex(params.draws[1], MESH_SALT, k.x, k.y);
    }
    storeTurning(1u, at, bornTurning(j));
    if (w.drag != NONE) {
        moved[w.drag * c + at] = bitcast<u32>(dragScaleOf(j));
    }
    for (var i = 0u; i < 8u; i++) {
        let first = w.custom[i / 4u][i % 4u];
        if (first != NONE) {
            let s = SOURCE_CUSTOM + i;
            let value = drawVector(s, age, draw(CUSTOM_SALT + i, k.x, k.y));
            storeValue(first, params.sources[s].modes.z, at, value);
        }
    }
    // No neighbors until the next step's query, as the core's births.
    if (w.neighbors.x != NONE) {
        moved[w.neighbors.x * c + at] = 0u;
        storeValue(w.neighbors.y, 3u, at, vec4<f32>(0.0));
        storeValue(w.neighbors.z, 3u, at, vec4<f32>(0.0));
    }
}

// A birth's colour (ones where the emitter stores none): drawn, or a host
// particle's as it gives it.
fn bornColor(j: u32) -> vec4<f32> {
    if (params.words.color == NONE) {
        return vec4<f32>(1.0);
    }
    let host = birthWord(j, 2u).y;
    if (host != NONE) {
        let record = hostBirthAt(host);
        if ((bitcast<u32>(record.place.w) & HOST_GIVES_COLOR) != 0u) {
            return record.color;
        }
    }
    let k = birthKey(j);
    let age = bitcast<f32>(birthWord(j, 2u).x);
    return drawVector(SOURCE_COLOR, age, draw(COLOR_SALT, k.x, k.y));
}

// A size's axes past those a rigid shape takes made copies of X, as the
// core's mvfxTieSize.
fn tiedSize(size: vec3<f32>) -> vec3<f32> {
    if (params.rigid.y > 0u && params.rigid.x == RIGID_SPHERE) {
        return vec3<f32>(size.x);
    }
    if (params.rigid.y > 0u && params.rigid.x == RIGID_CAPSULE) {
        return vec3<f32>(size.x, size.y, size.x);
    }
    return size;
}

// A birth's size (ones where the emitter stores none), as its colour, its
// axes tied to a rigid shape's.
fn bornSize(j: u32) -> vec3<f32> {
    if (params.words.size == NONE) {
        return vec3<f32>(1.0);
    }
    let host = birthWord(j, 2u).y;
    if (host != NONE) {
        let record = hostBirthAt(host);
        if ((bitcast<u32>(record.place.w) & HOST_GIVES_SIZE) != 0u) {
            return tiedSize(record.size.xyz);
        }
    }
    let k = birthKey(j);
    let age = bitcast<f32>(birthWord(j, 2u).x);
    return tiedSize(drawVector(SOURCE_SIZE, age, draw(SIZE_SALT, k.x, k.y)).xyz);
}

// Kill births: each birth with room killed on its way to the plan's end
// (a kill rule, an attractor's kill radius, a contact that kills, a
// contact's life loss that ends its life within the plan, a pour into a
// water surface) loses its room's mark (2), so the scan keeps it not; it
// held its room for the step, as a killed particle does until the step's
// end. One whose contacts make an event, or that crossed a water surface,
// is marked so too. The workgroups stride over
// the births as update's over the particles.
@compute @workgroup_size(64, 1, 1)
fn killBirths(@builtin(global_invocation_id) id: vec3<u32>) {
    if (params.forceHead.y == 0u && params.collisionHead.z == 0u && params.fateHead.x == 0u &&
        params.water.x == NONE) {
        return;
    }
    for (var j = id.x; j < birthCount(); j += params.modes.z * 64u) {
        if (accepted[j] != 1u) {
            continue;
        }
        var p = vec3<f32>(0.0);
        var v = vec3<f32>(0.0);
        var born = vec3<f32>(0.0);
        var bornVelocity = vec3<f32>(0.0);
        var turning = Turning();
        let fate = bornFacts(j, &born, &bornVelocity, &p, &v, &turning);
        // Its crossing of the water surface, a pour killing it.
        let crossed = waterMeet(waterTouch(born), born, p).crossed;
        var mark = 1u;
        if (fate.killed || !outlives(fate.death.x, fate.death.y) ||
            (crossed && params.waterCross.y != 0u)) {
            mark = 2u;
            // Kept by no scatter: its places for the ribbons pass.
            storeBirthPlaces(j, born, p);
        }
        // Its contacts make an event (BIRTH_CONTACT), and its crossing
        // of the water surface (BIRTH_CROSSED), for the events' count; a
        // rigid body's contacts dropped past the most above, which offsets
        // sums (MVFX_GPU_RIGID_CAPPED).
        accepted[j] = mark | select(0u, BIRTH_CONTACT, fate.contacted) |
                      select(0u, BIRTH_CROSSED, crossed) |
                      (min(fate.capped, 0xFFFFu) << 8u) | (min(fate.searches, 0xFFu) << 24u);
    }
}
