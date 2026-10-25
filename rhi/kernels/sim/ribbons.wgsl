// The sim kernel's ribbons (spliced into sim.wgsl): the ribbon each birth
// joins and its distance along it, as the core's AssignRibbon and
// RibbonDistanceOf decide them while it appends the step's births, in one
// pass after scatter, in one workgroup: it places the births the CPU path
// appends (each one's place among them, the latest counter birth before
// it), gives each its point before and whether it breaks the counter's
// ribbon, the counter's ids by a scan of the breaks, each distance by
// pointer jumping along the links, and stores both with each kept birth.
// Each birth's places at its tick and after its move come from scatter
// (spawnAt) for a kept birth and killBirths for a killed one, where the
// move was taken anyway: a pass costs more than its work here.
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

// The ribbon ids (mvfxRibbonIds) the pass tells apart.
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
// four times it, six arrays over the births (twice the capacity each):
// each one's place at its tick and after its move; from sixteen times it,
// arrays of the capacity over the appended (at most the capacity): the
// birth each is, its id, its kind, the latest counter birth before it,
// two values and two links for the pointer jumping; then the head (the
// window's particles) and the window, latest first.
const PLACE_BORN: u32 = 0u;
const PLACE_MOVED: u32 = 3u;
const ARRAY_BIRTH: u32 = 0u;
const ARRAY_ID: u32 = 1u;
const ARRAY_KIND: u32 = 2u;
const ARRAY_LAST: u32 = 3u;
const ARRAY_VALUE: u32 = 4u;
const ARRAY_LINK: u32 = 6u;
const RIBBON_ARRAYS: u32 = 8u;
const HEAD_WORDS: u32 = 1u;

fn keptAt(j: u32) -> u32 {
    return accepted[2u * params.counts.x + j];
}

fn placeWord(k: u32, j: u32) -> u32 {
    return 4u * params.counts.x + 2u * params.counts.x * k + j;
}

fn ribbonWord(array: u32, a: u32) -> u32 {
    return (16u + array) * params.counts.x + a;
}

fn headWord() -> u32 {
    return (16u + RIBBON_ARRAYS) * params.counts.x;
}

fn windowWord(k: u32) -> u32 {
    return headWord() + HEAD_WORDS + k;
}

// Stores birth j's places at its tick and after its move, for an emitter
// with ribbons.
fn storeBirthPlaces(j: u32, born: vec3<f32>, place: vec3<f32>) {
    if (params.words.ribbon == NONE) {
        return;
    }
    for (var k = 0u; k < 3u; k++) {
        accepted[placeWord(PLACE_BORN + k, j)] = bitcast<u32>(born[k]);
        accepted[placeWord(PLACE_MOVED + k, j)] = bitcast<u32>(place[k]);
    }
}

fn birthPlace(first: u32, j: u32) -> vec3<f32> {
    return vec3<f32>(bitcast<f32>(accepted[placeWord(first, j)]),
                     bitcast<f32>(accepted[placeWord(first + 1u, j)]),
                     bitcast<f32>(accepted[placeWord(first + 2u, j)]));
}

// Whether birth j takes the counter's ribbon: every birth by the counter,
// one not of an event by source, none by batch, none of a unit.
fn takesCounter(j: u32) -> bool {
    if (unitPoints() > 1u) {
        return false;
    }
    let ids = params.ribbon.x;
    return ids == RIBBON_ONE || (ids == RIBBON_PER_SOURCE && birthWord(j, 2u).w == 0u);
}

