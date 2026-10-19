// The sim kernel's events (spliced into sim.wgsl): what the step's
// particles did, counted, scanned and written in the CPU path's order.

// Events: what the step's particles did, written in the CPU path's order
// (ended deaths, the live particles' collisions, the births with their
// birth and collision events, the killed), at most the emitter's capacity
// of them, the rest dropped. Each element of the step's sequence (live
// particles, then births) counts its events in four groups; a scan of
// each group gives an event's place after the groups before it.
const EVENT_BIRTH: u32 = 0u;
const EVENT_DEATH: u32 = 1u;
const EVENT_COLLISION: u32 = 2u;
// An event's words: kind | killed << 8, serial, tick (two words), place
// (an offset from the plan's anchor after, in the world), velocity,
// normal, colour, size; then the particle's birth tick (two words),
// inverse life and speed in the particles' space, for the values over
// life the records kernel applies.
const EVENT_WORDS: u32 = 28u;

fn writes(kind: u32) -> bool {
    return (params.fateHead.y & (1u << kind)) != 0u;
}

// Where the blocks' counts of the four groups are in the events buffer.
fn eventBlocks() -> u32 {
    return EVENT_FACTS * params.counts.x;
}

// Whether birth j joins the particles: it had room and outlives the plan
// unmet (it may be killed on its way).
fn appended(j: u32) -> bool {
    let mark = accepted[j] & 3u;
    let b0 = birthWord(j, 0u);
    let b1 = birthWord(j, 1u);
    return (mark == 1u || mark == 2u) && (b1.x > 0u || b0.w > params.counts.z);
}

// Element e's events in each group, of live particles alive at the
// step's start: ended deaths; collisions; births and their collisions;
// kills.
fn eventCounts(e: u32, live: u32) -> vec4<u32> {
    var g = vec4<u32>(0u);
    let c = params.counts.x;
    if (e < live) {
        let flags = events[EVENT_FACTS * e];
        if ((flags & EVENT_ENDED) != 0u) {
            g.x = select(0u, 1u, writes(EVENT_DEATH));
            return g;
        }
        g.y = select(0u, 1u, writes(EVENT_COLLISION) && (flags & EVENT_CONTACT) != 0u);
        g.w = select(0u, 1u, writes(EVENT_DEATH) && (source[params.words.alive * c + e] & 1u) == 0u);
        return g;
    }
    let j = e - live;
    if (j >= birthCount() || !appended(j)) {
        return g;
    }
    let mark = accepted[j];
    g.z = select(0u, 1u, writes(EVENT_BIRTH)) +
          select(0u, 1u, writes(EVENT_COLLISION) && (mark & BIRTH_CONTACT) != 0u);
    g.w = select(0u, 1u, writes(EVENT_DEATH) && (mark & 3u) == 2u);
    return g;
}

// Event count: each block's events in each group.
@compute @workgroup_size(256, 1, 1)
fn eventCount(@builtin(local_invocation_index) local: u32,
              @builtin(workgroup_id) group: vec3<u32>) {
    if (params.fateHead.y == 0u) {
        return;
    }
    let live = counts[1];
    let first = group.x * BLOCK + local * ITEMS;
    var mine = vec4<u32>(0u);
    for (var k = 0u; k < ITEMS; k++) {
        mine += eventCounts(first + k, live);
    }
    for (var g = 0u; g < 4u; g++) {
        let before = exclusiveScan(local, mine[g]);
        if (local == CHUNK - 1u) {
            events[eventBlocks() + 4u * group.x + g] = before + mine[g];
        }
    }
}

// Event offsets, in one workgroup: each group's blocks' counts made each
// block's first place in the group, in order; the groups' first places
// (statistics words 30 to 33), the events written (word 28, at most the
// capacity) and those dropped (word 29).
@compute @workgroup_size(256, 1, 1)
fn eventOffsets(@builtin(local_invocation_index) local: u32) {
    if (params.fateHead.y == 0u) {
        return;
    }
    let n = blockCount();
    var base = 0u;
    for (var g = 0u; g < 4u; g++) {
        if (local == 0u) {
            carried = 0u;
        }
        workgroupBarrier();
        for (var start = 0u; start < n; start += CHUNK) {
            let k = start + local;
            var count = 0u;
            if (k < n) {
                count = events[eventBlocks() + 4u * k + g];
            }
            let before = exclusiveScan(local, count);
            if (k < n) {
                events[eventBlocks() + 4u * k + g] = carried + before;
            }
            workgroupBarrier();
            if (local == CHUNK - 1u) {
                carried += before + count;
            }
            workgroupBarrier();
        }
        if (local == 0u) {
            counts[30u + g] = base;
        }
        base += workgroupUniformLoad(&carried);
    }
    if (local == 0u) {
        let written = min(base, params.fateHead.z);
        counts[28] = written;
        counts[29] = base - written;
    }
}

