// The sim kernel's rigid pairs' finding (spliced into sim.wgsl beside
// sim/rigid_pairs.wgsl), as the core's mvfxFindBodyPairs: the bodies'
// ranks, the hierarchical pair grid sorted in the workgroup, each body's
// candidates in index order, and the pool filled in the bodies' order up
// to its room.

// A body's words beside its solve: its reach, its flags (FLAG_*), its
// rank among the bodies the step moves (the core's index, the ended taken
// out), its level in the pair grid and its cell there, its pair
// contacts' count and first place in the pool, whether a pair touched it.
const INFO_WORDS: u32 = 12u;
const FLAG_ASLEEP: u32 = 1u;
// The grid's levels at most (the core's MOST_LEVELS) and its keys' salt
// (MVFX_GRID_SALT).
const MOST_LEVELS: u32 = 24u;
const PAIR_GRID_SALT: u32 = 0x4E474244u;

var<workgroup> pairHeld: u32;
var<workgroup> pairLeast: atomic<u32>;
var<workgroup> pairMost: atomic<u32>;
var<workgroup> pairPresent: atomic<u32>;

// v of invocation 0, the same in every invocation (control flow uniform).
fn pairUniform(t: u32, v: u32) -> u32 {
    workgroupBarrier();
    if (t == 0u) {
        pairHeld = v;
    }
    return workgroupUniformLoad(&pairHeld);
}

// The least power of two at least n.
fn powerAbove(n: u32) -> u32 {
    var p = 1u;
    while (p < n) {
        p <<= 1u;
    }
    return p;
}

fn infoAt(i: u32) -> u32 {
    return pairBase() + (FATE_WORDS + SOLID_WORDS + WORLD_WORDS) * params.counts.x + INFO_WORDS * i;
}

// The sorted grid's entries (key, index), a power of two of them.
fn gridAt() -> u32 {
    return pairBase() + (FATE_WORDS + SOLID_WORDS + WORLD_WORDS + INFO_WORDS) * params.counts.x;
}

fn bodyContacts() -> u32 {
    return bitcast<u32>(params.rigidContact.w);
}

// The pool's contact k.
fn poolAt(k: u32) -> u32 {
    return gridAt() + 2u * powerAbove(params.counts.x) + PAIR_WORDS * k;
}

// Sorts n entries of two words (key, value) at base ascending, in place,
// by a bitonic network over the power of two above n, the entries past n
// set to the largest (n uniform).
fn pairSort(t: u32, base: u32, n: u32) {
    let p = powerAbove(n);
    for (var i = n + t; i < p; i += PAIR_GROUP) {
        blocks[base + 2u * i] = NONE;
        blocks[base + 2u * i + 1u] = NONE;
    }
    pairWait();
    for (var k = 2u; k <= p; k <<= 1u) {
        for (var j = k >> 1u; j > 0u; j >>= 1u) {
            for (var i = t; i < p; i += PAIR_GROUP) {
                let l = i ^ j;
                if (l > i) {
                    let ki = blocks[base + 2u * i];
                    let vi = blocks[base + 2u * i + 1u];
                    let kl = blocks[base + 2u * l];
                    let vl = blocks[base + 2u * l + 1u];
                    let greater = ki > kl || (ki == kl && vi > vl);
                    if (greater == ((i & k) == 0u)) {
                        blocks[base + 2u * i] = kl;
                        blocks[base + 2u * i + 1u] = vl;
                        blocks[base + 2u * l] = ki;
                        blocks[base + 2u * l + 1u] = vi;
                    }
                }
            }
            pairWait();
        }
    }
}

// The first of n sorted entries at base not below (key, value).
fn sortedFrom(base: u32, n: u32, key: u32, value: u32) -> u32 {
    var low = 0u;
    var high = n;
    while (low < high) {
        let mid = (low + high) / 2u;
        let k = blocks[base + 2u * mid];
        let v = blocks[base + 2u * mid + 1u];
        if (k < key || (k == key && v < value)) {
            low = mid + 1u;
        } else {
            high = mid;
        }
    }
    return low;
}

// The level of a reach, as the core's LevelOf: the least with the reach
// at most base 2^level.
fn levelOf(reach: f32, base: f32) -> u32 {
    var level = 0u;
    var bound = base;
    while (reach > bound && level < MOST_LEVELS - 1u) {
        bound *= 2.0;
        level += 1u;
    }
    return level;
}

// A level's cells' inverse width, as the core's CellOf (2 base 2^level).
fn inverseCellOf(base: f32, level: u32) -> f32 {
    return 1.0 / (2.0 * base * exp2(f32(level)));
}

fn cellAt(x: vec3<f32>, inverse: f32) -> vec3<i32> {
    return vec3<i32>(cellOf(x.x, inverse), cellOf(x.y, inverse), cellOf(x.z, inverse));
}

