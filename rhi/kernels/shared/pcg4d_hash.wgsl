// pcg4d (Jarzynski and Olano, "Hash Functions for GPU Rendering", JCGT
// 2020) as the core computes it (src/random.h), for the kernels that
// splice it in.

fn pcg4dMix(v: vec4<u32>) -> vec4<u32> {
    var r = v;
    r.x += r.y * r.w;
    r.y += r.z * r.x;
    r.z += r.x * r.y;
    r.w += r.y * r.z;
    return r;
}

fn pcg4d(start: vec4<u32>) -> vec4<u32> {
    var v = start * 1664525u + 1013904223u;
    v = pcg4dMix(v);
    v ^= v >> vec4<u32>(16u);
    return pcg4dMix(v);
}
