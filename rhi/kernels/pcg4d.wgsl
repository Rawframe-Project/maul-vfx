// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// pcg4d (Jarzynski and Olano, "Hash Functions for GPU Rendering", JCGT
// 2020) as the core computes it (src/random.h): each invocation hashes
// (i, 3 i, 7 i, 0x9E3779B9) for its index i into words[i], the words the
// GPU part's tests hold to the CPU's bit for bit.

@group(0) @binding(0) var<storage, read_write> words: array<vec4<u32>>;

// include: shared/pcg4d_hash.wgsl

@compute @workgroup_size(64, 1, 1)
fn pcg(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    if (i >= arrayLength(&words)) {
        return;
    }
    words[i] = pcg4d(vec4<u32>(i, i * 3u, i * 7u, 0x9E3779B9u));
}
