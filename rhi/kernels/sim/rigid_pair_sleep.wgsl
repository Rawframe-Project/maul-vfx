// The sim kernel's rigid pairs' sleeping (spliced into sim.wgsl beside
// sim/rigid_pairs.wgsl), as the core's mvfxWakeTouching and
// mvfxSettleBodies: sleepers an awake body reaches woken, in rounds until
// none wakes; and at the step's end the islands of the last substep's
// pairs, each asleep or awake whole.

var<workgroup> pairWoken: atomic<u32>;

// Wakes the sleepers an awake body reaches, as the core's
// mvfxWakeTouching: rounds until one wakes none (at most a round a
// body), a body woken in one waking those it reaches in the next (the
// same sleepers in any order); whether one woke. A woken body's sleep is
// 0 and its flags say so.
fn pairWake(t: u32, live: u32) -> bool {
    if (bodyContacts() == 0u) {
        return false;
    }
    if (t == 0u) {
        _ = atomicExchange(&pairWoken, 0u);
    }
    workgroupBarrier();
    for (var i = t; i < live; i += PAIR_GROUP) {
        if (!isEnded(i) && (blocks[infoAt(i) + 1u] & FLAG_ASLEEP) != 0u) {
            atomicAdd(&pairWoken, 1u);
        }
    }
    workgroupBarrier();
    let asleep = pairUniform(t, atomicLoad(&pairWoken));
    if (asleep == 0u) {
        return false;
    }
    let grid = pairLevels(t, live);
    if (asleep == grid.count) {
        return false;
    }
    // A round that goes on wakes a sleeper, so live rounds wake them all.
    var woke = false;
    for (var round = 0u; round < live; round++) {
        if (t == 0u) {
            _ = atomicExchange(&pairWoken, 0u);
        }
        workgroupBarrier();
        for (var a = t; a < live; a += PAIR_GROUP) {
            if (isEnded(a)) {
                continue;
            }
            var past = 0u;
            loop {
                let b = nextCandidate(a, past, grid, true);
                if (b == NONE) {
                    break;
                }
                past = b + 1u;
                let sleeper = select(b, a, (blocks[infoAt(a) + 1u] & FLAG_ASLEEP) != 0u);
                let at = infoAt(sleeper) + 1u;
                if ((blocks[at] & FLAG_ASLEEP) != 0u) {
                    blocks[at] = (blocks[at] & ~FLAG_ASLEEP) | FLAG_WOKEN;
                    source[params.rigid.z * params.counts.x + sleeper] = bitcast<u32>(0.0);
                    atomicAdd(&pairWoken, 1u);
                }
            }
        }
        pairWait();
        if (pairUniform(t, atomicLoad(&pairWoken)) == 0u) {
            break;
        }
        woke = true;
    }
    return woke;
}

// Settles the step's bodies, as the core's mvfxSettleBodies: each awake
// body's still time grown; the islands of the last substep's pairs (each
// body's label lowered to its runs' partners' in rounds until none
// changes, at most a round a body: the least index of its island, the
// core's union-find root); an island with a body awake and not still the
// sleep time awake whole, its sleepers woken; the rest asleep, their
// velocities none.
fn pairSettle(t: u32, live: u32, found: vec3<u32>, seconds: f32) {
    for (var i = t; i < live; i += PAIR_GROUP) {
        if (isEnded(i)) {
            continue;
        }
        var b = loadBody(i);
        if (!isAsleep(b)) {
            stillBody(&b, seconds);
            storeBody(i, b);
        }
        blocks[infoAt(i) + 10u] = i;
    }
    pairWait();
    // A label falls to its island's least index in fewer rounds than
    // there are bodies, and a last round changes none.
    for (var round = 0u; round <= live; round++) {
        if (t == 0u) {
            _ = atomicExchange(&pairWoken, 0u);
        }
        workgroupBarrier();
        for (var i = t; i < live; i += PAIR_GROUP) {
            if (isEnded(i)) {
                continue;
            }
            var least = blocks[infoAt(i) + 10u];
            let range = bodyRunRange(i, found.x);
            for (var e = range.x; e < range.y && found.x > 0u; e++) {
                let head = poolAt(blocks[runStartAt(blocks[bodyRunsAt() + 2u * e + 1u])]);
                let other = select(blocks[head], blocks[head + 1u], blocks[head] == i);
                least = min(least, blocks[infoAt(other) + 10u]);
            }
            blocks[infoAt(i) + 11u] = least;
            if (least != blocks[infoAt(i) + 10u]) {
                atomicAdd(&pairWoken, 1u);
            }
        }
        pairWait();
        for (var i = t; i < live; i += PAIR_GROUP) {
            blocks[infoAt(i) + 10u] = blocks[infoAt(i) + 11u];
            blocks[infoAt(i) + 11u] = 0u;
        }
        pairWait();
        if (pairUniform(t, atomicLoad(&pairWoken)) == 0u) {
            break;
        }
    }
    for (var i = t; i < live; i += PAIR_GROUP) {
        let sleep = bitcast<f32>(source[params.rigid.z * params.counts.x + i]);
        if (!isEnded(i) && sleep >= 0.0 && sleep < params.rigidMass.z) {
            blocks[infoAt(blocks[infoAt(i) + 10u]) + 11u] = 1u;
        }
    }
    pairWait();
    for (var i = t; i < live; i += PAIR_GROUP) {
        if (isEnded(i)) {
            continue;
        }
        var b = loadBody(i);
        let awake = blocks[infoAt(blocks[infoAt(i) + 10u]) + 11u] != 0u;
        if (awake && isAsleep(b)) {
            b.sleep = 0.0;
        } else if (!awake && !isAsleep(b)) {
            b.sleep = SLEEP_NEW;
            b.moving.velocity = vec3<f32>(0.0);
            b.moving.turning.angular = vec3<f32>(0.0);
        }
        storeBody(i, b);
    }
    pairWait();
}
