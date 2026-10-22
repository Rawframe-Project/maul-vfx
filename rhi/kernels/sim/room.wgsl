// The sim kernel's room pass (spliced into sim.wgsl after
// sim/scatter.wgsl): which of the plan's births have room, as the CPU
// path's full step decides.

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
