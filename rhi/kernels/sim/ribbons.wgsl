// The sim kernel's ribbons (spliced into sim.wgsl): the ribbon each birth
// joins and its distance along it, as the core's AssignRibbon and
// RibbonDistanceOf decide them while it appends the step's births, in four
// passes after scatter (which keeps where each birth went): ribbonScan
// places the births the CPU path
// appends (each one's place among them, the latest counter birth before
// it) and the live particles a birth looks back at; ribbonFacts gives each
// its places and its kind; ribbonLinks each its point before and whether
// it breaks the counter's ribbon; ribbonIds the counter's ids by a scan of
// the breaks, and each distance by pointer jumping along the links, both
// stored with each kept birth.
//
// The look-back is the core's: a birth's ribbon's youngest point among
// the RIBBON_LOOKBACK entries before it, the births appended before it,
// then the live particles the CPU path's array holds while it appends
// (those not ended at the step's start, killed in their move or not, at
// their moved places). The counter's ids and the others never meet (by
// source the counter's take the top bit), so a counter birth's point
// before is the latest counter birth (none past the look-back), or for
// the first a live particle of the counter's id; a birth of a fixed id
// (by batch, by source) finds its own by looking back. Distances are
// sums along the links, added in another order than the core's.

// How far back a birth looks for its ribbon's point before, as the core.
const RIBBON_LOOKBACK: u32 = 256u;

// The ribbon ids (mvfxRibbonIds) the passes tell apart.
const RIBBON_ONE: u32 = 1u;
const RIBBON_PER_SOURCE: u32 = 3u;

// By source, the counter's ribbons take ids with the top bit set and a
// source's ribbon its serial's low bits, as the core's.
const RIBBON_COUNTER_TAG: u32 = 0x80000000u;

// The statistics record's word counting the ribbon breaks the GPU made
// since the instance's last clear (MVFX_GPU_RIBBON_BREAKS).
const STATISTICS_RIBBON_BREAKS: u32 = 38u;

// A birth's kind: a fixed id, the counter's, the counter's broken by it.
const KIND_FIXED: u32 = 0u;
const KIND_COUNTER: u32 = 1u;
const KIND_BREAK: u32 = 2u;

// A link to a live particle of the window, by its place there.
const LINK_LIVE: u32 = 0x80000000u;

// The words after the births' marks in accepted (rhi/src/sim.c gives the
// room): from twice the capacity, where scatter put each kept birth; from
// four times it, arrays of the
// capacity over the appended (at most the capacity): the birth each is,
// its place at its tick and after its move (three each), its id, its
// kind, the latest counter birth before it, two values and two links for
// the pointer jumping; then the head (the appended, the window's
// particles) and the window, latest first.
const ARRAY_BIRTH: u32 = 0u;
const ARRAY_BORN: u32 = 1u;
const ARRAY_MOVED: u32 = 4u;
const ARRAY_ID: u32 = 7u;
const ARRAY_KIND: u32 = 8u;
const ARRAY_LAST: u32 = 9u;
const ARRAY_VALUE: u32 = 10u;
const ARRAY_LINK: u32 = 12u;
const RIBBON_ARRAYS: u32 = 14u;
const HEAD_APPENDED: u32 = 0u;
const HEAD_HELD: u32 = 1u;
const HEAD_WORDS: u32 = 2u;

fn keptAt(j: u32) -> u32 {
    return accepted[2u * params.counts.x + j];
}

fn ribbonWord(array: u32, a: u32) -> u32 {
    return (4u + array) * params.counts.x + a;
}

fn headWord(k: u32) -> u32 {
    return (4u + RIBBON_ARRAYS) * params.counts.x + k;
}

fn windowWord(k: u32) -> u32 {
    return headWord(HEAD_WORDS) + k;
}

fn ribbonVector(array: u32, a: u32) -> vec3<f32> {
    return vec3<f32>(bitcast<f32>(accepted[ribbonWord(array, a)]),
                     bitcast<f32>(accepted[ribbonWord(array + 1u, a)]),
                     bitcast<f32>(accepted[ribbonWord(array + 2u, a)]));
}

