// The formats a host reads values in (mvfxFormat), and a value's bits in
// each, as the core's (src/layout.c's conversions): spliced where a kernel
// names it.

const FORMAT_FLOAT32: u32 = 0u;
const FORMAT_FLOAT16: u32 = 1u;
const FORMAT_UNORM8: u32 = 2u;
const FORMAT_SNORM8: u32 = 3u;
const FORMAT_UNORM16: u32 = 4u;
const FORMAT_SNORM16: u32 = 5u;

// A binary32's half float, as the core's integer conversion.
fn halfOf(value: f32) -> u32 {
    let bits = bitcast<u32>(value);
    let sign = (bits >> 16u) & 0x8000u;
    let magnitude = bits & 0x7FFFFFFFu;
    if (magnitude >= 0x7F800000u) {
        return sign | select(0x7C00u, 0x7E00u, magnitude > 0x7F800000u);
    }
    if (magnitude >= 0x477FF000u) {
        return sign | 0x7C00u;
    }
    if (magnitude >= 0x38800000u) {
        let rebased = magnitude - 0x38000000u;
        let odd = (rebased >> 13u) & 1u;
        return sign | ((rebased + 0x0FFFu + odd) >> 13u);
    }
    if (magnitude < 0x33000000u) {
        return sign;
    }
    let exponent = magnitude >> 23u;
    let mantissa = (magnitude & 0x007FFFFFu) | 0x00800000u;
    let shift = 126u - exponent;
    let kept = mantissa >> shift;
    let rest = mantissa & ((1u << shift) - 1u);
    let half = 1u << (shift - 1u);
    let up = rest > half || (rest == half && (kept & 1u) == 1u);
    return sign | (kept + select(0u, 1u, up));
}

// A value clamped to 0 to 1 (NaN to 0) in steps of 1 / top, rounded half
// up, as the core's.
fn unormOf(value: f32, top: f32) -> u32 {
    let x = select(0.0, min(value, 1.0), value >= 0.0);
    return u32(x * top + 0.5);
}

// A value clamped to -1 to 1 (NaN to 0) in steps of 1 / top, rounded half
// away from 0, as a two's complement word.
fn snormOf(value: f32, top: f32) -> u32 {
    var x = 0.0;
    if (value >= -1.0) {
        x = min(value, 1.0);
    } else if (value < -1.0) {
        x = -1.0;
    }
    let scaled = x * top;
    let magnitude = i32(abs(scaled) + 0.5);
    return bitcast<u32>(select(magnitude, -magnitude, scaled < 0.0));
}

fn formatBytes(format: u32) -> u32 {
    switch format {
        case FORMAT_FLOAT16, FORMAT_UNORM16, FORMAT_SNORM16: {
            return 2u;
        }
        case FORMAT_UNORM8, FORMAT_SNORM8: {
            return 1u;
        }
        default: {
            return 4u;
        }
    }
}