fn levelKey(cell: vec3<i32>, level: u32) -> u32 {
    return pcg4d(vec4<u32>(bitcast<vec3<u32>>(cell), PAIR_GRID_SALT + level)).x;
}

fn isEnded(i: u32) -> bool {
    return source[params.words.alive * params.counts.x + i] == LIVE_ENDED;
}

// The grid as found: its least reach, its levels with bodies, the bodies
// the step moves and the live count.
struct PairGrid {
    base: f32,
    present: u32,
    count: u32,
    live: u32,
}

// Whether body b, met at level and cell from a's search, is a's to find
// as the core's CandidatesAt: of that level and cell, after a or of a
// higher level, not two asleep, within their reaches.
fn isCandidate(a: u32, b: u32, level: u32, cell: vec3<i32>, own: u32) -> bool {
    let ib = infoAt(b);
    if (blocks[ib + 3u] != level || blocks[ib + 4u] != u32(cell.x) ||
        blocks[ib + 5u] != u32(cell.y) || blocks[ib + 6u] != u32(cell.z)) {
        return false;
    }
    if (!(level > own || b > a)) {
        return false;
    }
    let ia = infoAt(a);
    if ((blocks[ia + 1u] & blocks[ib + 1u] & FLAG_ASLEEP) != 0u) {
        return false;
    }
    let reach = bitcast<f32>(blocks[ia]) + bitcast<f32>(blocks[ib]);
    let xa = loadVector(solidAt(a));
    let xb = loadVector(solidAt(b));
    var square = 0.0;
    for (var k = 0u; k < 3u; k++) {
        let d = xa[k] - xb[k];
        square += d * d;
    }
    return square < reach * reach;
}

// a's least candidate from index past on, NONE for none: over the
// levels from its own, each of the 27 cells about it there, the first
// sorted entry of that cell's key not below from that passes.
fn nextCandidate(a: u32, past: u32, grid: PairGrid) -> u32 {
    let own = blocks[infoAt(a) + 3u];
    let x = loadVector(solidAt(a));
    let entries = powerAbove(grid.live);
    var best = NONE;
    for (var level = own; level < MOST_LEVELS; level++) {
        if (((grid.present >> level) & 1u) == 0u) {
            continue;
        }
        let cell = cellAt(x, inverseCellOf(grid.base, level));
        for (var n = 0u; n < 27u; n++) {
            let around = cell + vec3<i32>(i32(n % 3u) - 1, i32(n / 3u % 3u) - 1,
                                          i32(n / 9u) - 1);
            let key = levelKey(around, level);
            for (var e = sortedFrom(gridAt(), entries, key, past); e < entries; e++) {
                let b = blocks[gridAt() + 2u * e + 1u];
                if (blocks[gridAt() + 2u * e] != key || b >= best) {
                    break;
                }
                if (isCandidate(a, b, level, around, own)) {
                    best = b;
                    break;
                }
            }
        }
    }
    return best;
}

// Two bodies' contacts, the lower against the higher as a sphere or a box
// it is kept out of, as the core's mvfxGatherPairContacts for spheres and
// boxes.
fn pairGather(low: u32, high: u32) -> Contacts {
    let a = loadBody(low);
    let b = loadBody(high);
    let sizeA = rigidSizeOf(a);
    let sizeB = rigidSizeOf(b);
    let sa = shapeOf(sizeA);
    let sb = shapeOf(sizeB);
    var list = Contacts();
    list.margin = CONTACT_MARGIN * min(rigidReach(sizeA), rigidReach(sizeB));
    var c = Collider();
    c.modes = vec4<u32>(select(COLLIDER_SPHERE, COLLIDER_BOX, sb.kind == RIGID_BOX), 0u, 0u, 0u);
    c.center = vec4<f32>(b.moving.place, sb.radius);
    c.halfExtents = vec4<f32>(sb.half, 0.0);
    c.rotation = b.moving.turning.orientation;
    let x = a.moving.place;
    let q = a.moving.turning.orientation;
    if (sa.kind == RIGID_BOX) {
        boxBoxContacts(x, q, sa, c, &list);
    } else {
        outsideContacts(x, q, sa, c, &list);
    }
    return list;
}

// Writes each of the first n bodies' value f(i) summed over those before
// it in index order to word `word` of its info, the ended counting 0;
// returns the sum of all (n uniform). f: 0 the step's bodies (ranks),
// 1 the pair contacts (pool places).
fn pairScan(t: u32, n: u32, which: u32, word: u32) -> u32 {
    var carry = 0u;
    for (var base = 0u; base < n; base += PAIR_GROUP) {
        let i = base + t;
        var v = 0u;
        if (i < n && !isEnded(i)) {
            v = select(blocks[infoAt(i) + 7u], 1u, which == 0u);
        }
        let before = exclusiveScan(t, v);
        if (i < n) {
            blocks[infoAt(i) + word] = carry + before;
        }
        carry += scan[CHUNK - 1u];
        workgroupBarrier();
    }
    return carry;
}

