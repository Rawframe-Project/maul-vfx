#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The checks, in tiers. Each tier includes the one before it.
#
#   tools/check.sh changed    the files changed against origin/main: the
#                             source rules, the generated files' checks,
#                             formatting, the documentation checks,
#                             clang-tidy on the changed library sources,
#                             and the linux-clang-20-asan preset's tests.
#                             Under two minutes; run it before every push.
#   tools/check.sh pr         what the pull request check runs on Linux:
#                             every check, clang-tidy, both parts built
#                             unoptimized with no stack frame over 64 KB,
#                             the linux-clang-20-asan, linux-gcc-14 and
#                             linux-rhi presets (the GPU sample on lavapipe
#                             among them), and a short fuzzing pass.
#   tools/check.sh full       also ThreadSanitizer, the shared library,
#                             Clang's Release build, clang-tidy on the
#                             kernel table and longer fuzzing.
#   tools/check.sh extended   also the GPU part's suites on lavapipe, under
#                             the validation layer and under the
#                             sanitizers, as the nightly runs them; up to
#                             two hours.
#
# The builds are the CI cells' CMake presets (CMakePresets.json), in
# out/<preset>; the other trees go to out/check-*. Needs gcc-14, clang-20,
# clang-tidy-20, ninja, the clang-format version CI pins, and from the pr
# tier on lavapipe and the Vulkan validation layers, for linux-rhi's GPU
# sample and extended's suites: the organization's CI image has them all.
# The web, macOS and Windows cells run in CI only, rhi_kernels on D3D12
# among them. The org documentation check runs when the
# organization's .github checkout is at ORG_DEFAULTS (default:
# ../../.github from the repository).
set -euo pipefail

tier=${1:-changed}
case "$tier" in
changed | pr | full | extended) ;;
*)
    echo "usage: tools/check.sh [changed|pr|full|extended]" >&2
    exit 2
    ;;
esac

cd "$(dirname "$0")/.."
root=$PWD
out=$root/out
mkdir -p "$out"
org=${ORG_DEFAULTS:-$root/../../.github}
jobs=${JOBS:-8}

step() { printf '\n== %s\n' "$*"; }

# A tree outside the presets (clang-tidy, fuzzing).
configure() { # name, then cmake arguments
    local name=check-$1
    shift
    cmake -S "$root" -B "$out/$name" -G Ninja -DMAUL_VFX_WERROR=ON "$@" >"$out/$name.log"
}

build() { cmake --build "$out/check-$1" -j "$jobs" "${@:2}" >>"$out/check-$1.log"; }

# A CI cell: its configure, build and test presets. More ctest arguments
# follow the preset's name.
preset() {
    local name=$1
    shift
    step "preset $name"
    cmake --preset "$name" >"$out/$name.log"
    python3 tools/check_test_hygiene.py --ctest "out/$name"
    cmake --build --preset "$name" -j "$jobs" >>"$out/$name.log"
    ctest --preset "$name" "$@"
}

source_checks() {
    step "source, module, length, documentation and snippet rules"
    python3 tools/check_source.py
    python3 tools/check_modules.py
    python3 tools/check_float_env.py
    python3 tools/check_lengths.py
    python3 tools/check_docs.py
    python3 tools/check_guide.py
    python3 tools/check_test_hygiene.py --self-test
    python3 tools/check_test_hygiene.py
    step "generated files"
    python3 tools/gen_file.py --check
    python3 tools/gen_marching_cubes.py --check
    python3 tools/gen_kernels.py --check
    python3 tools/effect_json.py --check test/fixtures
    if [ -f "$org/scripts/docs_check.py" ]; then
        python3 "$org/scripts/docs_check.py" --strict
    else
        echo "skipped the organization's documentation check: no checkout at $org"
    fi
}

# clang-tidy on library sources, as CI's: in a Release tree, where an
# assertion's condition is not evaluated, and in a Debug one, where the
# analyzer reads it. Each hides findings the other shows.
tidy() { # files
    local build
    for build in Release Debug; do
        configure "tidy-$build" -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=$build \
            -DMAUL_VFX_RHI=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        printf '%s\n' "$@" |
            xargs -P "$jobs" -n 4 clang-tidy-20 -p "$out/check-tidy-$build" --quiet \
                --warnings-as-errors='*' 2>/dev/null
    done
}