// Whether birth j takes the counter's ribbon: every birth by the counter,
// one not of an event by source, none by batch.
fn takesCounter(j: u32) -> bool {
    let ids = params.ribbon.x;
    return ids == RIBBON_ONE || (ids == RIBBON_PER_SOURCE && births[3u * j + 2u].w == 0u);
}

fn counterTag() -> u32 {
    return select(0u, RIBBON_COUNTER_TAG, params.ribbon.x == RIBBON_PER_SOURCE);
}

// The distance between two places, summed in the core's order.
fn gapBetween(a: vec3<f32>, b: vec3<f32>) -> f32 {
    let d = a - b;
    var square = d.x * d.x;
    square += d.y * d.y;
    square += d.z * d.z;
    return sqrt(square);
}

// Live particle i's ribbon and distance.
fn liveId(i: u32) -> u32 {
    return source[params.words.ribbon * params.counts.x + i];
}

fn liveAlong(i: u32) -> f32 {
    if (params.words.ribbonDistance == NONE) {
        return 0.0;
    }
    return bitcast<f32>(source[params.words.ribbonDistance * params.counts.x + i]);
}

// Each element's largest of v over those before it in the workgroup (0
// for the first).
fn exclusiveMax(local: u32, v: u32) -> u32 {
    scan[local] = v;
    workgroupBarrier();
    for (var offset = 1u; offset < CHUNK; offset <<= 1u) {
        var other = 0u;
        if (local >= offset) {
            other = scan[local - offset];
        }
        workgroupBarrier();
        scan[local] = max(scan[local], other);
        workgroupBarrier();
    }
    return select(0u, scan[max(local, 1u) - 1u], local > 0u);
}

// Ribbon scan, in one workgroup: the window (in its first invocation),
// then each appended birth's place among them and the latest counter birth
// before it, a chunk of the births at a time.
@compute @workgroup_size(256, 1, 1)
fn ribbonScan(@builtin(local_invocation_index) local: u32) {
    let c = params.counts.x;
    if (local == 0u) {
        var held = 0u;
        // The live count before the step (offsets moved it to counts[1]).
        var i = counts[1];
        while (i > 0u && held < RIBBON_LOOKBACK) {
            i--;
            if ((source[params.words.alive * c + i] & LIVE_ENDED) == 0u) {
                accepted[windowWord(held)] = i;
                held++;
            }
        }
        accepted[headWord(HEAD_HELD)] = held;
        carried = 0u;
        taken = 0u;
        liveCount = birthCount();
    }
    let n = workgroupUniformLoad(&liveCount);
    for (var first = 0u; first < n; first += CHUNK) {
        let j = first + local;
        let keep = select(0u, 1u, j < n && appended(j));
        let a = carried + exclusiveScan(local, keep);
        let counter = keep == 1u && takesCounter(j);
        // The latest counter birth before, from 1 (0 for none).
        let last = max(taken, exclusiveMax(local, select(0u, a + 1u, counter)));
        if (keep == 1u) {
            accepted[ribbonWord(ARRAY_BIRTH, a)] = j;
            accepted[ribbonWord(ARRAY_LAST, a)] = select(NONE, last - 1u, last > 0u);
        }
        // The chunk's last element carries its sums to the next chunk.
        workgroupBarrier();
        if (local == CHUNK - 1u) {
            carried = a + keep;
            taken = max(last, select(0u, a + 1u, counter));
        }
        workgroupBarrier();
    }
    if (local == 0u) {
        accepted[headWord(HEAD_APPENDED)] = carried;
    }
}

