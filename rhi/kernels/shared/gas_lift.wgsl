// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas cell's buoyant factor, without bindings: gas.wgsl takes it, and
// test/kernels/exact_check.wgsl holds it against the core's.

// densityWeight density - heatLift temperature, each scalar held within
// 2^80 first, as the core's mvfxGasLift: the products stay finite, which
// WGSL lets a device assume.
fn gasLift(densityWeight: f32, density: f32, heatLift: f32, temperature: f32) -> f32 {
    let d = select(select(density, -0x1p80f, density < -0x1p80f), 0x1p80f, density > 0x1p80f);
    let t = select(select(temperature, -0x1p80f, temperature < -0x1p80f), 0x1p80f,
                   temperature > 0x1p80f);
    return densityWeight * d - heatLift * t;
}
