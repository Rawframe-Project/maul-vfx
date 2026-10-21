// A value read out in a host's format (mvfxFormat): mapped from a read's
// range, then its bits, as the core's (src/layout.c's conversions);
// spliced after shared/formats.wgsl where a read kernel names it.

// A value mapped from a range (low, high) as a normalized format takes
// it.
fn formatMapped(format: u32, value: f32, range: vec2<f32>) -> f32 {
    let low = range.x;
    let high = range.y;
    switch format {
        case FORMAT_UNORM8, FORMAT_UNORM16: {
            return (value - low) / (high - low);
        }
        case FORMAT_SNORM8, FORMAT_SNORM16: {
            return 2.0 * (value - low) / (high - low) - 1.0;
        }
        default: {
            return value;
        }
    }
}

// A value's bits in a format.
fn formatBits(format: u32, value: f32) -> u32 {
    switch format {
        case FORMAT_FLOAT16: {
            return halfOf(value);
        }
        case FORMAT_UNORM8: {
            return unormOf(value, 255.0);
        }
        case FORMAT_SNORM8: {
            return snormOf(value, 127.0) & 0xFFu;
        }
        case FORMAT_UNORM16: {
            return unormOf(value, 65535.0);
        }
        case FORMAT_SNORM16: {
            return snormOf(value, 32767.0) & 0xFFFFu;
        }
        default: {
            return bitcast<u32>(value);
        }
    }
}