// Birth j's fixed id: a unit's point its unit's first point's serial (a
// unit is a ribbon, as the core's AssignRibbon); its batch's first serial
// (its serial less its batch index; an event birth's batch index is 0),
// or by source its event's particle's serial (in its host record's free
// word).
fn fixedId(j: u32) -> u32 {
    if (unitPoints() > 1u) {
        return birthWord(j, 0u).x * unitPoints();
    }
    if (params.ribbon.x == RIBBON_PER_SOURCE) {
        let host = birthWord(j, 2u).y;
        return births[6u * params.counts.x + 4u * host + 3u].w & ~RIBBON_COUNTER_TAG;
    }
    return birthWord(j, 0u).x - birthWord(j, 1u).z;
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

// The youngest live particle of the window of ribbon id among its first
// budget particles, as a link (NONE for none).
fn liveLink(id: u32, budget: u32) -> u32 {
    let count = min(accepted[headWord()], budget);
    for (var k = 0u; k < count; k++) {
        if (liveId(accepted[windowWord(k)]) == id) {
            return LINK_LIVE | k;
        }
    }
    return NONE;
}

// Ribbons, in one workgroup, for an emitter with ribbons.
@compute @workgroup_size(256, 1, 1)
fn ribbons(@builtin(local_invocation_index) local: u32) {
    if (local == 0u) {
        // The window, in the first invocation: the live count before the
        // step is in counts[1] since offsets.
        let c = params.counts.x;
        var held = 0u;
        var i = counts[1];
        while (i > 0u && held < RIBBON_LOOKBACK) {
            // Read, then step: maul-rhi's stale_reads falsely refuses the reverse.
            let below = i - 1u;
            if ((source[params.words.alive * c + below] & LIVE_ENDED) == 0u) {
                accepted[windowWord(held)] = below;
                held++;
            }
            i = below;
        }
        accepted[headWord()] = held;
        liveCount = birthCount();
    }
    let count = workgroupUniformLoad(&liveCount);
    placeAppended(local, count);
    let n = workgroupUniformLoad(&carried);
    storageBarrier();
    linkAppended(local, n);
    storageBarrier();
    countBreaks(local, n);
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

// Each appended birth's place among them (their count left in carried),
// the birth it is, its kind and fixed id, and the latest counter birth
// before it: each invocation walks a run of the births twice, joined by
// one scan of the runs' counts and one of their latest counter births
// (a workgroup barrier costs more than a run's work).
fn placeAppended(local: u32, count: u32) {
    let per = (count + CHUNK - 1u) / CHUNK;
    let lo = min(local * per, count);
    let hi = min(lo + per, count);
    var kept = 0u;
    // The run's latest counter birth, from 1 among its appended.
    var latest = 0u;
    for (var j = lo; j < hi; j++) {
        if (appended(j)) {
            // Read, then step: maul-rhi's stale_reads falsely refuses the reverse.
            let next = kept + 1u;
            latest = select(latest, next, takesCounter(j));
            kept = next;
        }
    }
    let base = exclusiveScan(local, kept);
    // The latest counter birth before the run, from 1 (0 for none).
    var last = exclusiveMax(local, select(0u, base + latest, latest > 0u));
    var a = base;
    for (var j = lo; j < hi; j++) {
        if (appended(j)) {
            let counter = takesCounter(j);
            accepted[ribbonWord(ARRAY_BIRTH, a)] = j;
            accepted[ribbonWord(ARRAY_LAST, a)] = select(NONE, last - 1u, last > 0u);
            accepted[ribbonWord(ARRAY_KIND, a)] = select(KIND_FIXED, KIND_COUNTER, counter);
            accepted[ribbonWord(ARRAY_ID, a)] = select(fixedId(j), 0u, counter);
            last = select(last, a + 1u, counter);
            a++;
        }
    }
    if (local == CHUNK - 1u) {
        carried = base + kept;
    }
}

// Each appended birth's point before (a birth, a live particle of the
// window, none), its gap to it, whether it breaks the counter's ribbon;
// its value and link for the distances' pointer jumping (a live
// particle's distance taken in, its link ended).
fn linkAppended(local: u32, n: u32) {
    let start = params.modes.w + counts[STATISTICS_RIBBON_BREAKS];
    let breakDistance = bitcast<f32>(params.ribbon.y);
    for (var a = local; a < n; a += CHUNK) {
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
                before = birthPlace(PLACE_MOVED, accepted[ribbonWord(ARRAY_BIRTH, link)]);
            }
            let j = accepted[ribbonWord(ARRAY_BIRTH, a)];
            gap = gapBetween(birthPlace(PLACE_BORN, j), before);
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

// Each counter birth's id, the counter at the step's start plus the
// breaks up to it, by runs joined by a scan of their breaks, their count
// kept in the statistics record.
fn countBreaks(local: u32, n: u32) {
    let start = params.modes.w + counts[STATISTICS_RIBBON_BREAKS];
    let tag = counterTag();
    let per = (n + CHUNK - 1u) / CHUNK;
    let lo = min(local * per, n);
    let hi = min(lo + per, n);
    var broken = 0u;
    for (var a = lo; a < hi; a++) {
        broken += select(0u, 1u, accepted[ribbonWord(ARRAY_KIND, a)] == KIND_BREAK);
    }
    var breaks = exclusiveScan(local, broken);
    for (var a = lo; a < hi; a++) {
        let kind = accepted[ribbonWord(ARRAY_KIND, a)];
        breaks += select(0u, 1u, kind == KIND_BREAK);
        if (kind != KIND_FIXED) {
            accepted[ribbonWord(ARRAY_ID, a)] = (start + breaks) | tag;
        }
    }
    if (local == CHUNK - 1u) {
        counts[STATISTICS_RIBBON_BREAKS] = start - params.modes.w + breaks;
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