// Ribbon facts: each appended birth's place at its tick and after its
// move (where scatter stored it, moved again only for one killed on its
// way), its kind and, for a fixed id, its id (its batch's first serial,
// its serial less its batch index; its event's particle's serial).
@compute @workgroup_size(64, 1, 1)
fn ribbonFacts(@builtin(global_invocation_id) id: vec3<u32>) {
    let c = params.counts.x;
    let n = accepted[headWord(HEAD_APPENDED)];
    for (var a = id.x; a < n; a += params.modes.z * 64u) {
        let j = accepted[ribbonWord(ARRAY_BIRTH, a)];
        var born = vec3<f32>(0.0);
        var unused = vec3<f32>(0.0);
        bornPlace(j, false, &born, &unused);
        var place = vec3<f32>(0.0);
        if ((accepted[j] & 3u) == 1u && keptAt(j) < c) {
            place = readVector(1u, keptAt(j), params.words.place);
        } else {
            var velocity = vec3<f32>(0.0);
            _ = bornAt(j, &place, &velocity);
        }
        for (var k = 0u; k < 3u; k++) {
            accepted[ribbonWord(ARRAY_BORN + k, a)] = bitcast<u32>(born[k]);
            accepted[ribbonWord(ARRAY_MOVED + k, a)] = bitcast<u32>(place[k]);
        }
        var kind = KIND_FIXED;
        var fixed = births[3u * j].x - births[3u * j + 1u].z;
        if (takesCounter(j)) {
            kind = KIND_COUNTER;
        } else if (params.ribbon.x == RIBBON_PER_SOURCE) {
            fixed = births[6u * c + 4u * births[3u * j + 2u].y + 3u].w & ~RIBBON_COUNTER_TAG;
        }
        accepted[ribbonWord(ARRAY_KIND, a)] = kind;
        accepted[ribbonWord(ARRAY_ID, a)] = fixed;
    }
}

// The youngest live particle of the window of ribbon id among its first
// budget particles, as a link (NONE for none).
fn liveLink(id: u32, budget: u32) -> u32 {
    let count = min(accepted[headWord(HEAD_HELD)], budget);
    for (var k = 0u; k < count; k++) {
        if (liveId(accepted[windowWord(k)]) == id) {
            return LINK_LIVE | k;
        }
    }
    return NONE;
}

// Ribbon links: each appended birth's point before (a birth, a live
// particle of the window, none), its gap to it, whether it breaks the
// counter's ribbon; its value and link for the distances' pointer
// jumping (a live particle's distance taken in, its link ended).
@compute @workgroup_size(64, 1, 1)
fn ribbonLinks(@builtin(global_invocation_id) id: vec3<u32>) {
    let n = accepted[headWord(HEAD_APPENDED)];
    let start = params.modes.w + counts[STATISTICS_RIBBON_BREAKS];
    let breakDistance = bitcast<f32>(params.ribbon.y);
    for (var a = id.x; a < n; a += params.modes.z * 64u) {
        let budget = RIBBON_LOOKBACK - min(a, RIBBON_LOOKBACK);
        var link = NONE;
        let kind = accepted[ribbonWord(ARRAY_KIND, a)];
        if (kind == KIND_COUNTER) {
            let last = accepted[ribbonWord(ARRAY_LAST, a)];
            if (last != NONE) {
                link = select(NONE, last, a - last <= RIBBON_LOOKBACK);
            } else {
                link = liveLink(start | counterTag(), budget);
            }
        } else {
            let fixed = accepted[ribbonWord(ARRAY_ID, a)];
            for (var k = 1u; k <= min(a, RIBBON_LOOKBACK); k++) {
                let b = a - k;
                if (accepted[ribbonWord(ARRAY_KIND, b)] == KIND_FIXED &&
                    accepted[ribbonWord(ARRAY_ID, b)] == fixed) {
                    link = b;
                    break;
                }
            }
            if (link == NONE) {
                link = liveLink(fixed, budget);
            }
        }
        var gap = 0.0;
        var base = 0.0;
        if (link != NONE) {
            var before = vec3<f32>(0.0);
            if ((link & LINK_LIVE) != 0u) {
                let i = accepted[windowWord(link & ~LINK_LIVE)];
                before = readVector(0u, i, params.words.place);
                base = liveAlong(i);
            } else {
                before = ribbonVector(ARRAY_MOVED, link);
            }
            gap = gapBetween(ribbonVector(ARRAY_BORN, a), before);
        }
        if (kind == KIND_COUNTER && link != NONE && breakDistance > 0.0 && gap > breakDistance) {
            // A new ribbon: no point before it.
            accepted[ribbonWord(ARRAY_KIND, a)] = KIND_BREAK;
            link = NONE;
        }
        var value = 0.0;
        var next = NONE;
        if (link != NONE) {
            value = select(gap, base + gap, (link & LINK_LIVE) != 0u);
            next = select(link, NONE, (link & LINK_LIVE) != 0u);
        }
        accepted[ribbonWord(ARRAY_VALUE, a)] = bitcast<u32>(value);
        accepted[ribbonWord(ARRAY_LINK, a)] = next;
    }
}

