// The sim kernel's step passes (spliced into sim.wgsl): update, the
// compaction's scans and its offsets. The births' values and
// killBirths are sim/births.wgsl's, scatter and measure
// sim/scatter.wgsl's, room sim/room.wgsl's.

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
    if (unitPoints() > 1u || params.rigid.y > 0u || params.liquid.x != 0u) {
        // A unit's point, a rigid body or a liquid's particle: shifted
        // here, moved by the unit pass (sim/units.wgsl), the rigid pass
        // (sim/rigid.wgsl) or the liquid's passes (liquid.wgsl) after.
        holdUnitPoint(i, start);
        return;
    }
    var a = vec3<f32>(0.0);
    var wind = 0.0;
    forcesAt(start, 0.0, &a, &wind);
    var touch = Touch(false, vec3<f32>(0.0), 0.0);
    if (params.water.x != NONE) {
        touch = waterDrag(start, &a, &wind);
    }
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
    let carried = readVector(0u, i, params.words.velocity);
    let v0 = limitSpeed(carried, age, params.angular.z);
    // The water's lift last: solved with the move it makes.
    waterLift(touch, v0, f.y, f.z, &a);
    var p = moveByModules(start, start + v0 * f.y + a * f.z, age, seconds);
    var v = v0 * f.x + a * f.y;
    let fate = meet(start, &p, &v, birth, inverseLife, death);
    // A crossing of the water surface after the collisions, as the
    // core's MoveParticle meets it.
    let crossing = waterMeet(touch, start, p);
    noteFacts(i, select(0u, EVENT_CONTACT, fate.contacted) | select(0u, EVENT_CROSSED, crossing.crossed),
              fate.first, inverseLife);
    noteCrossing(i, crossing);
    var placed = p;
    if (params.liquid.w != NONE) {
        // A liquid gas's particle (the core's mvfxMoveGridLiquid): moved
        // by the velocity the gas gave it, followed along the colliders.
        placed = start + carried * seconds;
        if (params.collisionHead.z != 0u) {
            var way = placed - start;
            var first = Hit(0.0, vec3<f32>(0.0), vec3<f32>(0.0));
            _ = collideWith(start, &placed, &way, &first, vec4<f32>(0.0));
        }
    }
    for (var k = 0u; k < 3u; k++) {
        source[(params.words.place + k) * c + i] = bitcast<u32>(placed[k]);
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
    // A pour into the water kills it, as the core's mvfxMeetWater does.
    let poured = crossing.crossed && params.waterCross.y != 0u;
    source[params.words.alive * c + i] =
        select(0u, 1u, outlives(fate.death.x, fate.death.y) && !fate.killed && !poured &&
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

// The statistics record's word counting units' tears (MVFX_GPU_TORN).
const STATISTICS_TORN: u32 = 50u;

// The statistics record's words counting rigid bodies' contacts dropped
// past the most and their polytope searches stopped at their cap
// (MVFX_GPU_RIGID_CAPPED, MVFX_GPU_RIGID_SEARCHES).
const STATISTICS_RIGID_CAPPED: u32 = 52u;
const STATISTICS_RIGID_SEARCHES: u32 = 53u;

// Adds the step's rigid contacts dropped past the most and searches
// capped to the counts, in one workgroup: each live body's, which the
// rigid pass left in its alive word of the other buffer (rigidCounts), and
// each birth's, which killBirths left above its mark (bits 8 to 23, and
// 24 on).
fn countRigidCapped(local: u32) {
    let c = params.counts.x;
    var mine = 0u;
    var searches = 0u;
    for (var i = local; i < counts[0]; i += CHUNK) {
        let word = moved[params.words.alive * c + i];
        mine += word & 0xFFFFFFu;
        searches += word >> 24u;
    }
    for (var j = local; j < birthCount(); j += CHUNK) {
        mine += (accepted[j] >> 8u) & 0xFFFFu;
        searches += accepted[j] >> 24u;
    }
    let before = exclusiveScan(local, mine);
    let searchesBefore = exclusiveScan(local, searches);
    if (local == CHUNK - 1u) {
        counts[STATISTICS_RIGID_CAPPED] += before + mine;
        counts[STATISTICS_RIGID_SEARCHES] += searchesBefore + searches;
    }
    workgroupBarrier();
}

// Adds the step's tears to the count, in one workgroup: each live unit's
// tally, which the unit pass left in its first point's alive word of the
// other buffer.
fn countTears(local: u32) {
    let c = params.counts.x;
    let points = unitPoints();
    let units = counts[0] / points;
    var mine = 0u;
    for (var u = local; u < units; u += CHUNK) {
        mine += moved[params.words.alive * c + u * points];
    }
    let before = exclusiveScan(local, mine);
    if (local == CHUNK - 1u) {
        counts[STATISTICS_TORN] += before + mine;
    }
    workgroupBarrier();
}

// Offsets, in one workgroup: the blocks' counts made each block's first
// place, in order; the live count before the step kept in counts[1], the
// new one in counts[0], those past the capacity in counts[2]; for units
// that tear, the step's tears counted first.
@compute @workgroup_size(256, 1, 1)
fn offsets(@builtin(local_invocation_index) local: u32) {
    let c = params.counts.x;
    if (params.unitTears.x != NONE) {
        countTears(local);
    }
    if (params.rigid.y > 0u && params.collisionHead.z != 0u) {
        countRigidCapped(local);
    }
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
