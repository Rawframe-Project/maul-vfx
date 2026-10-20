// The sim kernel's rigid pairs' colours (spliced into sim.wgsl beside
// sim/rigid_pairs.wgsl), as the core's mvfxColourBodyPairs and
// mvfxPairPass: the pool's runs, their colours found in rounds, and the
// passes colour by colour.

// The salt of a run's priority (the letters "pair"), as the core's.
const PAIR_SALT: u32 = 0x70616972u;

var<workgroup> pairLeft: atomic<u32>;
var<workgroup> pairColours: atomic<u32>;

// The runs' arrays after the pool's room: each run's first contact, its
// priority, its colour, its next colour (while colouring), then the
// entries (body, run) sorted, then (colour, run) sorted.
fn runsAt() -> u32 {
    return poolAt(bodyContacts() * params.counts.x);
}

fn runStartAt(r: u32) -> u32 {
    return runsAt() + r;
}

fn runPriorityAt(r: u32) -> u32 {
    return runsAt() + bodyContacts() * params.counts.x + r;
}

fn runColourAt(r: u32) -> u32 {
    return runsAt() + 2u * bodyContacts() * params.counts.x + r;
}

fn runNextAt(r: u32) -> u32 {
    return runsAt() + 3u * bodyContacts() * params.counts.x + r;
}

fn bodyRunsAt() -> u32 {
    return runsAt() + 4u * bodyContacts() * params.counts.x;
}

fn colourRunsAt() -> u32 {
    return bodyRunsAt() + 2u * powerAbove(2u * bodyContacts() * params.counts.x);
}

// Run r's contacts' end in the pool.
fn runEnd(r: u32, runs: u32, pool: u32) -> u32 {
    return select(pool, blocks[runStartAt(r + 1u)], r + 1u < runs);
}

// Whether run r goes before run s: the higher priority, the lower index on
// a tie, as the core's keys.
fn runBefore(r: u32, s: u32) -> bool {
    let pr = blocks[runPriorityAt(r)];
    let ps = blocks[runPriorityAt(s)];
    return pr > ps || (pr == ps && r < s);
}

// Body x's runs' entries in the sorted (body, run) list of 2 runs.
fn bodyRunRange(x: u32, runs: u32) -> vec2<u32> {
    let n = powerAbove(2u * runs);
    return vec2<u32>(sortedFrom(bodyRunsAt(), n, x, 0u), sortedFrom(bodyRunsAt(), n, x + 1u, 0u));
}

// Whether run r's neighbours before it all have colours, and the least
// colour none of them has (NONE while one has none), as the core's
// greedy colouring in descending priority (Jones and Plassmann).
fn readyColour(r: u32, runs: u32) -> u32 {
    let head = runStartAt(r);
    let first = blocks[head];
    let bodies = vec2<u32>(blocks[poolAt(first)], blocks[poolAt(first) + 1u]);
    for (var colour = 0u; colour <= runs; colour++) {
        var taken = false;
        for (var side = 0u; side < 2u; side++) {
            let range = bodyRunRange(bodies[side], runs);
            for (var e = range.x; e < range.y; e++) {
                let s = blocks[bodyRunsAt() + 2u * e + 1u];
                if (s == r || !runBefore(s, r)) {
                    continue;
                }
                let c = blocks[runColourAt(s)];
                if (c == NONE) {
                    return NONE;
                }
                taken = taken || c == colour;
            }
        }
        if (!taken) {
            return colour;
        }
    }
    return NONE;
}

