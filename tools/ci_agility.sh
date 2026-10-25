#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The D3D12 Agility SDK for the windows-rhi-d3d12 cell, through Maul RHI's
# own tools/ci_agility.sh at the version cmake/Rhi.cmake pins: it fetches
# the SDK's NuGet package, checks its digest and unpacks it into
# out/agility, where the preset's MAUL_VFX_D3D12_AGILITY points. maul-vfx
# keeps no copy of the version or the digest; both move with the pin.
#
# usage: tools/ci_agility.sh (from the repository's root)
set -euo pipefail

version=$(sed -n 's/^set(MAUL_VFX_RHI_VERSION \([0-9.]*\))$/\1/p' cmake/Rhi.cmake)
[ -n "$version" ] || { echo "no MAUL_VFX_RHI_VERSION in cmake/Rhi.cmake" >&2; exit 1; }
rm -rf out/maul-rhi-tools
git -c advice.detachedHead=false clone -q --depth 1 --branch "v$version" \
    https://github.com/Rawframe-Project/maul-rhi.git out/maul-rhi-tools
bash out/maul-rhi-tools/tools/ci_agility.sh