// The pair grid of the step's bodies at their poses, as the core's
// BuildLevels: the least and widest reach, each body's level, cell and
// key, sorted by key and index.
fn pairLevels(t: u32, live: u32) -> PairGrid {
    var grid = PairGrid();
    grid.live = live;
    grid.count = pairUniform(t, pairScan(t, live, 0u, 2u));
    if (t == 0u) {
        _ = atomicExchange(&pairLeast, NONE);
        _ = atomicExchange(&pairMost, 0u);
        _ = atomicExchange(&pairPresent, 0u);
    }
    workgroupBarrier();
    for (var i = t; i < live; i += PAIR_GROUP) {
        if (!isEnded(i)) {
            let reach = blocks[infoAt(i)];
            atomicMin(&pairLeast, reach);
            atomicMax(&pairMost, reach);
        }
    }
    workgroupBarrier();
    let least = bitcast<f32>(pairUniform(t, atomicLoad(&pairLeast)));
    let widest = bitcast<f32>(pairUniform(t, atomicLoad(&pairMost)));
    grid.base = max(least, max(widest * exp2(1.0 - f32(MOST_LEVELS)), 1e-6));
    for (var i = t; i < live; i += PAIR_GROUP) {
        var key = NONE;
        var index = NONE;
        if (!isEnded(i)) {
            let at = infoAt(i);
            let level = levelOf(bitcast<f32>(blocks[at]), grid.base);
            let cell = cellAt(loadVector(solidAt(i)), inverseCellOf(grid.base, level));
            blocks[at + 3u] = level;
            blocks[at + 4u] = u32(cell.x);
            blocks[at + 5u] = u32(cell.y);
            blocks[at + 6u] = u32(cell.z);
            atomicOr(&pairPresent, 1u << level);
            key = levelKey(cell, level);
            index = i;
        }
        blocks[gridAt() + 2u * i] = key;
        blocks[gridAt() + 2u * i + 1u] = index;
    }
    workgroupBarrier();
    grid.present = pairUniform(t, atomicLoad(&pairPresent));
    pairWait();
    pairSort(t, gridAt(), live);
    return grid;
}

// The pairs of the step's bodies, as the core's mvfxFindBodyPairs: each
// body's candidates' contacts counted, their places in the pool by the
// bodies' order, then each body's pairs in its candidates' order written,
// those past the pool's room dropped and counted against it. Returns the
// pool's contacts.
fn pairFind(t: u32, live: u32) -> u32 {
    if (bodyContacts() == 0u) {
        return 0u;
    }
    let grid = pairLevels(t, live);
    if (grid.count < 2u) {
        return 0u;
    }
    for (var a = t; a < live; a += PAIR_GROUP) {
        if (isEnded(a)) {
            continue;
        }
        var count = 0u;
        for (var b = nextCandidate(a, 0u, grid); b != NONE; b = nextCandidate(a, b + 1u, grid)) {
            count += pairGather(min(a, b), max(a, b)).count;
        }
        blocks[infoAt(a) + 7u] = count;
    }
    pairWait();
    let total = pairUniform(t, pairScan(t, live, 1u, 8u));
    let room = bodyContacts() * grid.count;
    for (var a = t; a < live; a += PAIR_GROUP) {
        if (isEnded(a)) {
            continue;
        }
        var at = blocks[infoAt(a) + 8u];
        var fate = loadFate(a);
        for (var b = nextCandidate(a, 0u, grid); b != NONE; b = nextCandidate(a, b + 1u, grid)) {
            let low = min(a, b);
            let high = max(a, b);
            var list = pairGather(low, high);
            fate.capped += list.capped;
            fate.searches += list.searches;
            if (list.count > 0u) {
                blocks[infoAt(low) + 9u] = 1u;
                blocks[infoAt(high) + 9u] = 1u;
            }
            let bodyB = loadSolid(high);
            let bodyA = loadSolid(low);
            for (var j = 0u; j < list.count; j++) {
                if (at >= room) {
                    fate.capped += list.count - j;
                    break;
                }
                let item = list.items[j];
                var c = PairContact();
                c.a = low;
                c.b = high;
                c.onA = item.body;
                c.onB = rotate(conjugate(bodyB.q), item.surface - bodyB.x);
                c.normal = item.normal;
                var ra = vec3<f32>(0.0);
                var rb = vec3<f32>(0.0);
                _ = pairDepth(c, bodyA, bodyB, false, &ra, &rb);
                c.speed = pairSpeed(c, bodyA, bodyB, ra, rb);
                storePair(poolAt(at), c);
                at += 1u;
            }
        }
        storeFate(a, fate);
    }
    pairWait();
    return min(total, room);
}
