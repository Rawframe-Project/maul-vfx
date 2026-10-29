// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The speed hold, without bindings, after shared/finite.wgsl: the sim's
// holdSpeed takes it with the emitter's bound, test/kernels/exact_check.wgsl
// with any.

// A velocity's speed held within fastest, keeping its heading, as the
// core's mvfxHoldSpeed: over its largest component, then scaled to the
// bound past it. A NaN or infinite component leaves it as it is, for the
// kill rules, tested first and on its bits: WGSL lets an implementation
// assume neither occurs (lavapipe folds v != v to false), and its max of
// a NaN is unspecified.
fn heldSpeed(v: vec3<f32>, fastest: f32) -> vec3<f32> {
    if (!all(areFinite(v))) {
        return v;
    }
    let a = abs(v);
    let most = max(a.x, max(a.y, a.z));
    if (!(most > 0.0)) {
        return v;
    }
    let u = v / most;
    let length = sqrt(dot(u, u));
    if (!(most * length > fastest)) {
        return v;
    }
    return u * (fastest / length);
}
