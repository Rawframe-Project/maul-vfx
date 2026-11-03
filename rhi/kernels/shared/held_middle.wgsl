// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A body's held middle moment, without bindings, spliced where a kernel
// names it (sim/rigid_flow.wgsl).

// The middle of three principal moments, largest first, with the two
// largest held equal within 2^-21 of the largest, their gap ramped from 0
// there to its own at 2^-20, as the core's mvfxHeldMiddle: its operations
// in its order, every one exact but the last, so both paths take the same
// middle moment from the same moments, to the bit.
fn heldMiddle(m: vec3<f32>) -> f32 {
    let gap = m.x - m.y;
    let band = 4.76837158203125e-7 * m.x;
    if (gap <= band) {
        return m.x;
    }
    if (gap <= 2.0 * band) {
        return m.x - 2.0 * (gap - band);
    }
    return m.y;
}
