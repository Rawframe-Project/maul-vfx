// The sim kernel's part of a particle liquid (spliced into sim.wgsl), as
// the core's mvfxStepLiquid: each substep's move of every particle not
// ended before the moves, its predicted place kept in the other buffer
// (moveSpan's), and each iteration's slide of each particle's way to its
// corrected place (liquid.wgsl's, in the other buffer's velocity words)
// along the colliders. liquid.wgsl's passes do the rest.

fn liquidEnded(i: u32) -> bool {
    return source[params.words.alive * params.counts.x + i] == LIVE_ENDED;
}

// The move of substep root.substep: each particle over the substep's
// span, as mvfxIntegrateRange moves it.
@compute @workgroup_size(64, 1, 1)
fn liquidMove(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = counts[0];
    let ticks = params.counts.z;
    let substeps = params.liquid.y;
    let since = ticks * root.substep / substeps;
    let span = ticks * (root.substep + 1u) / substeps - since;
    for (var i = id.x; i < live; i += params.modes.z * 64u) {
        if (!liquidEnded(i)) {
            moveSpan(i, since, span);
        }
    }
}

// The slide: each particle's way from its place to its corrected place met
// with the colliders, sliding (no restitution, no friction, never
// stopping), as the core's Slide, into its place.
@compute @workgroup_size(64, 1, 1)
fn liquidSlide(@builtin(global_invocation_id) id: vec3<u32>) {
    let live = counts[0];
    let w = params.words;
    for (var i = id.x; i < live; i += params.modes.z * 64u) {
        if (liquidEnded(i)) {
            continue;
        }
        let start = readVector(0u, i, w.place);
        var place = readVector(1u, i, w.velocity);
        if (params.collisionHead.z != 0u) {
            var way = place - start;
            var first = Hit(0.0, vec3<f32>(0.0), vec3<f32>(0.0));
            _ = collideWith(start, &place, &way, &first, vec4<f32>(0.0));
        }
        setVectorIn(0u, w.place, i, place);
    }
}
