// The sim kernel's event spawns (spliced into sim.wgsl): an emitter's
// births from its source's events, counted, sorted, scanned and merged.

// Births from another emitter's events (an emitter whose spawnRule is
// set), as the core's mvfxBuildEventBirths and its step number them. The
// source's events and their count are copied into the spawn region of
// this emitter's events buffer (spawnHead.w on) before four kernels:
// spawnCount keeps the source's events of the kind in the ticks that
// spawn, in their order, each with its count drawn at its serial and tick;
// spawnSort orders them by tick, keeping their order at a tick (a radix
// sort in one workgroup over the bytes the latest tick has); spawnScan
// gives each its births' first place in that order, keeps the first
// spawnRule[1].x and counts the rest refused, and moves the serials the
// emitter's event births took on; spawnMerge writes the step's births as
// the core numbers them: the host's, then the plan's own and the kept
// event births by tick (events first at a tick), each with its serial,
// life and death tick, an event birth or host's particle with its
// velocity drawn at its serial, past the births' room none.

// The spawn region's words: the events kept, their births, those kept
// and refused; the serials event births took before the step (two
// words); the step's births and those past the room; the source's events
// written and the first of its step's (after a split frame's steps
// before), the latest tick kept, which set holds the order; the source's
// events from SPAWN_EVENTS, then seven arrays of its event capacity.
const SPAWN_MATCHED: u32 = 0u;
const SPAWN_TOTAL: u32 = 1u;
const SPAWN_KEPT: u32 = 2u;
const SPAWN_REFUSED: u32 = 3u;
const SPAWN_BASE: u32 = 4u;
const SPAWN_BIRTHS: u32 = 6u;
const SPAWN_DROPPED: u32 = 7u;
const SPAWN_SOURCE: u32 = 8u;
const SPAWN_FROM: u32 = 9u;
const SPAWN_LATEST: u32 = 10u;
const SPAWN_SET: u32 = 11u;
const SPAWN_EVENTS: u32 = 16u;
// The arrays: two sets of keys (ticks) and indices into the kept events
// for the sort, the counts, the first places, the source's event each
// kept one is.
const ARRAY_KEYS: u32 = 0u;
const ARRAY_COUNTS: u32 = 4u;
const ARRAY_FIRSTS: u32 = 5u;
const ARRAY_EVENTS: u32 = 6u;
// The serials a statistics record keeps of the event births (two words).
const STATISTICS_EVENT_SERIALS: u32 = 34u;
const STATISTICS_EVENT_REFUSED: u32 = 36u;
const STATISTICS_BIRTHS_DROPPED: u32 = 37u;

var<workgroup> spawnLatest: atomic<u32>;
var<workgroup> tally: array<atomic<u32>, CHUNK>;
var<workgroup> starts: array<u32, CHUNK>;
var<workgroup> bases: array<u32, CHUNK>;
var<workgroup> splitZeros: u32;
var<workgroup> localKeys: array<u32, BLOCK>;
var<workgroup> localIndices: array<u32, BLOCK>;

fn spawning() -> bool {
    return params.spawnRule[0].x != 0u;
}

fn spawnWord(k: u32) -> u32 {
    return params.spawnHead.w + k;
}

// A word of the spawn region, the same in every invocation.
fn spawnUniform(local: u32, k: u32) -> u32 {
    if (local == 0u) {
        liveCount = events[spawnWord(k)];
    }
    return workgroupUniformLoad(&liveCount);
}

// Array a's first word in the spawn region.
fn spawnArray(a: u32) -> u32 {
    let capacity = params.spawnRule[1].w;
    return params.spawnHead.w + SPAWN_EVENTS + EVENT_WORDS * capacity + a * capacity;
}

// The births the step's kernels take, by element: the plan's, or for an
// emitter that spawns from events the merged ones; an emitter of units
// takes each birth's points as its elements (unitPoints).
fn birthCount() -> u32 {
    if (!spawning()) {
        return params.counts.y * unitPoints();
    }
    return events[spawnWord(SPAWN_BIRTHS)] * unitPoints();
}

// The points a birth gives: a unit's, 1 for a particle.
fn unitPoints() -> u32 {
    return params.units.x;
}

// Word k of the birth element j belongs to: birth j / unitPoints() (a
// unit's points share its birth); a particle's own.
fn birthWord(j: u32, k: u32) -> vec4<u32> {
    return births[3u * (j / unitPoints()) + k];
}

