#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# A throwaway probe for #65 (not to merge): the Agility SDK 1.620.1 into
# out/agility and the redistributable WARP 1.0.22 into out/warp, each
# pinned by its SHA-256.
set -euo pipefail

mkdir -p out
curl -sSfL --retry 3 -o out/agility.zip \
    "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.620.1"
echo "a8a494c3e5f687d9be0a465ec8248da70781ab689d888db8806b600422722be6  out/agility.zip" |
    sha256sum -c -
rm -rf out/agility
unzip -q out/agility.zip -d out/agility
curl -sSfL --retry 3 -o out/warp.zip \
    "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.WARP/1.0.22"
echo "afc058f4d1066edc4231a38f3ab3592b1930f7287d4ac700e6a056e0874bb0be  out/warp.zip" |
    sha256sum -c -
rm -rf out/warp
unzip -q out/warp.zip -d out/warp
ls -l out/warp/build/native/bin/x64/d3d10warp.dll out/agility/build/native/bin/x64/
