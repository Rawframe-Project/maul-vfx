// The sim kernel's scatter and boxes (spliced into sim.wgsl after
// sim/births.wgsl): scatter, rebox and measure.

// A birth's marks beside its room's: its contacts make an event; it
// crossed the water surface.
const BIRTH_CONTACT: u32 = 4u;
const BIRTH_CROSSED: u32 = 8u;

// The box around a box and a place, by comparisons that pass a NaN over.
fn lowerOf(low: vec3<f32>, p: vec3<f32>) -> vec3<f32> {
    return select(low, p, p < low);
}

fn higherOf(high: vec3<f32>, p: vec3<f32>) -> vec3<f32> {
    return select(high, p, p > high);
}

// The workgroup's box from each invocation's, by a fixed tree; whole in
// boxLow[0] and boxHigh[0], the largest growth in boxHigh[0].w.
fn reduceBox(local: u32, low: vec3<f32>, high: vec3<f32>, growth: f32) {
    boxLow[local] = vec4<f32>(low, 0.0);
    boxHigh[local] = vec4<f32>(high, growth);
    workgroupBarrier();
    for (var half = CHUNK / 2u; half > 0u; half >>= 1u) {
        if (local < half) {
            boxLow[local] = vec4<f32>(lowerOf(boxLow[local].xyz, boxLow[local + half].xyz), 0.0);
            boxHigh[local] = vec4<f32>(higherOf(boxHigh[local].xyz, boxHigh[local + half].xyz),
                                       max(boxHigh[local].w, boxHigh[local + half].w));
        }
        workgroupBarrier();
    }
}

// The empty box: min at the largest binary32, max at the least.
const EMPTY_LOW: f32 = 3.40282347e38;

// Scatter: each kept element to its place in the other buffer, its block's
// first place and those kept before it in the block; a live particle
// moved, a birth spawned; none past the capacity.
@compute @workgroup_size(256, 1, 1)
fn scatter(@builtin(local_invocation_index) local: u32,
           @builtin(workgroup_id) group: vec3<u32>) {
    let c = params.counts.x;
    let live = counts[1];
    let first = group.x * BLOCK + local * ITEMS;
    var keep: array<u32, ITEMS>;
    var kept = 0u;
    for (var k = 0u; k < ITEMS; k++) {
        keep[k] = keepAt(first + k, live);
        kept += keep[k];
    }
    var at = blocks[group.x] + exclusiveScan(local, kept);
    var low = vec3<f32>(EMPTY_LOW);
    var high = vec3<f32>(-EMPTY_LOW);
    var growth = 0.0;
    for (var k = 0u; k < ITEMS; k++) {
        let i = first + k;
        if (keep[k] == 1u && at < c) {
            var p = vec3<f32>(0.0);
            if (i < live) {
                for (var w = 0u; w < params.words.alive; w++) {
                    moved[w * c + at] = source[w * c + i];
                }
                p = readVector(0u, i, params.words.place);
            } else {
                p = spawnAt(i - live, at);
            }
            low = lowerOf(low, p);
            high = higherOf(high, p);
            growth = max(growth, growthOf(at));
        }
        if (keep[k] == 1u && i >= live && params.words.ribbon != NONE) {
            // Where the birth went, for the ribbons passes after.
            accepted[2u * c + i - live] = at;
        }
        at += keep[k];
    }
    reduceBox(local, low, high, growth);
    if (local == 0u) {
        let n = blockCount();
        let box = n + 6u * group.x;
        for (var k = 0u; k < 3u; k++) {
            blocks[box + k] = bitcast<u32>(boxLow[0][k]);
            blocks[box + 3u + k] = bitcast<u32>(boxHigh[0][k]);
        }
        blocks[7u * n + group.x] = bitcast<u32>(boxHigh[0].w);
    }
}

// How much a kept particle grows the bounds: its largest size component
// times the most the size over life scales that axis, 1 for a size it
// does not store, as the CPU path's bounds (the largest over particles of
// each axis's stored size is the largest product, the factors being
// positive).
fn growthOf(at: u32) -> f32 {
    var size = vec3<f32>(1.0);
    if (params.words.size != NONE) {
        size = abs(readVector(1u, at, params.words.size));
    }
    let g = size * params.growth.xyz;
    return max(g.x, max(g.y, g.z));
}

// Rebox, for an emitter whose contacts moved its particles after the
// scatter: each block's box again from the places of the compacted pool,
// a block of it a workgroup (dispatched a block each), the blocks past the
// pool's empty; the growth by size stays the scatter's (contacts change
// no size).
@compute @workgroup_size(256, 1, 1)
fn rebox(@builtin(local_invocation_index) local: u32,
         @builtin(workgroup_id) group: vec3<u32>) {
    let c = params.counts.x;
    let live = counts[0];
    let n = blockCount();
    for (var g = group.x; g < n; g += n) {
        var low = vec3<f32>(EMPTY_LOW);
        var high = vec3<f32>(-EMPTY_LOW);
        for (var k = 0u; k < ITEMS; k++) {
            let i = g * BLOCK + local * ITEMS + k;
            if (i < live) {
                let p = readVector(1u, i, params.words.place);
                low = lowerOf(low, p);
                high = higherOf(high, p);
            }
        }
        reduceBox(local, low, high, 0.0);
        if (local == 0u) {
            let box = n + 6u * g;
            for (var k = 0u; k < 3u; k++) {
                blocks[box + k] = bitcast<u32>(boxLow[0][k]);
                blocks[box + 3u + k] = bitcast<u32>(boxHigh[0][k]);
            }
        }
        workgroupBarrier();
    }
}

// Measure, in one workgroup: the blocks' boxes reduced by a fixed tree to
// the emitter's, in the particles' space, into the statistics record with
// the growth by the largest size, with its margin.
@compute @workgroup_size(256, 1, 1)
fn measure(@builtin(local_invocation_index) local: u32) {
    let n = blockCount();
    var low = vec3<f32>(EMPTY_LOW);
    var high = vec3<f32>(-EMPTY_LOW);
    var growth = 0.0;
    for (var g = local; g < n; g += CHUNK) {
        let box = n + 6u * g;
        growth = max(growth, bitcast<f32>(blocks[7u * n + g]));
        low = lowerOf(low, vec3<f32>(bitcast<f32>(blocks[box]), bitcast<f32>(blocks[box + 1u]),
                                     bitcast<f32>(blocks[box + 2u])));
        high = higherOf(high, vec3<f32>(bitcast<f32>(blocks[box + 3u]),
                                        bitcast<f32>(blocks[box + 4u]),
                                        bitcast<f32>(blocks[box + 5u])));
    }
    reduceBox(local, low, high, growth);
    if (local == 0u) {
        counts[3] = bitcast<u32>(boxHigh[0].w * params.growth.w);
        for (var k = 0u; k < 3u; k++) {
            counts[4u + k] = bitcast<u32>(boxLow[0][k]);
            counts[7u + k] = bitcast<u32>(boxHigh[0][k]);
        }
    }
}
