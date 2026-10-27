// The sim kernel's rigid particles that meet each other (spliced into
// sim.wgsl), as the core's mvfxStepRigid with pairs: one workgroup an
// emitter, its invocations striding over the bodies, each substep taken
// by every body before the next, so that bodies can meet each other
// within it. A body's fate over the step (its first strike, its drops and
// stopped searches) is kept in the blocks' buffer past the words the
// blocks hold, as are the pairs' arrays.

// include: shared/cell_floor.wgsl

const PAIR_GROUP: u32 = 256u;
// A body's fate's words: its strikes, drops and stopped searches, whether
// it struck, the strike's tick, velocity, time, point and normal, and its
// life's inverse at the step's start.
const FATE_WORDS: u32 = 16u;
// A touched body's solve (Solid) and its contacts with the world: their
// count, then each contact's words.
const SOLID_WORDS: u32 = 28u;
const WORLD_CONTACT_WORDS: u32 = 18u;
const WORLD_WORDS: u32 = 1u + MOST_CONTACTS * WORLD_CONTACT_WORDS;

// The first word of the rigid pairs' arrays in the blocks' buffer: past
// the blocks' 8 words each (mvfxScanBlocks).
fn pairBase() -> u32 {
    return 8u * ((3u * params.counts.x + 1023u) / 1024u);
}

fn storeFate(i: u32, fate: Fate) {
    let at = pairBase() + FATE_WORDS * i;
    blocks[at] = fate.contacts;
    blocks[at + 1u] = fate.capped;
    blocks[at + 2u] = fate.searches;
    blocks[at + 3u] = select(0u, 1u, fate.contacted);
    blocks[at + 4u] = fate.tick;
    blocks[at + 5u] = bitcast<u32>(fate.velocity.x);
    blocks[at + 6u] = bitcast<u32>(fate.velocity.y);
    blocks[at + 7u] = bitcast<u32>(fate.velocity.z);
    blocks[at + 8u] = bitcast<u32>(fate.first.t);
    blocks[at + 9u] = bitcast<u32>(fate.first.point.x);
    blocks[at + 10u] = bitcast<u32>(fate.first.point.y);
    blocks[at + 11u] = bitcast<u32>(fate.first.point.z);
    blocks[at + 12u] = bitcast<u32>(fate.first.normal.x);
    blocks[at + 13u] = bitcast<u32>(fate.first.normal.y);
    blocks[at + 14u] = bitcast<u32>(fate.first.normal.z);
}

fn loadFate(i: u32) -> Fate {
    let at = pairBase() + FATE_WORDS * i;
    var fate = Fate();
    fate.contacts = blocks[at];
    fate.capped = blocks[at + 1u];
    fate.searches = blocks[at + 2u];
    fate.contacted = blocks[at + 3u] != 0u;
    fate.tick = blocks[at + 4u];
    fate.velocity = vec3<f32>(bitcast<f32>(blocks[at + 5u]), bitcast<f32>(blocks[at + 6u]),
                              bitcast<f32>(blocks[at + 7u]));
    fate.first.t = bitcast<f32>(blocks[at + 8u]);
    fate.first.point = vec3<f32>(bitcast<f32>(blocks[at + 9u]), bitcast<f32>(blocks[at + 10u]),
                                 bitcast<f32>(blocks[at + 11u]));
    fate.first.normal = vec3<f32>(bitcast<f32>(blocks[at + 12u]),
                                  bitcast<f32>(blocks[at + 13u]),
                                  bitcast<f32>(blocks[at + 14u]));
    return fate;
}

// Where body i's solve and its world contacts are.
fn solidAt(i: u32) -> u32 {
    return pairBase() + FATE_WORDS * params.counts.x + SOLID_WORDS * i;
}

fn worldAt(i: u32) -> u32 {
    return pairBase() + (FATE_WORDS + SOLID_WORDS) * params.counts.x + WORLD_WORDS * i;
}

fn storeVector(at: u32, v: vec3<f32>) {
    blocks[at] = bitcast<u32>(v.x);
    blocks[at + 1u] = bitcast<u32>(v.y);
    blocks[at + 2u] = bitcast<u32>(v.z);
}

fn loadVector(at: u32) -> vec3<f32> {
    return vec3<f32>(bitcast<f32>(blocks[at]), bitcast<f32>(blocks[at + 1u]),
                     bitcast<f32>(blocks[at + 2u]));
}

fn storeRotation(at: u32, q: vec4<f32>) {
    storeVector(at, q.xyz);
    blocks[at + 3u] = bitcast<u32>(q.w);
}

fn loadRotation(at: u32) -> vec4<f32> {
    return vec4<f32>(loadVector(at), bitcast<f32>(blocks[at + 3u]));
}

