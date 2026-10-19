// The sim kernel's step passes (spliced into sim.wgsl): update, the
// compaction's scans, the births' values, killBirths, scatter, measure
// and room.

// Update: each live particle moved, the workgroups (at most a dispatch's
// along one axis) striding over them.
@compute @workgroup_size(64, 1, 1)
fn update(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = counts[0];
    for (var i = id.x; i < live; i += params.modes.z * 64u) {
        moveLive(i);
    }
}

// A live particle shifted with the anchor and moved over the plan; whether
// it outlives the plan. One whose life ends within the plan is taken out
// unmoved, as the CPU path takes the ended out before the moves: it dies
// where the step found it, marked LIVE_ENDED.
// The alive word's bits: kept (bit 0), and taken out unmoved, ended before
// the moves (bit 1), which the ribbons pass tells from killed.
const LIVE_ENDED: u32 = 2u;

fn moveLive(i: u32) {
    let c = params.counts.x;
    let ending = liveDeath(i);
    if (!outlives(ending.x, ending.y)) {
        source[params.words.alive * c + i] = LIVE_ENDED;
        noteFacts(i, EVENT_ENDED, Hit(0.0, vec3<f32>(0.0), vec3<f32>(0.0)), 0.0);
        return;
    }
    let start = readVector(0u, i, params.words.place) + params.shift.xyz;
    if (unitPoints() > 1u) {
        // A unit's point: shifted and turned here, moved by the unit pass
        // (sim/units.wgsl) after.
        holdUnitPoint(i, start);
        return;
    }
    var a = vec3<f32>(0.0);
    var wind = 0.0;
    forcesAt(start, 0.0, &a, &wind);
    var scale = 1.0;
    if (params.words.drag != NONE) {
        scale = bitcast<f32>(source[params.words.drag * c + i]);
    }
    let seconds = secondsOf(vec2<u32>(params.counts.z, 0u));
    let f = factorsFor(wind, scale, seconds, true);
    let birth = vec2<u32>(source[params.words.birth * c + i], source[(params.words.birth + 1u) * c + i]);
    let death = vec2<u32>(source[params.words.death * c + i], source[(params.words.death + 1u) * c + i]);
    let inverseLife = bitcast<f32>(source[params.words.inverseLife * c + i]);
    let age = ageAt(birth, inverseLife);
    let v0 = limitSpeed(readVector(0u, i, params.words.velocity), age, params.angular.z);
    var p = moveByModules(start, start + v0 * f.y + a * f.z, age, seconds);
    var v = v0 * f.x + a * f.y;
    let fate = meet(start, &p, &v, birth, inverseLife, death);
    noteFacts(i, select(0u, EVENT_CONTACT, fate.contacted), fate.first, inverseLife);
    for (var k = 0u; k < 3u; k++) {
        source[(params.words.place + k) * c + i] = bitcast<u32>(p[k]);
        source[(params.words.velocity + k) * c + i] = bitcast<u32>(v[k]);
    }
    let collisions = params.words.collisions;
    var contacts = 0u;
    if (collisions != NONE) {
        let count = source[collisions * c + i];
        contacts = select(count + fate.contacts, 0xFFFFFFFFu, count > 0xFFFFFFFFu - fate.contacts);
        source[collisions * c + i] = contacts;
    }
    source[params.words.inverseLife * c + i] = bitcast<u32>(fate.inverseLife);
    turn(0u, i, params.angular.xy, lifeAt(LIFE_ROTATION, ageAt(birth, fate.inverseLife)));
    source[params.words.death * c + i] = fate.death.x;
    source[(params.words.death + 1u) * c + i] = fate.death.y;
    // Its age at the plan's end, in ticks.
    let endLow = params.start.x + params.counts.z;
    let endHigh = params.start.y + select(0u, 1u, endLow < params.start.x);
    let ageTicks = vec2<u32>(endLow - birth.x, endHigh - birth.y - select(0u, 1u, endLow < birth.x));
    source[params.words.alive * c + i] =
        select(0u, 1u, outlives(fate.death.x, fate.death.y) && !fate.killed &&
                           !killed(start, p, v, ageTicks, contacts));
}

