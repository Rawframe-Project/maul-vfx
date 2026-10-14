// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The meshes a context owns, as the library's other parts reach them.

#ifndef MAUL_VFX_SRC_MESH_API_H
#define MAUL_VFX_SRC_MESH_API_H

#include "mesh.h"

// The mesh an id names on the context, or NULL when the id is stale.
mvfxMesh* mvfxContextMesh(const mvfxContext* context, mvfxMeshId mesh);

#endif