// Ribbon ids, in one workgroup: each counter birth's id, the counter at
// the step's start plus the breaks up to it, by a scan of the breaks
// (their count kept in the statistics record); then, for an emitter that
// stores distances, each birth's distance by pointer jumping along the
// links, in ping-pong arrays, as many rounds as halve the longest chain;
// each kept birth's id and distance stored where scatter put it.
@compute @workgroup_size(256, 1, 1)
fn ribbonIds(@builtin(local_invocation_index) local: u32) {
    if (local == 0u) {
        liveCount = accepted[headWord(HEAD_APPENDED)];
        carried = 0u;
    }
    let n = workgroupUniformLoad(&liveCount);
    let start = params.modes.w + counts[STATISTICS_RIBBON_BREAKS];
    let tag = counterTag();
    for (var first = 0u; first < n; first += CHUNK) {
        let a = first + local;
        var kind = KIND_FIXED;
        if (a < n) {
            kind = accepted[ribbonWord(ARRAY_KIND, a)];
        }
        let broken = select(0u, 1u, kind == KIND_BREAK);
        let breaks = carried + exclusiveScan(local, broken) + broken;
        if (kind != KIND_FIXED) {
            accepted[ribbonWord(ARRAY_ID, a)] = (start + breaks) | tag;
        }
        workgroupBarrier();
        if (local == CHUNK - 1u) {
            carried = breaks;
        }
        workgroupBarrier();
    }
    if (local == 0u) {
        counts[STATISTICS_RIBBON_BREAKS] += carried;
    }
    if (params.words.ribbonDistance != NONE) {
        sumDistances(local, n);
    }
    storageBarrier();
    let c = params.counts.x;
    for (var a = local; a < n; a += CHUNK) {
        let j = accepted[ribbonWord(ARRAY_BIRTH, a)];
        let at = keptAt(j);
        if ((accepted[j] & 3u) == 1u && at < c) {
            moved[params.words.ribbon * c + at] = accepted[ribbonWord(ARRAY_ID, a)];
            if (params.words.ribbonDistance != NONE) {
                moved[params.words.ribbonDistance * c + at] =
                    accepted[ribbonWord(ARRAY_VALUE, a)];
            }
        }
    }
}

// Each appended birth's distance in its first value: pointer jumping over
// n births along their links, a round halving every chain.
fn sumDistances(local: u32, n: u32) {
    let rounds = 32u - countLeadingZeros(max(n, 1u));
    for (var r = 0u; r < rounds; r++) {
        let read = r % 2u;
        let write = 1u - read;
        for (var a = local; a < n; a += CHUNK) {
            let link = accepted[ribbonWord(ARRAY_LINK + read, a)];
            var value = bitcast<f32>(accepted[ribbonWord(ARRAY_VALUE + read, a)]);
            var next = NONE;
            if (link != NONE) {
                value = bitcast<f32>(accepted[ribbonWord(ARRAY_VALUE + read, link)]) + value;
                next = accepted[ribbonWord(ARRAY_LINK + read, link)];
            }
            accepted[ribbonWord(ARRAY_VALUE + write, a)] = bitcast<u32>(value);
            accepted[ribbonWord(ARRAY_LINK + write, a)] = next;
        }
        storageBarrier();
    }
    if (rounds % 2u == 1u) {
        for (var a = local; a < n; a += CHUNK) {
            accepted[ribbonWord(ARRAY_VALUE, a)] = accepted[ribbonWord(ARRAY_VALUE + 1u, a)];
        }
    }
}