// Birth element j's serial's low word: a unit's points take the unit's
// serial times its points plus their place, as the core's.
fn birthSerial(j: u32) -> u32 {
    let points = unitPoints();
    return birthWord(j, 0u).x * points + j % points;
}

// Birth element j's point in its unit, across and down.
fn unitPlace(j: u32) -> vec2<f32> {
    let point = j % unitPoints();
    return vec2<f32>(f32(point % params.units.y), f32(point / params.units.y));
}

// A copied source event's word.
fn sourceEvent(e: u32, k: u32) -> u32 {
    return events[spawnWord(SPAWN_EVENTS) + EVENT_WORDS * e + k];
}

// A copied event's tick from the plan's start, and whether it spawns: of
// the kind, within the spawning ticks.
fn spawnsFrom(e: u32, tick: ptr<function, u32>) -> bool {
    let low = sourceEvent(e, 2u);
    let high = sourceEvent(e, 3u);
    let since = subtract64(vec2<u32>(low, high), params.start.xy);
    *tick = since.x;
    return (sourceEvent(e, 0u) & 0xFFu) == params.spawnRule[0].y && since.y == 0u &&
           since.x >= params.spawnHead.x && since.x <= params.spawnHead.y;
}

// The births an event gives, drawn from its serial and tick.
fn spawnCountOf(e: u32) -> u32 {
    let words = pcg4d(vec4<u32>(params.counts.w, params.start.z,
                                EVENT_SALT ^ sourceEvent(e, 2u), sourceEvent(e, 1u)));
    return params.spawnRule[0].z + productHigh(words.x, params.spawnRule[0].w);
}

// Spawn count, one workgroup: the events that spawn kept in order.
@compute @workgroup_size(256, 1, 1)
fn spawnCount(@builtin(local_invocation_index) local: u32) {
    if (!spawning()) {
        return;
    }
    if (local == 0u) {
        carried = 0u;
        _ = atomicExchange(&spawnLatest, 0u);
    }
    // The source's events of the step alone, as the CPU path's.
    let n = min(spawnUniform(local, SPAWN_SOURCE), params.spawnRule[1].w);
    for (var start = spawnUniform(local, SPAWN_FROM); start < n; start += CHUNK) {
        let e = start + local;
        var tick = 0u;
        let keep = e < n && spawnsFrom(e, &tick);
        let at = carried + exclusiveScan(local, select(0u, 1u, keep));
        if (keep) {
            events[spawnArray(ARRAY_KEYS) + at] = tick;
            events[spawnArray(ARRAY_KEYS + 1u) + at] = at;
            events[spawnArray(ARRAY_COUNTS) + at] = spawnCountOf(e);
            events[spawnArray(ARRAY_EVENTS) + at] = e;
            atomicMax(&spawnLatest, tick);
        }
        workgroupBarrier();
        if (local == CHUNK - 1u) {
            carried = at + select(0u, 1u, keep);
        }
        workgroupBarrier();
    }
    if (local == 0u) {
        events[spawnWord(SPAWN_MATCHED)] = carried;
        events[spawnWord(SPAWN_LATEST)] = atomicLoad(&spawnLatest);
    }
}