// A place, velocity or normal of the particles' space at a tick of the
// plan in the world, from the plan's anchor after: a world-space
// emitter's as they are, a local one's by the instance's place then.
fn worldPlace(p: vec3<f32>, tick: u32) -> vec3<f32> {
    if (params.start.w == 0u) {
        return p;
    }
    var origin = vec3<f32>(0.0);
    var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    let scale = placeAt(tick, &origin, &rotation);
    return origin + rotate(rotation, p * scale);
}

fn worldVector(v: vec3<f32>, tick: u32) -> vec3<f32> {
    if (params.start.w == 0u) {
        return v;
    }
    var origin = vec3<f32>(0.0);
    var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    let scale = placeAt(tick, &origin, &rotation);
    return rotate(rotation, v * scale);
}

// A plan's tick as the instance's (two words).
fn planTick(tick: u32) -> vec2<u32> {
    let low = params.start.x + tick;
    return vec2<u32>(low, params.start.y + select(0u, 1u, low < params.start.x));
}

// The tick of a contact at a share t of a move from a tick of the plan
// over ticks of it, rounded half up, as the CPU path's.
fn contactTick(start: u32, ticks: u32, t: f32) -> u32 {
    return start + u32(floor(t * f32(ticks) + 0.5));
}

// What an event says, in the particles' space, its ticks the plan's: when
// it happened, and the tick whose place of the instance maps it into the
// world (an ended death's the step's start).
struct Event {
    kind: u32,
    killed: bool,
    serial: u32,
    tick: u32,
    placeTick: u32,
    place: vec3<f32>,
    velocity: vec3<f32>,
    normal: vec3<f32>,
    color: vec4<f32>,
    size: vec3<f32>,
    birth: vec2<u32>,
    inverseLife: f32,
}

