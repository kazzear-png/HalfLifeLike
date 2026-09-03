#pragma once
//
// Minimal Wavefront OBJ importer (M3 asset pipeline, part 1).
//
// Scope (deliberately narrow, zero dependencies):
//   - `v`, `vn`, `vt` (positions, normals; texcoords parsed and ignored --
//     texturing lands with the material milestone)
//   - `f` faces with 3+ corners in the forms: `v`, `v/vt`, `v//vn`,
//     `v/vt/vn`, negative indices, polygons triangulated as a fan
//   - missing normals: flat face normals are generated (per-face duplicated
//     vertices); partially missing normals fall back to the face normal
//   - deduplication: identical (position, normal) corners share one vertex
//
// Out of scope until needed: materials (mtl), curves, multiple objects into
// separate meshes. Everything after the first object tag is merged into one
// mesh -- good enough for props; a scene graph comes with M4.

#include "rendering/Mesh.h"   // engine::Vertex

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

struct LoadObjOptions {
    bool  centerToOrigin = true;   // translate so the bbox center sits at the origin
    float targetRadius   = 0.0f;   // > 0: uniform-scale so the bbox "radius" (max
                                   // distance from center) matches this value.
                                   // 0 keeps the author's original scale.
};

struct LoadObjResult {
    bool ok = false;
    std::string error;             // set when !ok
    std::string warnings;          // non-fatal notes ("no normals -> flat shaded")

    std::vector<Vertex>         vertices;
    std::vector<std::uint32_t>  indices;
    int triangleCount = 0;
    int skippedFaces  = 0;         // degenerate polygons dropped
};

// Parses an OBJ file from disk. Never throws; all failures come back in the
// result. Vertex colors are emitted white -- materials tint them via PBR
// albedo uniforms (see rendering/Lighting.h).
LoadObjResult loadOBJ(const std::string& path, const LoadObjOptions& options = {});

} // namespace engine