// One pass of the sort over a byte: the counts of each digit, their first
// places, then each block of BLOCK in order sorted by the digit within it
// (a stable split on each of its 8 bits) and written at its digit's next
// places.
fn sortPass(local: u32, n: u32, shift: u32, origin: u32, into: u32) {
    _ = atomicExchange(&tally[local], 0u);
    workgroupBarrier();
    for (var e = local; e < n; e += CHUNK) {
        atomicAdd(&tally[(events[origin + e] >> shift) & 0xFFu], 1u);
    }
    workgroupBarrier();
    bases[local] = exclusiveScan(local, atomicLoad(&tally[local]));
    workgroupBarrier();
    let capacity = params.spawnRule[1].w;
    for (var block = 0u; block < n; block += BLOCK) {
        var key: array<u32, ITEMS>;
        var index: array<u32, ITEMS>;
        for (var k = 0u; k < ITEMS; k++) {
            let e = block + local * ITEMS + k;
            key[k] = NONE;
            index[k] = NONE;
            if (e < n) {
                key[k] = events[origin + e];
                index[k] = events[origin + capacity + e];
            }
        }
        _ = atomicExchange(&tally[local], 0u);
        workgroupBarrier();
        for (var k = 0u; k < ITEMS; k++) {
            if (index[k] != NONE) {
                atomicAdd(&tally[(key[k] >> shift) & 0xFFu], 1u);
            }
        }
        workgroupBarrier();
        let count = atomicLoad(&tally[local]);
        starts[local] = exclusiveScan(local, count);
        for (var bit = 0u; bit < 8u; bit++) {
            var mine = 0u;
            for (var k = 0u; k < ITEMS; k++) {
                mine += 1u - ((((key[k] >> shift) & 0xFFu) >> bit) & 1u);
            }
            let before = exclusiveScan(local, mine);
            if (local == CHUNK - 1u) {
                splitZeros = before + mine;
            }
            workgroupBarrier();
            let zeros = splitZeros;
            var z = before;
            for (var k = 0u; k < ITEMS; k++) {
                let at = local * ITEMS + k;
                var place = z;
                if (((((key[k] >> shift) & 0xFFu) >> bit) & 1u) == 1u) {
                    place = zeros + at - z;
                } else {
                    z += 1u;
                }
                localKeys[place] = key[k];
                localIndices[place] = index[k];
            }
            workgroupBarrier();
            for (var k = 0u; k < ITEMS; k++) {
                key[k] = localKeys[local * ITEMS + k];
                index[k] = localIndices[local * ITEMS + k];
            }
            workgroupBarrier();
        }
        for (var k = 0u; k < ITEMS; k++) {
            if (index[k] != NONE) {
                let d = (key[k] >> shift) & 0xFFu;
                let place = bases[d] + local * ITEMS + k - starts[d];
                events[into + place] = key[k];
                events[into + capacity + place] = index[k];
            }
        }
        workgroupBarrier();
        bases[local] += count;
        workgroupBarrier();
    }
}

// Spawn sort, one workgroup: the kept events by tick, stable, a pass for
// each byte the latest tick has; the set holding the order noted.
@compute @workgroup_size(256, 1, 1)
fn spawnSort(@builtin(local_invocation_index) local: u32) {
    if (!spawning()) {
        return;
    }
    let n = spawnUniform(local, SPAWN_MATCHED);
    let latest = spawnUniform(local, SPAWN_LATEST);
    var held = 0u;
    for (var shift = 0u; shift < 32u && (shift == 0u || (latest >> shift) != 0u); shift += 8u) {
        sortPass(local, n, shift, spawnArray(2u * held), spawnArray(2u * (1u - held)));
        held = 1u - held;
    }
    if (local == 0u) {
        events[spawnWord(SPAWN_SET)] = held;
    }
}

// The sorted order's set: its keys' first word (indices follow).
fn sortedSet() -> u32 {
    return spawnArray(2u * events[spawnWord(SPAWN_SET)]);
}

// Spawn scan, one workgroup: each kept event's first birth in the sorted
// order, the births kept and refused, the serials before the step, the
// statistics' serials moved on, the step's births within the room.
@compute @workgroup_size(256, 1, 1)
fn spawnScan(@builtin(local_invocation_index) local: u32) {
    if (!spawning()) {
        return;
    }
    if (local == 0u) {
        carried = 0u;
    }
    let n = spawnUniform(local, SPAWN_MATCHED);
    let sorted = sortedSet();
    let capacity = params.spawnRule[1].w;
    for (var start = 0u; start < n; start += CHUNK) {
        let r = start + local;
        var count = 0u;
        if (r < n) {
            count = events[spawnArray(ARRAY_COUNTS) + events[sorted + capacity + r]];
        }
        let before = exclusiveScan(local, count);
        if (r < n) {
            events[spawnArray(ARRAY_FIRSTS) + r] = carried + before;
        }
        workgroupBarrier();
        if (local == CHUNK - 1u) {
            carried += before + count;
        }
        workgroupBarrier();
    }
    if (local == 0u) {
        let total = carried;
        let kept = min(total, params.spawnRule[1].x);
        let base = vec2<u32>(counts[STATISTICS_EVENT_SERIALS], counts[STATISTICS_EVENT_SERIALS + 1u]);
        let after = add64(base, vec2<u32>(total, 0u));
        let births = params.counts.y + kept;
        let room = params.spawnOwed.w;
        events[spawnWord(SPAWN_TOTAL)] = total;
        events[spawnWord(SPAWN_KEPT)] = kept;
        events[spawnWord(SPAWN_REFUSED)] = total - kept;
        events[spawnWord(SPAWN_BASE)] = base.x;
        events[spawnWord(SPAWN_BASE + 1u)] = base.y;
        events[spawnWord(SPAWN_BIRTHS)] = min(births, room);
        events[spawnWord(SPAWN_DROPPED)] = births - min(births, room);
        counts[STATISTICS_EVENT_SERIALS] = after.x;
        counts[STATISTICS_EVENT_SERIALS + 1u] = after.y;
        counts[STATISTICS_EVENT_REFUSED] = total - kept;
        counts[STATISTICS_BIRTHS_DROPPED] = births - min(births, room);
    }
}