// Body i's pose, velocities and start pose of a solve, its mass as it
// was.
fn storeMoved(i: u32, b: Solid) {
    let at = solidAt(i);
    storeVector(at, b.x);
    storeVector(at + 3u, b.v);
    storeVector(at + 6u, b.w);
    storeVector(at + 9u, b.x0);
    storeRotation(at + 12u, b.q);
    storeRotation(at + 16u, b.q0);
}

// Body i's solve, its units' exponents in its last word, the mass's in
// the low 16 bits and the length's in the high 16.
fn storeSolid(i: u32, b: Solid) {
    let at = solidAt(i);
    storeMoved(i, b);
    blocks[at + 20u] = bitcast<u32>(b.inverseMass);
    for (var k = 0u; k < 6u; k++) {
        blocks[at + 21u + k] = bitcast<u32>(b.inverse[k]);
    }
    blocks[at + 27u] = (bitcast<u32>(b.massUnit) & 0xFFFFu) | (bitcast<u32>(b.lengthUnit) << 16u);
}

fn loadSolid(i: u32) -> Solid {
    let at = solidAt(i);
    var b = Solid();
    b.x = loadVector(at);
    b.v = loadVector(at + 3u);
    b.w = loadVector(at + 6u);
    b.x0 = loadVector(at + 9u);
    b.q = loadRotation(at + 12u);
    b.q0 = loadRotation(at + 16u);
    b.inverseMass = bitcast<f32>(blocks[at + 20u]);
    for (var k = 0u; k < 6u; k++) {
        b.inverse[k] = bitcast<f32>(blocks[at + 21u + k]);
    }
    let units = bitcast<i32>(blocks[at + 27u]);
    b.massUnit = (units << 16u) >> 16u;
    b.lengthUnit = units >> 16u;
    return b;
}

// Body i's contacts with the world (their count 0: untouched by it).
fn worldCount(i: u32) -> u32 {
    return blocks[worldAt(i)];
}

fn storeWorld(i: u32, list: ptr<function, Contacts>) {
    let base = worldAt(i);
    blocks[base] = (*list).count;
    for (var j = 0u; j < (*list).count; j++) {
        let c = (*list).items[j];
        let at = base + 1u + WORLD_CONTACT_WORDS * j;
        storeVector(at, c.body);
        storeVector(at + 3u, c.surface);
        storeVector(at + 6u, c.normal);
        blocks[at + 9u] = bitcast<u32>(c.depth);
        blocks[at + 10u] = bitcast<u32>(c.lambda);
        blocks[at + 11u] = bitcast<u32>(c.speed);
        blocks[at + 12u] = bitcast<u32>(c.applied);
        blocks[at + 13u] = bitcast<u32>(c.rubbed);
        blocks[at + 14u] = bitcast<u32>(c.slowed);
        blocks[at + 15u] = bitcast<u32>(c.twisted);
        blocks[at + 16u] = bitcast<u32>(c.spun);
        blocks[at + 17u] = c.collider;
    }
}

fn loadWorld(i: u32) -> Contacts {
    let base = worldAt(i);
    var list = Contacts();
    list.count = blocks[base];
    for (var j = 0u; j < list.count; j++) {
        let at = base + 1u + WORLD_CONTACT_WORDS * j;
        var c = Contact();
        c.body = loadVector(at);
        c.surface = loadVector(at + 3u);
        c.normal = loadVector(at + 6u);
        c.depth = bitcast<f32>(blocks[at + 9u]);
        c.lambda = bitcast<f32>(blocks[at + 10u]);
        c.speed = bitcast<f32>(blocks[at + 11u]);
        c.applied = bitcast<f32>(blocks[at + 12u]);
        c.rubbed = bitcast<f32>(blocks[at + 13u]);
        c.slowed = bitcast<f32>(blocks[at + 14u]);
        c.twisted = bitcast<f32>(blocks[at + 15u]);
        c.spun = bitcast<f32>(blocks[at + 16u]);
        c.collider = blocks[at + 17u];
        list.items[j] = c;
    }
    return list;
}

// Waits for the workgroup's writes to the buffers before going on.
fn pairWait() {
    storageBarrier();
    workgroupBarrier();
}

// A substep's start for body i, as rigidSubstep to its meeting: its start
// pose kept, an awake body spun and moved, a sleeping one held, then
// prepared (pairPrepare); again, a body woken by touch prepared anew at
// the pose it was held at, as the core prepares the woken.
fn pairStart(i: u32, since: u32, ticks: u32, again: bool) {
    var b = loadBody(i);
    if (again && (blocks[infoAt(i) + 1u] & FLAG_WOKEN) == 0u) {
        return;
    }
    var fate = loadFate(i);
    let h = secondsOf(vec2<u32>(ticks, 0u));
    let x0 = b.moving.place;
    let q0 = b.moving.turning.orientation;
    let asleep = isAsleep(b);
    if (!asleep && !again) {
        spinBody(&b, rigidMass(rigidSizeOf(b)), h);
    }
    if (!again) {
        var moving = b.moving;
        _ = moveState(&moving, since, ticks);
        b.moving = moving;
    }
    if (asleep) {
        holdSleeping(&b, x0, q0, h);
    }
    pairPrepare(i, b, x0, q0, &fate);
    storeFate(i, fate);
    storeBody(i, b);
}