# The sources clang-format reads: every C file but the generated tables
# and kernel containers, which tools write and no one edits.
all_c_files() {
    git ls-files 'include/*.h' 'src/*.[ch]' 'rhi/*.[ch]' 'test/*.[ch]' 'tools/*.[ch]' \
        'bench/*.[ch]' 'samples/*.[ch]' |
        grep -vE '_container\.h$|^src/(file_fields|marching_table)\.c$'
}

if [ "$tier" = changed ]; then
    base=$(git merge-base HEAD origin/main 2>/dev/null || echo HEAD)
    mapfile -t changed < <({
        git diff --name-only "$base"
        git ls-files --others --exclude-standard
    } | sort -u)
    if [ ${#changed[@]} -eq 0 ]; then
        echo "nothing changed against origin/main"
        exit 0
    fi
    source_checks
    mapfile -t c_files < <(printf '%s\n' "${changed[@]}" | grep -E '\.[ch]$' |
        grep -vE '_container\.h$|^src/(file_fields|marching_table)\.c$' || true)
    present=()
    for f in "${c_files[@]+"${c_files[@]}"}"; do [ -f "$f" ] && present+=("$f"); done
    if [ ${#present[@]} -gt 0 ]; then
        step "clang-format on ${#present[@]} changed files"
        clang-format --dry-run -Werror "${present[@]}"
    fi
    # The library sources changed, as the pr tier reads them (the kernel
    # table left to the full tier).
    tidy_files=()
    for f in "${changed[@]}"; do
        if [[ -f "$f" && "$f" =~ ^(src|rhi/src)/[^/]+\.c$ && "$f" != rhi/src/kernels.c ]]; then
            tidy_files+=("$f")
        fi
    done
    if [ ${#tidy_files[@]} -gt 0 ]; then
        step "clang-tidy on ${#tidy_files[@]} changed library files"
        tidy "${tidy_files[@]}"
    fi
    if ! printf '%s\n' "${changed[@]}" | grep -qvE '\.md$|^docs/|^LICENSE$|^\.github/'; then
        echo "only documentation changed: no build"
        exit 0
    fi
    preset linux-clang-20-asan
    exit 0
fi

source_checks

step "clang-format on every file"
files=$(all_c_files)
[ -n "$files" ] || {
    echo "git listed no sources"
    exit 1
}
echo "$files" | xargs -P "$jobs" -n 32 clang-format --dry-run -Werror

step "clang-tidy"
# rhi/src/kernels.c includes 70 MB of kernel containers and takes clang-tidy
# over three minutes, so only the full tier reads it.
tidy_files=(src/*.c rhi/src/*.c)
if [ "$tier" = pr ]; then
    mapfile -t tidy_files < <(printf '%s\n' "${tidy_files[@]}" | grep -vx rhi/src/kernels.c)
fi
tidy "${tidy_files[@]}"

step "no stack frame over 64 KB, unoptimized"
# Optimized builds fold the temporaries that an unoptimized one keeps on
# the stack, so only this build shows a frame past the limit.
configure frames -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS_DEBUG=-O0 \
    -DMAUL_VFX_RHI=ON -DMAUL_VFX_BUILD_TESTS=OFF -DMAUL_VFX_BUILD_BENCH=OFF \
    -DMAUL_VFX_BUILD_SAMPLES=OFF
build frames

preset linux-clang-20-asan
preset linux-gcc-14
preset linux-rhi

fuzz_seconds=10
[ "$tier" = pr ] || fuzz_seconds=60
step "fuzzing, $fuzz_seconds seconds per target"
configure fuzz -DCMAKE_C_COMPILER=clang-20 -DMAUL_VFX_FUZZ=ON -DMAUL_VFX_BUILD_BENCH=OFF \
    -DMAUL_VFX_BUILD_SAMPLES=OFF
build fuzz --target fuzz_file fuzz_step test_golden
for target in "$out"/check-fuzz/fuzz_*; do
    name=$(basename "$target")
    corpus=$out/corpus/$name
    mkdir -p "$corpus"
    tar -xzf "test/corpus/$name.tar.gz" -C "$corpus"
    [ "$name" = fuzz_file ] && "$out/check-fuzz/test_golden" --seeds "$corpus" >/dev/null
    "$target" -max_total_time=$fuzz_seconds "$corpus" >"$out/$name.log" 2>&1 ||
        {
            tail -30 "$out/$name.log"
            exit 1
        }
    echo "$name: no finding"
done

[ "$tier" = pr ] && exit 0

for name in linux-clang-20-tsan linux-shared linux-clang-20; do
    preset "$name"
done

[ "$tier" = extended ] || exit 0

preset linux-rhi-lavapipe
preset linux-rhi-lavapipe-asan

step "all checks passed"