// A live unit's point at the plan's start: its place shifted with the
// anchor (and its pin's anchor, held in the particles' space), no facts
// yet, and kept until the unit pass's substeps kill it.
fn holdUnitPoint(i: u32, start: vec3<f32>) {
    let c = params.counts.x;
    noteFacts(i, 0u, Hit(0.0, vec3<f32>(0.0), vec3<f32>(0.0)), 0.0);
    for (var k = 0u; k < 3u; k++) {
        source[(params.words.place + k) * c + i] = bitcast<u32>(start[k]);
    }
    // The anchors held in the particles' space move with the places, as
    // the frames' origins do for units held to frames.
    var held = NONE;
    if (params.unitArrays.x != NONE && params.unitPins.y == 0u) {
        held = params.unitArrays.x;
    } else if (isFramed()) {
        held = params.unitArrays.y;
    }
    if (held != NONE) {
        let anchor = readVector(0u, i, held) + params.shift.xyz;
        for (var k = 0u; k < 3u; k++) {
            source[(held + k) * c + i] = bitcast<u32>(anchor[k]);
        }
    }
    source[params.words.alive * c + i] = 1u;
}

// Adds keep's values of the workgroup in order: each invocation's sum of
// those before it.
fn exclusiveScan(local: u32, keep: u32) -> u32 {
    scan[local] = keep;
    workgroupBarrier();
    for (var offset = 1u; offset < CHUNK; offset <<= 1u) {
        var add = 0u;
        if (local >= offset) {
            add = scan[local - offset];
        }
        workgroupBarrier();
        scan[local] += add;
        workgroupBarrier();
    }
    return scan[local] - keep;
}

// Whether element i of the step's sequence is kept: the live particles
// first (live of them), those that outlive the plan; then the plan's
// births, those with room that outlive it, their death from the plan's
// start in two words past its ticks when the high word is set or the low
// passes them.
fn keepAt(i: u32, live: u32) -> u32 {
    let c = params.counts.x;
    if (i < live) {
        return source[params.words.alive * c + i] & 1u;
    }
    let j = i - live;
    if (j >= birthCount()) {
        return 0u;
    }
    let b0 = birthWord(j, 0u);
    let b1 = birthWord(j, 1u);
    return select(0u, 1u, (accepted[j] & 3u) == 1u && (b1.x > 0u || b0.w > params.counts.z));
}

// The workgroups of the reduce and scatter entries: the sequence is at
// most the capacity's live particles and twice the capacity's births.
fn blockCount() -> u32 {
    return (3u * params.counts.x + BLOCK - 1u) / BLOCK;
}

// Reduce: each workgroup's count of kept elements, in blocks.
@compute @workgroup_size(256, 1, 1)
fn reduce(@builtin(local_invocation_index) local: u32,
          @builtin(workgroup_id) group: vec3<u32>) {
    let live = counts[0];
    let first = group.x * BLOCK + local * ITEMS;
    var kept = 0u;
    for (var k = 0u; k < ITEMS; k++) {
        kept += keepAt(first + k, live);
    }
    let before = exclusiveScan(local, kept);
    if (local == CHUNK - 1u) {
        blocks[group.x] = before + kept;
    }
}