// Body i's preparing at its pose from the start pose, as the core's
// Prepare: an awake one meeting the world gathers its contacts; its solve
// stored (a sleeping body immovable), with its reach and whether it
// sleeps.
fn pairPrepare(i: u32, b: Body, x0: vec3<f32>, q0: vec4<f32>, fate: ptr<function, Fate>) {
    let asleep = isAsleep(b);
    let size = rigidSizeOf(b);
    let m = b.moving;
    var list = Contacts();
    if (!asleep && params.collisionHead.z != 0u) {
        list = gatherContacts(m.place, m.turning.orientation, x0, q0, shapeOf(size),
                              rigidReach(size));
        (*fate).capped += list.capped;
        (*fate).searches += list.searches;
    }
    var mass = rigidMass(size);
    if (asleep) {
        mass.inverseMass = 0.0;
        mass.inverse = array<f32, 6>();
    }
    let body = Solid(m.place, m.velocity, m.turning.angular, x0, m.turning.orientation, q0,
                     mass.inverseMass, mass.inverse, mass.massUnit, mass.lengthUnit);
    speedsOf(body, &list);
    storeSolid(i, body);
    storeWorld(i, &list);
    let at = infoAt(i);
    blocks[at] = bitcast<u32>(rigidReach(size));
    blocks[at + 1u] = select(0u, FLAG_ASLEEP, asleep);
    blocks[at + 9u] = 0u;
}

// Whether body i met the world or another body this substep.
fn isTouched(i: u32) -> bool {
    return worldCount(i) > 0u || blocks[infoAt(i) + 9u] != 0u;
}

// A touched body's prediction, symplectic Euler's (its start place plus
// its velocity times h), as the core's Meet.
fn pairPredict(i: u32, h: f32) {
    if (!isTouched(i)) {
        return;
    }
    var body = loadSolid(i);
    body.x = body.x0 + body.v * h;
    storeSolid(i, body);
}

// A pass of a stage over body i's contacts with the world: stabilizing
// (0), pressing (1, its static friction after the first round), bouncing
// (2); a touched body's correction between pressing and bouncing (3).
fn pairWorldPass(i: u32, stage: u32, round: u32, h: f32, still: f32) {
    if (stage == 3u) {
        if (isTouched(i)) {
            var body = loadSolid(i);
            correctBody(&body, h);
            storeSolid(i, body);
        }
        return;
    }
    if (worldCount(i) == 0u) {
        return;
    }
    var body = loadSolid(i);
    var list = loadWorld(i);
    let backward = round % 2u == 1u;
    switch stage {
        case 0u: {
            stabilize(&body, &list, backward);
        }
        case 1u: {
            solvePositions(&body, &list, h, round > 0u, backward);
        }
        default: {
            solveVelocities(&body, &list, h, still, backward);
        }
    }
    storeSolid(i, body);
    storeWorld(i, &list);
}

// Body i's first pair contact struck faster than still, in the pool's
// order, as the core's mvfxStrikePairs: false for none, else its point (on
// the contact's second body) and normal toward i.
fn pairStrike(i: u32, found: vec3<u32>, still: f32, c: ptr<function, Contact>) -> bool {
    let range = bodyRunRange(i, found.x);
    for (var e = range.x; e < range.y; e++) {
        let r = blocks[bodyRunsAt() + 2u * e + 1u];
        for (var k = blocks[runStartAt(r)]; k < runEnd(r, found.x, found.z); k++) {
            let p = loadPair(poolAt(k));
            if (p.lambda > 0.0 && p.speed < -still) {
                let b = loadSolid(p.b);
                (*c).surface = b.x + rotate(b.q, p.onB);
                (*c).normal = select(-p.normal, p.normal, i == p.a);
                return true;
            }
        }
    }
    return false;
}

// A substep's end for body i, touched: its pose and velocities from its
// solve, then its first contact with the world struck faster than still,
// else its first pair contact, as meetWorld and the core's strikes.
fn pairEnd(i: u32, tick: u32, still: f32, found: vec3<u32>) {
    if (!isTouched(i)) {
        return;
    }
    var b = loadBody(i);
    let body = loadSolid(i);
    let list = loadWorld(i);
    b.moving.place = body.x;
    b.moving.velocity = body.v;
    b.moving.turning.angular = body.w;
    b.moving.turning.orientation = body.q;
    var fate = loadFate(i);
    var struck = false;
    for (var j = 0u; j < list.count; j++) {
        let c = list.items[j];
        if (c.lambda > 0.0 && c.speed < -still) {
            joinFates(&fate, strikeBody(&b, c, tick));
            struck = true;
            break;
        }
    }
    var c = Contact();
    if (!struck && found.x > 0u && pairStrike(i, found, still, &c)) {
        joinFates(&fate, strikeBody(&b, c, tick));
    }
    storeFate(i, fate);
    storeBody(i, b);
}