// Writes an event at its place, in the world, unless past the capacity.
fn writeEvent(at: u32, e: Event) {
    if (at >= params.fateHead.z) {
        return;
    }
    let w = params.fateHead.w + EVENT_WORDS * at;
    let tick = planTick(e.tick);
    let place = worldPlace(e.place, e.placeTick);
    let velocity = worldVector(e.velocity, e.placeTick);
    var normal = vec3<f32>(0.0);
    if (dot3(e.normal, e.normal) > 0.0) {
        normal = normalize(worldVector(e.normal, e.placeTick));
    }
    events[w] = e.kind | (select(0u, 1u, e.killed) << 8u);
    events[w + 1u] = e.serial;
    events[w + 2u] = tick.x;
    events[w + 3u] = tick.y;
    for (var k = 0u; k < 3u; k++) {
        events[w + 4u + k] = bitcast<u32>(place[k]);
        events[w + 7u + k] = bitcast<u32>(velocity[k]);
        events[w + 10u + k] = bitcast<u32>(normal[k]);
        events[w + 17u + k] = bitcast<u32>(e.size[k]);
    }
    for (var k = 0u; k < 4u; k++) {
        events[w + 13u + k] = bitcast<u32>(e.color[k]);
    }
    let v = e.velocity;
    events[w + 20u] = e.birth.x;
    events[w + 21u] = e.birth.y;
    events[w + 22u] = bitcast<u32>(e.inverseLife);
    events[w + 23u] = bitcast<u32>(sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
    // Its place in the particles' space and the tick it is for, from
    // which an emitter spawning from it maps it with no world between.
    for (var k = 0u; k < 3u; k++) {
        events[w + 24u + k] = bitcast<u32>(e.place[k]);
    }
    events[w + 27u] = e.placeTick;
}

// A live particle's stored vector of n components, ones where the
// emitter stores none.
fn liveStored(first: u32, i: u32, n: u32) -> vec4<f32> {
    var out = vec4<f32>(1.0);
    for (var k = 0u; first != NONE && k < n; k++) {
        out[k] = bitcast<f32>(source[(first + k) * params.counts.x + i]);
    }
    return out;
}

// Live particle i's event of a kind, its place and velocity as given.
fn liveEvent(i: u32, kind: u32, tick: u32, place: vec3<f32>, velocity: vec3<f32>) -> Event {
    let c = params.counts.x;
    let w = params.words;
    return Event(kind, false, source[w.serial * c + i], tick, tick, place, velocity, vec3<f32>(0.0),
                 liveStored(w.color, i, 4u), liveStored(w.size, i, 3u).xyz,
                 vec2<u32>(source[w.birth * c + i], source[(w.birth + 1u) * c + i]),
                 bitcast<f32>(source[w.inverseLife * c + i]));
}

// Live particle i's events of its groups, at their places.
fn writeLiveEvents(i: u32, g: vec4<u32>, at: ptr<function, vec4<u32>>) {
    let c = params.counts.x;
    let w = params.words;
    let place = readVector(0u, i, w.place);
    let velocity = readVector(0u, i, w.velocity);
    if (g.x > 0u) {
        // Ended: where the step found it, at its death tick.
        var e = liveEvent(i, EVENT_DEATH, 0u, place + params.shift.xyz, velocity);
        e.tick = liveDeath(i).x - params.start.x;
        writeEvent((*at).x, e);
        (*at).x += 1u;
        return;
    }
    if (g.y > 0u) {
        let facts = EVENT_FACTS * i;
        let t = bitcast<f32>(events[facts + 1u]);
        var point = vec3<f32>(0.0);
        var normal = vec3<f32>(0.0);
        for (var k = 0u; k < 3u; k++) {
            point[k] = bitcast<f32>(events[facts + 2u + k]);
            normal[k] = bitcast<f32>(events[facts + 5u + k]);
        }
        var e = liveEvent(i, EVENT_COLLISION, contactTick(0u, params.counts.z, t), point, velocity);
        e.normal = normal;
        e.inverseLife = bitcast<f32>(events[facts + 8u]);
        writeEvent((*at).y, e);
        (*at).y += 1u;
    }
    if (g.w > 0u) {
        var e = liveEvent(i, EVENT_DEATH, params.counts.z, place, velocity);
        e.killed = true;
        writeEvent((*at).w, e);
        (*at).w += 1u;
    }
}

// Birth j's events of its groups, at their places.
fn writeBirthEvents(j: u32, g: vec4<u32>, at: ptr<function, vec4<u32>>) {
    let b0 = birthWord(j, 0u);
    let b1 = birthWord(j, 1u);
    var born = vec3<f32>(0.0);
    var bornVelocity = vec3<f32>(0.0);
    var place = vec3<f32>(0.0);
    var velocity = vec3<f32>(0.0);
    let fate = bornFacts(j, &born, &bornVelocity, &place, &velocity);
    var birth = vec2<u32>(0u);
    var death = vec2<u32>(0u);
    birthTicks(b0, b1, &birth, &death);
    var e = Event(EVENT_BIRTH, false, birthSerial(j), b0.z, b0.z, born, bornVelocity, vec3<f32>(0.0),
                  bornColor(j), bornSize(j), birth, bitcast<f32>(b1.y));
    if (writes(EVENT_BIRTH)) {
        writeEvent((*at).z, e);
        (*at).z += 1u;
    }
    if (writes(EVENT_COLLISION) && (accepted[j] & BIRTH_CONTACT) != 0u) {
        e.kind = EVENT_COLLISION;
        e.tick = contactTick(b0.z, params.counts.z - b0.z, fate.first.t);
        e.placeTick = e.tick;
        e.place = fate.first.point;
        e.velocity = velocity;
        e.normal = fate.first.normal;
        writeEvent((*at).z, e);
        (*at).z += 1u;
    }
    if (g.w > 0u) {
        e.kind = EVENT_DEATH;
        e.killed = true;
        e.tick = params.counts.z;
        e.placeTick = params.counts.z;
        e.place = place;
        e.velocity = velocity;
        e.normal = vec3<f32>(0.0);
        e.inverseLife = fate.inverseLife;
        writeEvent((*at).w, e);
        (*at).w += 1u;
    }
}

// Event write: each block's elements' events at their places.
@compute @workgroup_size(256, 1, 1)
fn eventWrite(@builtin(local_invocation_index) local: u32,
              @builtin(workgroup_id) group: vec3<u32>) {
    if (params.fateHead.y == 0u) {
        return;
    }
    let live = counts[1];
    let first = group.x * BLOCK + local * ITEMS;
    var mine: array<vec4<u32>, ITEMS>;
    var sum = vec4<u32>(0u);
    for (var k = 0u; k < ITEMS; k++) {
        mine[k] = eventCounts(first + k, live);
        sum += mine[k];
    }
    var at = vec4<u32>(0u);
    for (var g = 0u; g < 4u; g++) {
        at[g] = counts[30u + g] + events[eventBlocks() + 4u * group.x + g] +
                exclusiveScan(local, sum[g]);
    }
    for (var k = 0u; k < ITEMS; k++) {
        let e = first + k;
        let g = mine[k];
        if (g.x + g.y + g.z + g.w == 0u) {
            continue;
        }
        if (e < live) {
            writeLiveEvents(e, g, &at);
        } else {
            writeBirthEvents(e - live, g, &at);
        }
    }
}
