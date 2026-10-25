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

# A diagnosis on WARP (to drop): the redistributable WARP 1.0.21, its
# digest checked, unpacked into out/warp for the copies in warp/.
curl -sSfL --retry 3 -o out/warp.zip \
    https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.warp/1.0.21/microsoft.direct3d.warp.1.0.21.nupkg
echo "ea44b77eb30eec14427193e20f40cdb2e4c31ed11ab39cb880538fdf4bac2681  out/warp.zip" | sha256sum -c -
rm -rf out/warp
unzip -q out/warp.zip -d out/warp