// A plan's birth (three vectors) from the staged plan.
fn plannedBirth(p: u32, k: u32) -> vec4<u32> {
    return births[params.spawnOwed.z + 3u * p + k];
}

// The sorted rank of the kept event whose births hold event birth j.
fn rankOf(j: u32) -> u32 {
    var low = 0u;
    var high = events[spawnWord(SPAWN_MATCHED)];
    let firsts = spawnArray(ARRAY_FIRSTS);
    while (high - low > 1u) {
        let middle = (low + high) / 2u;
        if (events[firsts + middle] <= j) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

// Event birth j's tick from the plan's start.
fn eventBirthTick(j: u32) -> u32 {
    return events[sortedSet() + rankOf(j)];
}

// Event birth j's draw key, as the core's mvfxEventBirthKey: its event's
// particle's serial, and the top bit with 31 bits of pcg4d of the event's
// tick and the birth's index among the event's births.
const KEY_SALT: u32 = 0x4B455953u;

fn eventBirthKey(j: u32) -> vec2<u32> {
    let r = rankOf(j);
    let e = events[spawnArray(ARRAY_EVENTS) + events[sortedSet() + params.spawnRule[1].w + r]];
    let index = j - events[spawnArray(ARRAY_FIRSTS) + r];
    let words = pcg4d(vec4<u32>(sourceEvent(e, 2u), sourceEvent(e, 3u), index, KEY_SALT));
    return vec2<u32>(sourceEvent(e, 1u), 0x80000000u | (words.x >> 1u));
}

// How many of the plan's own births are among the first q of the merged
// births past the host's: own birth i goes before event birth j when its
// tick is earlier, after it when the same or later.
fn ownTaken(q: u32, own: u32, kept: u32) -> u32 {
    let hosts = params.spawnHead.z;
    var low = select(0u, q - kept, q > kept);
    var high = min(q, own);
    while (low < high) {
        let middle = (low + high) / 2u;
        let j = q - middle;
        if (j > 0u && plannedBirth(hosts + middle, 0u).z < eventBirthTick(j - 1u)) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low;
}

// The emitter's source age at a tick of the plan, as the core's
// mvfxSourceAge: its loop's share, or seconds without loops.
fn sourceAgeAt(tick: u32) -> f32 {
    let delay = vec2<u32>(params.spawnRule[2].x, params.spawnRule[2].y);
    let length = vec2<u32>(params.spawnRule[2].z, params.spawnRule[2].w);
    let since = subtract64(add64(params.start.xy, vec2<u32>(tick, 0u)), delay);
    if (length.x == 0u && length.y == 0u) {
        // Before its delay the count is negative: as the core's, in two's
        // complement.
        if ((since.y & 0x80000000u) != 0u) {
            return -secondsOf(subtract64(vec2<u32>(0u), since));
        }
        return secondsOf(since);
    }
    return ratio(remainder64(since, length), length);
}

// A host's particle's velocity at a draw key (its serial, or an event
// birth's key), as the core's
// mvfxHostVelocity: given; or drawn from the shape's normal at its place
// (turned and scaled in the world's space, the instance's velocity
// inherited), with a given one added.
fn hostVelocity(h: u32, key: vec2<u32>, tick: u32) -> vec3<f32> {
    let record = hostBirthAt(h);
    let given = bitcast<u32>(record.place.w);
    if ((given & HOST_GIVES_VELOCITY) != 0u) {
        return record.velocity.xyz;
    }
    var origin = vec3<f32>(0.0);
    var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    let scale = placeAt(tick, &origin, &rotation);
    let world = params.start.w == 0u;
    var local = record.place.xyz;
    if (world) {
        local = rotate(vec4<f32>(-rotation.xyz, rotation.w), (record.place.xyz - origin) / scale);
    }
    var normal = local;
    if (!normalized(&normal)) {
        let shapeWords = draw(SHAPE_SALT, key.x, key.y);
        normal = randomDirection(shapeWords.x, shapeWords.y);
    }
    var v = drawVelocity(draw(VELOCITY_SALT, key.x, key.y), local, normal);
    if (world) {
        v = rotate(rotation, v * scale) + params.shift.w * params.instanceVelocity.xyz;
    }
    if ((given & HOST_ADDS_VELOCITY) != 0u) {
        v += record.velocity.xyz;
    }
    return v;
}

const HOST_GIVES_VELOCITY: u32 = 4u;
const HOST_ADDS_VELOCITY: u32 = 8u;
const SHORTEST_LIFE: f32 = 9.5367431640625e-7;
const LONGEST_LIFE: f32 = 1.0e6;

// The instance's move from the plan's start to a tick of it.
fn movedBy(tick: u32) -> vec3<f32> {
    let t = select(1.0, f32(tick) / f32(params.counts.z), params.counts.z > 0u);
    return params.spawnMove.xyz * t;
}

// An event's place in this emitter's terms at its tick, from its place in
// the source's particles' space for a tick (held, at): in the world's
// space as an offset from this emitter's anchor, in the instance's as the
// instance is then. Neither goes through a place in the world, whose
// binary32 words far from its origin hold too little: an instance's
// places are differences of its origins (the move over the plan), and a
// world-space source's places offsets from its anchor.
fn eventPlace(held: vec3<f32>, at: u32, tick: u32) -> vec3<f32> {
    let sourceLocal = (params.spawnRule[1].y & 4u) != 0u;
    var origin = vec3<f32>(0.0);
    var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    if (params.start.w == 0u) {
        if (!sourceLocal) {
            return held + params.spawnShift.xyz;
        }
        let scale = placeAt(at, &origin, &rotation);
        return origin + rotate(rotation, held * scale);
    }
    // From the instance's origin at the event's tick, in the world.
    var relative = vec3<f32>(0.0);
    if (sourceLocal) {
        let scale = placeAt(at, &origin, &rotation);
        relative = (movedBy(at) - movedBy(tick)) + rotate(rotation, held * scale);
    } else {
        relative = held + params.spawnFrom.xyz - movedBy(tick);
    }
    let scale = placeAt(tick, &origin, &rotation);
    return rotate(vec4<f32>(-rotation.xyz, rotation.w), relative * (1.0 / scale));
}

// Writes event birth j's host record (after the plan's host births): its
// place and inherited velocity in the emitter's terms at its event's tick,
// the colour and size it inherits.
fn writeEventHost(j: u32, tick: u32) {
    let e = events[spawnArray(ARRAY_EVENTS) + events[sortedSet() + params.spawnRule[1].w + rankOf(j)]];
    let share = bitcast<f32>(params.spawnRule[1].z);
    var held = vec3<f32>(0.0);
    var velocity = vec3<f32>(0.0);
    var color = vec4<f32>(0.0);
    var size = vec3<f32>(0.0);
    for (var k = 0u; k < 3u; k++) {
        held[k] = bitcast<f32>(sourceEvent(e, 24u + k));
        velocity[k] = bitcast<f32>(sourceEvent(e, 7u + k)) * share;
        size[k] = bitcast<f32>(sourceEvent(e, 17u + k));
    }
    for (var k = 0u; k < 4u; k++) {
        color[k] = bitcast<f32>(sourceEvent(e, 13u + k));
    }
    // Held to what the emitter's particles may store, as the core's
    // BirthOf holds them.
    let sizeCaps = bitcast<vec4<f32>>(params.spawnRule[3]).xyz;
    let colorCaps = bitcast<vec4<f32>>(params.spawnRule[4]);
    size = clamp(size, -sizeCaps, sizeCaps);
    color = clamp(color, -colorCaps, colorCaps);
    let place = eventPlace(held, sourceEvent(e, 27u), tick);
    if (params.start.w != 0u) {
        var origin = vec3<f32>(0.0);
        var rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
        let scale = placeAt(tick, &origin, &rotation);
        velocity = rotate(vec4<f32>(-rotation.xyz, rotation.w), velocity * (1.0 / scale));
    }
    let flags = params.spawnRule[1].y;
    let given = HOST_ADDS_VELOCITY | select(0u, HOST_GIVES_COLOR, (flags & 1u) != 0u) |
                select(0u, HOST_GIVES_SIZE, (flags & 2u) != 0u);
    let at = 6u * params.counts.x + 4u * (params.spawnHead.z + j);
    births[at] = bitcast<vec4<u32>>(vec4<f32>(place, bitcast<f32>(given)));
    births[at + 1u] = bitcast<vec4<u32>>(vec4<f32>(velocity, 0.0));
    births[at + 2u] = bitcast<vec4<u32>>(color);
    // Its event's particle's serial, which a ribbon by source takes.
    births[at + 3u] = vec4<u32>(bitcast<vec3<u32>>(size), sourceEvent(e, 1u));
}

// Spawn merge: merged birth m written as the core numbers it.
@compute @workgroup_size(64, 1, 1)
fn spawnMerge(@builtin(global_invocation_id) id: vec3<u32>) {
    if (!spawning()) {
        return;
    }
    let n = events[spawnWord(SPAWN_BIRTHS)];
    let hosts = params.spawnHead.z;
    let own = params.counts.y - hosts;
    let kept = events[spawnWord(SPAWN_KEPT)];
    let skipped = add64(vec2<u32>(events[spawnWord(SPAWN_BASE)], events[spawnWord(SPAWN_BASE + 1u)]),
                        vec2<u32>(events[spawnWord(SPAWN_REFUSED)], 0u));
    for (var m = id.x; m < n; m += params.modes.z * 64u) {
        var b0 = vec4<u32>(0u);
        var b1 = vec4<u32>(0u);
        var b2 = vec4<u32>(0u);
        var p = m;
        var before = 0u;
        var isEvent = false;
        var j = 0u;
        if (m >= hosts) {
            let q = m - hosts;
            let i = ownTaken(q, own, kept);
            j = q - i;
            p = hosts + i;
            isEvent = i == own || (j < kept && eventBirthTick(j) <= plannedBirth(p, 0u).z);
            before = j;
        }
        var serial = vec2<u32>(0u);
        var key = vec2<u32>(0u);
        var tick = 0u;
        if (isEvent) {
            let next = select(vec2<u32>(params.spawnOwed.x, params.spawnOwed.y),
                              plannedBirth(p, 0u).xy, p < params.counts.y);
            serial = add64(add64(next, skipped), vec2<u32>(j, 0u));
            tick = eventBirthTick(j);
            key = eventBirthKey(j);
            b1 = vec4<u32>(0u, 0u, 0u, 1u);
            // Its key in the record's last words, which the step draws by.
            b2 = vec4<u32>(bitcast<u32>(sourceAgeAt(tick)), hosts + j, key.x, key.y);
            writeEventHost(j, tick);
        } else {
            let planned0 = plannedBirth(p, 0u);
            let planned1 = plannedBirth(p, 1u);
            b2 = plannedBirth(p, 2u);
            serial = add64(add64(planned0.xy, skipped), vec2<u32>(before, 0u));
            key = serial;
            tick = planned0.z;
            b1 = vec4<u32>(0u, 0u, planned1.z, planned1.w);
        }
        let age = bitcast<f32>(b2.x);
        let drawn = drawVector(SOURCE_LIFE, age, draw(LIFE_SALT, key.x, key.y)).x;
        let life = min(max(drawn, SHORTEST_LIFE), LONGEST_LIFE);
        let inverse = divide(1.0, life);
        let death = add64(vec2<u32>(tick, 0u), lifeTicks(inverse));
        b0 = vec4<u32>(serial.x, serial.y, tick, death.x);
        b1.x = death.y;
        b1.y = bitcast<u32>(inverse);
        if (b2.y != NONE) {
            // A host's particle or an event birth: its velocity drawn at
            // its key.
            let at = 6u * params.counts.x + 4u * b2.y + 1u;
            births[at] = bitcast<vec4<u32>>(vec4<f32>(hostVelocity(b2.y, key, tick), 0.0));
        }
        births[3u * m] = b0;
        births[3u * m + 1u] = b1;
        births[3u * m + 2u] = b2;
    }
}
