// A baked curve as the kernels read it (tools/gen_kernels.py splices it
// where a kernel names it), from the includer's grids buffer of baked
// tables: MVFX_CURVE_SAMPLES samples, then the domain's start and the
// factor to a sample position.

const CURVE_SAMPLES: u32 = 256u;

// A baked table's binary32 at a word of the grids buffer.
fn tableAt(at: u32) -> f32 {
    return bitcast<f32>(grids[at]);
}

// A baked curve's value at a time, as the core blends it.
fn curveAt(first: u32, time: f32) -> f32 {
    let last = f32(CURVE_SAMPLES - 1u);
    var position = (time - tableAt(first + CURVE_SAMPLES)) * tableAt(first + CURVE_SAMPLES + 1u);
    position = select(0.0, position, position >= 0.0);
    position = select(position, last, position > last);
    let index = min(u32(position), CURVE_SAMPLES - 2u);
    let fraction = position - f32(index);
    let low = tableAt(first + index);
    return low + (tableAt(first + index + 1u) - low) * fraction;
}