// Whether body i takes part in the step: live and not ended.
fn pairLive(i: u32) -> bool {
    return !isEnded(i);
}

// One substep of every body, as the core's Substep with pairs: each
// body's start; when it collides, the pairs found and coloured, touched
// bodies' predictions, the stages' rounds (the world's contacts then the
// pairs' colours forward, the reverse backward, a wait after each), and
// each body's end.
fn pairSubstep(t: u32, live: u32, span: vec2<u32>) -> vec3<u32> {
    let h = secondsOf(vec2<u32>(span.y, 0u));
    let still = 2.0 * params.gravity.w * h;
    // The start, then (sleeping on) waking by touch and the woken
    // prepared anew, in one loop so the gathering is inlined once.
    let starts = select(1u, 2u, params.rigid.z != NONE && params.collisionHead.z != 0u);
    for (var round = 0u; round < starts; round++) {
        if (round == 1u) {
            if (!pairWake(t, live)) {
                break;
            }
        }
        for (var i = t; i < live; i += PAIR_GROUP) {
            if (pairLive(i)) {
                pairStart(i, span.x, span.y, round == 1u);
            }
        }
        pairWait();
    }
    var found = vec3<u32>(0u);
    if (params.collisionHead.z != 0u) {
        found.z = pairFind(t, live);
        if (found.z > 0u) {
            found = vec3<u32>(pairColour(t, found.z), found.z);
        }
    }
    for (var i = t; i < live; i += PAIR_GROUP) {
        if (pairLive(i)) {
            pairPredict(i, h);
        }
    }
    pairWait();
    var rounds = array<u32, 4>(POSITION_PASSES, POSITION_PASSES, 1u, RESTITUTION_PASSES);
    var stages = array<u32, 4>(0u, 1u, 3u, 2u);
    for (var k = 0u; k < 4u; k++) {
        for (var round = 0u; round < rounds[k]; round++) {
            let backward = round % 2u == 1u;
            if (backward && stages[k] != 3u) {
                pairPass(t, stages[k], round, found, h, still);
            }
            for (var i = t; i < live; i += PAIR_GROUP) {
                if (pairLive(i)) {
                    pairWorldPass(i, stages[k], round, h, still);
                }
            }
            pairWait();
            if (!backward && stages[k] != 3u) {
                pairPass(t, stages[k], round, found, h, still);
            }
        }
    }
    for (var i = t; i < live; i += PAIR_GROUP) {
        if (pairLive(i)) {
            pairEnd(i, span.x + span.y, still, found);
        }
    }
    pairWait();
    return found;
}

// Rigid pairs: the rigid pass for an emitter whose bodies meet each other,
// in one workgroup. Each live body's fate cleared and its life's inverse
// kept; each substep taken by every body; with sleeping, the islands of
// the last substep's pairs settled; then each body's first strike's event
// and its drops, as the rigid pass.
@compute @workgroup_size(256, 1, 1)
fn rigidPairs(@builtin(local_invocation_index) t: u32) {
    let c = params.counts.x;
    let live = counts[0];
    for (var i = t; i < live; i += PAIR_GROUP) {
        moved[params.words.alive * c + i] = 0u;
        storeFate(i, Fate());
        blocks[pairBase() + FATE_WORDS * i + 15u] = bitcast<u32>(loadMoving(0u, i).inverseLife);
    }
    pairWait();
    var last = vec3<u32>(0u);
    for (var s = 0u; s < params.rigid.y; s++) {
        let span = rigidSpan(s);
        if (span.y > 0u) {
            last = pairSubstep(t, live, span);
        }
    }
    if (params.rigid.z != NONE) {
        pairSettle(t, live, last, secondsOf(vec2<u32>(params.counts.z, 0u)));
    }
    for (var i = t; i < live; i += PAIR_GROUP) {
        if (source[params.words.alive * c + i] == LIVE_ENDED) {
            continue;
        }
        let b = loadBody(i);
        let fate = loadFate(i);
        if (fate.contacted) {
            let inverseLife = bitcast<f32>(blocks[pairBase() + FATE_WORDS * i + 15u]);
            noteUnitContact(i, fate.tick, fate.first, fate.velocity, inverseLife);
        }
        moved[params.words.alive * c + i] = rigidCounts(fate);
        storeBody(i, b);
    }
}