// Offsets, in one workgroup: the blocks' counts made each block's first
// place, in order; the live count before the step kept in counts[1], the
// new one in counts[0], those past the capacity in counts[2].
@compute @workgroup_size(256, 1, 1)
fn offsets(@builtin(local_invocation_index) local: u32) {
    let c = params.counts.x;
    let n = blockCount();
    if (local == 0u) {
        carried = 0u;
    }
    for (var base = 0u; base < n; base += CHUNK) {
        let k = base + local;
        var count = 0u;
        if (k < n) {
            count = blocks[k];
        }
        let before = exclusiveScan(local, count);
        if (k < n) {
            blocks[k] = carried + before;
        }
        workgroupBarrier();
        if (local == CHUNK - 1u) {
            carried += before + count;
        }
        workgroupBarrier();
    }
    if (local == 0u) {
        counts[1] = counts[0];
        counts[0] = min(carried, c);
        counts[2] = carried - min(carried, c);
    }
}

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
    return bornFacts(j, &born, &bornVelocity, place, velocity);
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
// move, as its birth event gives them.
fn bornFacts(j: u32, born: ptr<function, vec3<f32>>, bornVelocity: ptr<function, vec3<f32>>,
             place: ptr<function, vec3<f32>>, velocity: ptr<function, vec3<f32>>) -> Fate {
    let b0 = birthWord(j, 0u);
    let b1 = birthWord(j, 1u);
    var p = vec3<f32>(0.0);
    var v = vec3<f32>(0.0);
    bornPlace(j, true, &p, &v);
    *born = p;
    *bornVelocity = v;
    var a = vec3<f32>(0.0);
    var wind = 0.0;
    forcesAt(p, secondsOf(vec2<u32>(b0.z, 0u)), &a, &wind);
    let seconds = remainderOf(j);
    let f = factorsFor(wind, dragScaleOf(j), seconds, false);
    // Born at age 0, its speed limit's decay its own.
    v = limitSpeed(v, 0.0, factors(bitcast<f32>(params.turns.w), seconds).x);
    *place = moveByModules(p, p + v * f.y + a * f.z, 0.0, seconds);
    *velocity = v * f.x + a * f.y;
    var birth = vec2<u32>(0u);
    var death = vec2<u32>(0u);
    birthTicks(b0, b1, &birth, &death);
    var fate = meet(p, place, velocity, birth, bitcast<f32>(b1.y), death);
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
    let fate = bornFacts(j, &born, &bornVelocity, &moved_place, &moved_velocity);
    if (params.unitArrays.x != NONE && isPinned(j % unitPoints())) {
        // A pinned point stays where it was born, as the core's AppendPoint
        // (its move's contacts and kills still count).
        moved_place = born;
        moved_velocity = vec3<f32>(0.0);
    }
    storeBirthPlaces(j, born, moved_place);
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
    storeValues(j, at);
    // Born at age 0, turned over the rest of the plan.
    let angular = factors(bitcast<f32>(params.turns.y), remainderOf(j)).xy;
    turn(1u, at, angular, lifeAt(LIFE_ROTATION, 0.0));
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
    let away = square > 0.0 && square < LARGEST;
    let unit = out * inverseSqrt(square);
    var p = start + scale * (place - start) + added * params.modules[0].xyz;
    if (away) {
        p += radial * unit;
    }
    let angle = lifeAt(LIFE_ORBIT_RATE, age) * h;
    if (angle != 0.0) {
        let center = params.modules[2].xyz;
        let q = vec4<f32>(params.modules[1].xyz * sin(0.5 * angle), cos(0.5 * angle));
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

// Turns particle i of a buffer as the core's MoveAngular: its angle by
// its spin and its orientation by its angular velocity, over the angular
// factors (decay, carry), the carry scaled by the rotation speed; its
// spin and angular velocity decayed.
fn turn(buffer: u32, i: u32, f: vec2<f32>, scale: f32) {
    let c = params.counts.x;
    let w = params.words;
    if (w.angle != NONE) {
        let angle = bitcast<f32>(wordIn(buffer, w.angle * c + i));
        let spin = bitcast<f32>(wordIn(buffer, w.spin * c + i));
        setWordIn(buffer, w.angle * c + i, bitcast<u32>(angle + spin * (f.y * scale)));
        setWordIn(buffer, w.spin * c + i, bitcast<u32>(spin * f.x));
    }
    if (w.orientation != NONE) {
        var q = vec4<f32>(0.0);
        for (var k = 0u; k < 4u; k++) {
            q[k] = bitcast<f32>(wordIn(buffer, (w.orientation + k) * c + i));
        }
        let omega = readVector(buffer, i, w.angular);
        q = turnBy(q, omega * (f.y * scale));
        for (var k = 0u; k < 4u; k++) {
            setWordIn(buffer, (w.orientation + k) * c + i, bitcast<u32>(q[k]));
        }
        for (var k = 0u; k < 3u; k++) {
            setWordIn(buffer, (w.angular + k) * c + i, bitcast<u32>(omega[k] * f.x));
        }
    }
}

// A unit quaternion turned by a rotation vector (the turn's axis times
// its angle) and normalized, as the core's TurnBy.
fn turnBy(q: vec4<f32>, theta: vec3<f32>) -> vec4<f32> {
    let angle = sqrt(theta.x * theta.x + theta.y * theta.y + theta.z * theta.z);
    if (!(angle > 0.0)) {
        return q;
    }
    let s = sin(0.5 * angle) / angle;
    let r = vec4<f32>(theta * s, cos(0.5 * angle));
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
    if (w.angle != NONE) {
        moved[w.angle * c + at] =
            bitcast<u32>(drawVector(SOURCE_ANGLE, age, draw(ANGLE_SALT, k.x, k.y)).x);
        moved[w.spin * c + at] =
            bitcast<u32>(drawVector(SOURCE_SPIN, age, draw(SPIN_SALT, k.x, k.y)).x);
    }
    if (w.orientation != NONE) {
        storeValue(w.orientation, 4u, at, orientationOf(age, draw(ORIENTATION_SALT, k.x, k.y)));
        storeValue(w.angular, 3u, at, drawVector(SOURCE_ANGULAR, age, draw(ANGULAR_SALT, k.x, k.y)));
    }
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

// A birth's size (ones where the emitter stores none), as its colour.
fn bornSize(j: u32) -> vec3<f32> {
    if (params.words.size == NONE) {
        return vec3<f32>(1.0);
    }
    let host = birthWord(j, 2u).y;
    if (host != NONE) {
        let record = hostBirthAt(host);
        if ((bitcast<u32>(record.place.w) & HOST_GIVES_SIZE) != 0u) {
            return record.size.xyz;
        }
    }
    let k = birthKey(j);
    let age = bitcast<f32>(birthWord(j, 2u).x);
    return drawVector(SOURCE_SIZE, age, draw(SIZE_SALT, k.x, k.y)).xyz;
}

// Kill births: each birth with room killed on its way to the plan's end
// (a kill rule, an attractor's kill radius, a contact that kills, a
// contact's life loss that ends its life within the plan) loses its
// room's mark (2), so the scan keeps it not; it held its room for the
// step, as a killed particle does until the step's end. One whose
// contacts make an event is marked so too. The workgroups stride over
// the births as update's over the particles.
@compute @workgroup_size(64, 1, 1)
fn killBirths(@builtin(global_invocation_id) id: vec3<u32>) {
    if (params.forceHead.y == 0u && params.collisionHead.z == 0u && params.fateHead.x == 0u) {
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
        let fate = bornFacts(j, &born, &bornVelocity, &p, &v);
        var mark = 1u;
        if (fate.killed || !outlives(fate.death.x, fate.death.y)) {
            mark = 2u;
            // Kept by no scatter: its places for the ribbons pass.
            storeBirthPlaces(j, born, p);
        }
        // Its contacts make an event (BIRTH_CONTACT), for the events'
        // count.
        accepted[j] = mark | select(0u, BIRTH_CONTACT, fate.contacted);
    }
}

// A birth's mark beside its room's: its contacts make an event.
const BIRTH_CONTACT: u32 = 4u;

// The box around a box and a place, by comparisons that pass a NaN over.
fn lowerOf(low: vec3<f32>, p: vec3<f32>) -> vec3<f32> {
    return select(low, p, p < low);
}

fn higherOf(high: vec3<f32>, p: vec3<f32>) -> vec3<f32> {
    return select(high, p, p > high);
}

// The workgroup's box from each invocation's, by a fixed tree; whole in
// boxLow[0] and boxHigh[0], the largest growth in boxHigh[0].w.
fn reduceBox(local: u32, low: vec3<f32>, high: vec3<f32>, growth: f32) {
    boxLow[local] = vec4<f32>(low, 0.0);
    boxHigh[local] = vec4<f32>(high, growth);
    workgroupBarrier();
    for (var half = CHUNK / 2u; half > 0u; half >>= 1u) {
        if (local < half) {
            boxLow[local] = vec4<f32>(lowerOf(boxLow[local].xyz, boxLow[local + half].xyz), 0.0);
            boxHigh[local] = vec4<f32>(higherOf(boxHigh[local].xyz, boxHigh[local + half].xyz),
                                       max(boxHigh[local].w, boxHigh[local + half].w));
        }
        workgroupBarrier();
    }
}

// The empty box: min at the largest binary32, max at the least.
const EMPTY_LOW: f32 = 3.40282347e38;

// Scatter: each kept element to its place in the other buffer, its block's
// first place and those kept before it in the block; a live particle
// moved, a birth spawned; none past the capacity.
@compute @workgroup_size(256, 1, 1)
fn scatter(@builtin(local_invocation_index) local: u32,
           @builtin(workgroup_id) group: vec3<u32>) {
    let c = params.counts.x;
    let live = counts[1];
    let first = group.x * BLOCK + local * ITEMS;
    var keep: array<u32, ITEMS>;
    var kept = 0u;
    for (var k = 0u; k < ITEMS; k++) {
        keep[k] = keepAt(first + k, live);
        kept += keep[k];
    }
    var at = blocks[group.x] + exclusiveScan(local, kept);
    var low = vec3<f32>(EMPTY_LOW);
    var high = vec3<f32>(-EMPTY_LOW);
    var growth = 0.0;
    for (var k = 0u; k < ITEMS; k++) {
        let i = first + k;
        if (keep[k] == 1u && at < c) {
            var p = vec3<f32>(0.0);
            if (i < live) {
                for (var w = 0u; w < params.words.alive; w++) {
                    moved[w * c + at] = source[w * c + i];
                }
                p = readVector(0u, i, params.words.place);
            } else {
                p = spawnAt(i - live, at);
            }
            low = lowerOf(low, p);
            high = higherOf(high, p);
            growth = max(growth, growthOf(at));
        }
        if (keep[k] == 1u && i >= live && params.words.ribbon != NONE) {
            // Where the birth went, for the ribbons passes after.
            accepted[2u * c + i - live] = at;
        }
        at += keep[k];
    }
    reduceBox(local, low, high, growth);
    if (local == 0u) {
        let n = blockCount();
        let box = n + 6u * group.x;
        for (var k = 0u; k < 3u; k++) {
            blocks[box + k] = bitcast<u32>(boxLow[0][k]);
            blocks[box + 3u + k] = bitcast<u32>(boxHigh[0][k]);
        }
        blocks[7u * n + group.x] = bitcast<u32>(boxHigh[0].w);
    }
}

// How much a kept particle grows the bounds: its largest size component
// times the most the size over life scales that axis, 1 for a size it
// does not store, as the CPU path's bounds (the largest over particles of
// each axis's stored size is the largest product, the factors being
// positive).
fn growthOf(at: u32) -> f32 {
    var size = vec3<f32>(1.0);
    if (params.words.size != NONE) {
        size = abs(readVector(1u, at, params.words.size));
    }
    let g = size * params.growth.xyz;
    return max(g.x, max(g.y, g.z));
}

// Rebox, for an emitter whose contacts moved its particles after the
// scatter: each block's box again from the places of the compacted pool,
// a block of it a workgroup (dispatched a block each), the blocks past the
// pool's empty; the growth by size stays the scatter's (contacts change
// no size).
@compute @workgroup_size(256, 1, 1)
fn rebox(@builtin(local_invocation_index) local: u32,
         @builtin(workgroup_id) group: vec3<u32>) {
    let c = params.counts.x;
    let live = counts[0];
    let n = blockCount();
    for (var g = group.x; g < n; g += n) {
        var low = vec3<f32>(EMPTY_LOW);
        var high = vec3<f32>(-EMPTY_LOW);
        for (var k = 0u; k < ITEMS; k++) {
            let i = g * BLOCK + local * ITEMS + k;
            if (i < live) {
                let p = readVector(1u, i, params.words.place);
                low = lowerOf(low, p);
                high = higherOf(high, p);
            }
        }
        reduceBox(local, low, high, 0.0);
        if (local == 0u) {
            let box = n + 6u * g;
            for (var k = 0u; k < 3u; k++) {
                blocks[box + k] = bitcast<u32>(boxLow[0][k]);
                blocks[box + 3u + k] = bitcast<u32>(boxHigh[0][k]);
            }
        }
        workgroupBarrier();
    }
}

// Measure, in one workgroup: the blocks' boxes reduced by a fixed tree to
// the emitter's, in the particles' space, into the statistics record with
// the growth by the largest size, with its margin.
@compute @workgroup_size(256, 1, 1)
fn measure(@builtin(local_invocation_index) local: u32) {
    let n = blockCount();
    var low = vec3<f32>(EMPTY_LOW);
    var high = vec3<f32>(-EMPTY_LOW);
    var growth = 0.0;
    for (var g = local; g < n; g += CHUNK) {
        let box = n + 6u * g;
        growth = max(growth, bitcast<f32>(blocks[7u * n + g]));
        low = lowerOf(low, vec3<f32>(bitcast<f32>(blocks[box]), bitcast<f32>(blocks[box + 1u]),
                                     bitcast<f32>(blocks[box + 2u])));
        high = higherOf(high, vec3<f32>(bitcast<f32>(blocks[box + 3u]),
                                        bitcast<f32>(blocks[box + 4u]),
                                        bitcast<f32>(blocks[box + 5u])));
    }
    reduceBox(local, low, high, growth);
    if (local == 0u) {
        counts[3] = bitcast<u32>(boxHigh[0].w * params.growth.w);
        for (var k = 0u; k < 3u; k++) {
            counts[4u + k] = bitcast<u32>(boxLow[0][k]);
            counts[7u + k] = bitcast<u32>(boxHigh[0][k]);
        }
    }
}

// Whether a birth of the plan dies by tick t of the plan, its death in two
// words from the plan's start.
fn birthDiesBy(j: u32, t: u32) -> bool {
    let b0 = birthWord(j, 0u);
    let b1 = birthWord(j, 1u);
    return b1.x == 0u && b0.w <= t;
}

// A live particle's death tick, absolute in two words: a unit point's its
// unit's first point's, as all of a unit's points die together (a
// contact's life loss can move a later point's alone).
fn liveDeath(i: u32) -> vec2<u32> {
    let c = params.counts.x;
    let first = i - i % unitPoints();
    return vec2<u32>(source[params.words.death * c + first],
                     source[(params.words.death + 1u) * c + first]);
}

// Whether a live particle dies by tick t of the plan, its death absolute.
fn liveDiesBy(i: u32, t: u32) -> bool {
    let death = liveDeath(i);
    let low = death.x;
    let high = death.y;
    let tickLow = params.start.x + t;
    let tickHigh = params.start.y + select(0u, 1u, tickLow < params.start.x);
    return high < tickHigh || (high == tickHigh && low <= tickLow);
}

// The tick of the plan by which live particle i dies (0 for one dead
// before the plan), ROOM_NEVER past 2^32 ticks of it.
const ROOM_NEVER: u32 = 0xFFFFFFFFu;

fn liveDeathTick(i: u32) -> u32 {
    let death = liveDeath(i);
    let start = params.start.xy;
    if (death.y < start.y || (death.y == start.y && death.x < start.x)) {
        return 0u;
    }
    let since = subtract64(death, start);
    return select(ROOM_NEVER, since.x, since.y == 0u && since.x != ROOM_NEVER);
}

// Counts in accepted, at each of the plan's births, the live particles
// that die by its tick and not by the birth's before it (each death at the
// first birth whose tick reaches it): each invocation keeps a run of the
// births (count over the workgroup's invocations, in order), the deaths
// coming through workgroup memory a tile at a time, found in its run by
// a binary search; it returns the deaths before its run.
fn liveDeathsByBirth(local: u32, live: u32, count: u32) -> u32 {
    let per = (count + CHUNK - 1u) / CHUNK;
    let lo = min(local * per, count);
    let hi = min(lo + per, count);
    for (var j = lo; j < hi; j++) {
        accepted[j] = 0u;
    }
    let before = select(0u, birthWord((max(lo, 1u) - 1u), 0u).z, lo > 0u);
    let last = select(0u, birthWord((max(hi, 1u) - 1u), 0u).z, hi > lo);
    var base = 0u;
    for (var tile = 0u; tile < live; tile += CHUNK) {
        let i = tile + local;
        var tick = ROOM_NEVER;
        if (i < live) {
            tick = liveDeathTick(i);
        }
        scan[local] = tick;
        workgroupBarrier();
        let n = min(CHUNK, live - tile);
        for (var k = 0u; k < n; k++) {
            let d = scan[k];
            if (hi == lo || d == ROOM_NEVER) {
                continue;
            }
            if (lo > 0u && d <= before) {
                base += 1u;
            } else if (d <= last) {
                var a = lo;
                var b = hi - 1u;
                while (a < b) {
                    let m = (a + b) / 2u;
                    if (birthWord(m, 0u).z >= d) {
                        b = m;
                    } else {
                        a = m + 1u;
                    }
                }
                accepted[a] += 1u;
            }
        }
        workgroupBarrier();
    }
    return base;
}

// Room by the allowance, when no birth dies by the last birth's tick:
// birth p is taken as the CPU path's full step takes it
// exactly when a(p) - p - 1 is at least every earlier birth's and 0,
// a(p) the capacity less the live particles plus those dead by its tick:
// each birth's value from the deaths counted, its run's least, the runs'
// least scanned, each birth taken against the least before it.
fn roomByAllowance(local: u32, live: u32, count: u32) {
    let c = params.counts.x;
    let per = (count + CHUNK - 1u) / CHUNK;
    let lo = min(local * per, count);
    let hi = min(lo + per, count);
    var gone = liveDeathsByBirth(local, live, count);
    var least = 0x7FFFFFFF;
    for (var p = lo; p < hi; p++) {
        gone += accepted[p];
        let v = i32(c - live + gone) - i32(p) - 1;
        accepted[p] = bitcast<u32>(v);
        least = min(least, v);
    }
    // The runs' least before each run, from 0.
    scan[local] = bitcast<u32>(least);
    workgroupBarrier();
    for (var offset = 1u; offset < CHUNK; offset <<= 1u) {
        var other = 0x7FFFFFFF;
        if (local >= offset) {
            other = bitcast<i32>(scan[local - offset]);
        }
        workgroupBarrier();
        scan[local] = bitcast<u32>(min(bitcast<i32>(scan[local]), other));
        workgroupBarrier();
    }
    var m = 0;
    if (local > 0u) {
        m = min(0, bitcast<i32>(scan[local - 1u]));
    }
    for (var p = lo; p < hi; p++) {
        let v = bitcast<i32>(accepted[p]);
        accepted[p] = select(0u, 1u, v >= m);
        m = min(m, v);
    }
}

// Room by a walk, when a birth dies by the last birth's tick: one
// invocation takes the births in order, the particles held less the
// deaths counted at each, a birth taken while they are under the
// capacity; a taken birth dying by the last birth's tick counted, as the
// live particles' deaths are, at the first later birth whose tick
// reaches its death. Linear in the births, a binary search for each that
// dies within the step.
fn roomByWalk(local: u32, live: u32, count: u32) {
    let c = params.counts.x;
    _ = liveDeathsByBirth(local, live, count);
    storageBarrier();
    workgroupBarrier();
    if (local != 0u) {
        return;
    }
    let last = birthWord((count - 1u), 0u).z;
    var held = live;
    for (var p = 0u; p < count; p++) {
        held -= accepted[p];
        let take = held < c;
        accepted[p] = select(0u, 1u, take);
        if (!take) {
            continue;
        }
        held += 1u;
        if (birthDiesBy(p, last)) {
            let d = birthWord(p, 0u).w;
            var a = p + 1u;
            var b = count;
            while (a < b) {
                let m = (a + b) / 2u;
                if (birthWord(m, 0u).z >= d) {
                    b = m;
                } else {
                    a = m + 1u;
                }
            }
            if (a < count) {
                accepted[a] += 1u;
            }
        }
    }
}

// Room: which of the plan's births the emitter takes, as the CPU path's
// full step takes them. When the live particles and the births fit the
// capacity, all; when no birth dies by the last birth's tick, by the
// allowance (roomByAllowance); else by a walk (roomByWalk). It runs
// before update, as the CPU path takes a full step's
// births before it moves: the deaths are those the step began with (and
// the plan's for births), not those its contacts shorten or its kills.
@compute @workgroup_size(256, 1, 1)
fn room(@builtin(local_invocation_index) local: u32) {
    let c = params.counts.x;
    if (local == 0u) {
        liveCount = counts[0];
        taken = birthCount();
    }
    let live = workgroupUniformLoad(&liveCount);
    let count = workgroupUniformLoad(&taken);
    if (live + count <= c) {
        for (var j = local; j < count; j += CHUNK) {
            accepted[j] = 1u;
        }
        return;
    }
    let lastTick = birthWord((count - 1u), 0u).z;
    var within = 0u;
    for (var j = local; j < count; j += CHUNK) {
        within += select(0u, 1u, birthDiesBy(j, lastTick));
    }
    let earlier = exclusiveScan(local, within);
    if (local == CHUNK - 1u) {
        dead = earlier + within;
    }
    workgroupBarrier();
    if (workgroupUniformLoad(&dead) == 0u) {
        roomByAllowance(local, live, count);
    } else {
        roomByWalk(local, live, count);
    }
}