// The runs of the pool's contacts and their colours, as the core's
// mvfxColourBodyPairs: a run where the pair changes, its priority pcg4d
// of its bodies' ranks; colours in rounds, each run ready once its
// neighbours before it have theirs; the passes' order by colour, then
// run. Returns (runs, colours).
fn pairColour(t: u32, pool: u32) -> vec2<u32> {
    var runs = 0u;
    for (var base = 0u; base < pool; base += PAIR_GROUP) {
        let k = base + t;
        var starts = 0u;
        if (k < pool) {
            let at = poolAt(k);
            starts = select(0u, 1u, k == 0u || blocks[at] != blocks[at - PAIR_WORDS] ||
                                        blocks[at + 1u] != blocks[at + 1u - PAIR_WORDS]);
        }
        let before = exclusiveScan(t, starts);
        if (starts != 0u) {
            blocks[runStartAt(runs + before)] = k;
        }
        runs += scan[CHUNK - 1u];
        workgroupBarrier();
    }
    runs = pairUniform(t, runs);
    pairWait();
    for (var r = t; r < runs; r += PAIR_GROUP) {
        let at = poolAt(blocks[runStartAt(r)]);
        let a = blocks[at];
        let b = blocks[at + 1u];
        blocks[runPriorityAt(r)] =
            pcg4d(vec4<u32>(blocks[infoAt(a) + 2u], blocks[infoAt(b) + 2u], PAIR_SALT, 0u)).x;
        blocks[runColourAt(r)] = NONE;
        blocks[bodyRunsAt() + 4u * r] = a;
        blocks[bodyRunsAt() + 4u * r + 1u] = r;
        blocks[bodyRunsAt() + 4u * r + 2u] = b;
        blocks[bodyRunsAt() + 4u * r + 3u] = r;
    }
    pairWait();
    pairSort(t, bodyRunsAt(), 2u * runs);
    if (t == 0u) {
        _ = atomicExchange(&pairColours, 0u);
    }
    loop {
        if (t == 0u) {
            _ = atomicExchange(&pairLeft, 0u);
        }
        workgroupBarrier();
        for (var r = t; r < runs; r += PAIR_GROUP) {
            var next = NONE;
            if (blocks[runColourAt(r)] == NONE) {
                next = readyColour(r, runs);
                atomicAdd(&pairLeft, 1u);
            }
            blocks[runNextAt(r)] = next;
        }
        pairWait();
        for (var r = t; r < runs; r += PAIR_GROUP) {
            let next = blocks[runNextAt(r)];
            if (next != NONE) {
                blocks[runColourAt(r)] = next;
                atomicMax(&pairColours, next + 1u);
            }
        }
        pairWait();
        if (pairUniform(t, atomicLoad(&pairLeft)) == 0u) {
            break;
        }
    }
    let colours = pairUniform(t, atomicLoad(&pairColours));
    for (var r = t; r < runs; r += PAIR_GROUP) {
        blocks[colourRunsAt() + 2u * r] = blocks[runColourAt(r)];
        blocks[colourRunsAt() + 2u * r + 1u] = r;
    }
    pairWait();
    pairSort(t, colourRunsAt(), runs);
    return vec2<u32>(runs, colours);
}

// A pass of a stage over the pairs, colour by colour (backward from the
// last), the runs of a colour at once: stabilizing (0), pressing (1,
// pushing with friction after the first round), bouncing (2).
fn pairPass(t: u32, stage: u32, round: u32, found: vec3<u32>, h: f32, still: f32) {
    let runs = found.x;
    let colours = found.y;
    let pool = found.z;
    let backward = round % 2u == 1u;
    let n = powerAbove(runs);
    for (var k = 0u; k < colours; k++) {
        let colour = select(k, colours - 1u - k, backward);
        let first = sortedFrom(colourRunsAt(), n, colour, 0u);
        let end = sortedFrom(colourRunsAt(), n, colour + 1u, 0u);
        for (var e = first + t; e < end; e += PAIR_GROUP) {
            let r = blocks[colourRunsAt() + 2u * e + 1u];
            let head = blocks[runStartAt(r)];
            let to = runEnd(r, runs, pool);
            if (stage == 2u) {
                bouncePair(head, to, backward, h, still);
            } else {
                solvePair(head, to, stage == 0u, stage == 1u && round > 0u, backward, h);
            }
        }
        pairWait();
    }
}
