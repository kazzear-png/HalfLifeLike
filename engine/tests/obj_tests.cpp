//
// M3 asset-pipeline verification: exercise engine::loadOBJ against small
// fixture files written into a temp directory at test start.
//
// Covers: basic triangles, quads (fan triangulation), `v//vn` and `v/vt/vn`
// corner forms, negative indices, missing normals (flat fallback), degenerate
// face skipping, invalid-path failure, and normalize-to-radius options.
//
// Run: ctest --test-dir build --output-on-failure

#include "assets/OBJ.h"
#include "math/Vec3.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>  // _getcwd
#include <io.h>      // _isatty: detect a double-clicked console run
#else
#include <unistd.h>  // getcwd
#endif

namespace {

int g_checks   = 0;
int g_failures = 0;

// Windows convenience: when the exe is double-clicked, the console window
// closes the instant main() returns and the results are unreadable. Hold it
// open ONLY when stdin is a real console -- ctest/CI pipes keep this a no-op.
void pauseIfInteractive() {
#ifdef _WIN32
    if (_isatty(_fileno(stdin))) {
        std::printf("\nPress Enter to close...");
        std::fflush(stdout);
        std::fgetc(stdin);
    }
#endif
}

void expectTrue(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: %s\n", what);
    }
}

void expectNear(float a, float b, float eps, const char* what) {
    expectTrue(std::fabs(a - b) <= eps, what);
}

void writeFile(const std::string& path, const char* contents) {
    std::ofstream out(path);
    if (!out) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: cannot write fixture '%s'\n", path.c_str());
        return;
    }
    out << contents;
}

std::string tempDir() {
    char buf[1024];
#ifdef _WIN32
    if (_getcwd(buf, sizeof(buf)) == nullptr) return std::string(".");
#else
    if (getcwd(buf, sizeof(buf)) == nullptr) return std::string(".");
#endif
    return std::string(buf) + "/obj_test_fixtures";
}

// ---------------------------------------------------------------------------
// Fixture OBJs
// ---------------------------------------------------------------------------

// One triangle with explicit normals in v//vn form.
const char* kTriangle = R"OBJ(# comment line
v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 0.0 1.0 0.0
vn 0.0 0.0 1.0
f 1//1 2//1 3//1
)OBJ";

// One quad (becomes 2 triangles) with vt present (ignored) and v/vt/vn corners.
const char* kQuadWithVt = R"OBJ(v -1.0 -1.0 0.0
v  1.0 -1.0 0.0
v  1.0  1.0 0.0
v -1.0  1.0 0.0
vn 0.0 0.0 1.0
vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0
f 1/1/1 2/2/1 3/3/1
f 1/1/1 3/3/1 4/4/1
)OBJ";

// Pentagon face -> fan of 3 triangles; negative indices wrap to the end of
// the list. No normals at all -> flat fallback path.
const char* kPentagonNoNormals = R"OBJ(v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 1.5 1.0 0.0
v 0.5 1.6 0.0
v -0.5 1.0 0.0
f -5 -4 -3 -2 -1
)OBJ";

// A good triangle followed by a degenerate one (repeated vertex) which must
// be skipped without failing the load.
const char* kWithDegenerate = R"OBJ(v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 0.0 1.0 0.0
vn 0.0 0.0 1.0
f 1//1 2//1 3//1
f 1//1 1//1 2//1
)OBJ";

// M5.5: CONCAVE polygon (reflex vertex at "v 1 1 0" -- the notch). The old
// corner-0 fan emitted a triangle extending into the notch region (or relied
// on the middle fan triangle being exactly degenerate to accidentally tile).
// Ear clipping must decompose it into exactly 3 triangles that tile the
// polygon and stay inside it.
const char* kConcavePentagon = R"OBJ(v 0 0 0
v 2 0 0
v 2 2 0
v 1 1 0
v 0 2 0
f 1 2 3 4 5
)OBJ";

// M5.5: malformed corner tokens. atoll() silently read "1.5" as corner 1,
// "2x" as corner 2, and "x" as corner 0; strict parsing must drop these
// faces loudly (skippedFaces), leaving only the one valid face.
const char* kGarbageCorners = R"OBJ(v 0 0 0
v 1 0 0
v 0 1 0
vn 0 0 1
f 1//1 2//1 3//1
f 1.5//1 2//1 3//1
f x//1 2//1 3//1
f 0//1 2//1 3//1
f 1//1 2x//1 3//1
f 1 2
)OBJ";

} // namespace

int main() {
    const std::string dir = tempDir();
    // Portable directory creation. The previous system("mkdir -p ...") was
    // Unix-only: cmd.exe rejects '-p' ("The syntax of the command is
    // incorrect"), fixtures were never written, loads failed, and indexed
    // access into the empty result tripped MSVC's vector-bounds assert.
    std::error_code fsErr;
    std::filesystem::create_directories(dir, fsErr);
    if (fsErr) {
        std::fprintf(stderr, "[obj] cannot create fixture dir '%s': %s\n",
                     dir.c_str(), fsErr.message().c_str());
        pauseIfInteractive();
        return 1;
    }

    const std::string triPath  = dir + "/triangle.obj";
    const std::string quadPath = dir + "/quad.obj";
    const std::string pentPath = dir + "/pentagon.obj";
    const std::string degenPath = dir + "/degenerate.obj";
    const std::string concavePath = dir + "/concave.obj";
    const std::string garbagePath = dir + "/garbage_corners.obj";

    writeFile(triPath, kTriangle);
    writeFile(quadPath, kQuadWithVt);
    writeFile(pentPath, kPentagonNoNormals);
    writeFile(degenPath, kWithDegenerate);
    writeFile(concavePath, kConcavePentagon);
    writeFile(garbagePath, kGarbageCorners);

    std::printf("[obj] basic triangle (v//vn)\n");
    {
        engine::LoadObjResult r = engine::loadOBJ(triPath);
        expectTrue(r.ok, "triangle load succeeds");
        expectTrue(r.vertices.size() == 3, "triangle has 3 vertices");
        expectTrue(r.indices.size() == 3, "triangle has 3 indices");
        expectTrue(r.triangleCount == 1, "triangle count == 1");
        // Indexed inspection is guarded: a failed load must report FAILs, not
        // crash on an out-of-range access (MSVC debug assertion).
        if (!r.vertices.empty()) {
            // White vertex color for imported geometry (PBR albedo tints instead).
            expectNear(r.vertices[0].r, 1.0f, 1e-6f, "OBJ vertex color is white");
            // Normal survived the trip.
            expectNear(r.vertices[0].nz, 1.0f, 1e-5f, "triangle normal is +Z");
        } else {
            expectTrue(false, "triangle vertices present for inspection");
        }
        expectTrue(r.warnings.empty(), "no warnings expected");
    }

    std::printf("[obj] quad with texcoords (v/vt/vn)\n");
    {
        engine::LoadObjResult r = engine::loadOBJ(quadPath);
        expectTrue(r.ok, "quad load succeeds");
        expectTrue(r.triangleCount == 2, "quad triangulated into 2 triangles");
        expectTrue(r.vertices.size() == 4, "quad has 4 unique vertices");
        expectTrue(r.warnings.find("texcoords ignored") != std::string::npos,
                   "texcoords warning emitted");
    }

    std::printf("[obj] pentagon, negative indices, no normals\n");
    {
        engine::LoadObjResult r = engine::loadOBJ(pentPath);
        expectTrue(r.ok, "pentagon load succeeds");
        expectTrue(r.triangleCount == 3, "pentagon fans into 3 triangles");
        // Flat fallback: the loader warns and every vertex carries the face
        // normal; +Z here because the polygon is in the XY plane CCW.
        expectTrue(r.warnings.find("flat shading") != std::string::npos,
                   "flat-shading warning emitted");
        bool allUnitZ = true;
        for (const engine::Vertex& v : r.vertices) {
            if (std::fabs(v.nz - 1.0f) > 1e-4f) allUnitZ = false;
        }
        expectTrue(allUnitZ, "flat normals point +Z for XY-plane polygon");
        expectTrue(!r.vertices.empty(), "pentagon produced vertices");
    }

    std::printf("[obj] degenerate face skipped\n");
    {
        engine::LoadObjResult r = engine::loadOBJ(degenPath);
        expectTrue(r.ok, "degenerate-face file still loads");
        expectTrue(r.triangleCount == 1, "only the valid triangle is kept");
        expectTrue(r.skippedFaces == 1, "degenerate face counted as skipped");
    }

    std::printf("[obj] concave polygon ear-clipped (M5.5)\n");
    {
        // centerToOrigin = false: the geometric assertions below use the
        // AUTHORED absolute coordinates (loadOBJ defaults would translate
        // the bbox center to the origin).
        engine::LoadObjOptions opts;
        opts.centerToOrigin = false;
        opts.targetRadius   = 0.0f;
        engine::LoadObjResult r = engine::loadOBJ(concavePath, opts);
        expectTrue(r.ok, "concave load succeeds");
        expectTrue(r.triangleCount == 3, "concave pentagon -> exactly 3 triangles");

        // The authored polygon (XY plane, CCW, area 3.0): square [0,2]^2 with
        // a triangular notch above the lines (2,2)-(1,1) and (1,1)-(0,2).
        const engine::Vec3 poly[5] = {
            engine::Vec3(0, 0, 0), engine::Vec3(2, 0, 0), engine::Vec3(2, 2, 0),
            engine::Vec3(1, 1, 0), engine::Vec3(0, 2, 0)
        };

        // Ray-cast point-in-polygon (xy components; polygon is planar).
        auto insidePoly = [](float px, float py) -> bool {
            const float xs[5] = { 0.0f, 2.0f, 2.0f, 1.0f, 0.0f };
            const float ys[5] = { 0.0f, 0.0f, 2.0f, 1.0f, 2.0f };
            bool inside = false;
            for (int i = 0, j = 4; i < 5; j = i++) {
                if ((ys[i] > py) != (ys[j] > py) &&
                    px < (xs[j] - xs[i]) * (py - ys[i]) / (ys[j] - ys[i]) + xs[i]) {
                    inside = !inside;
                }
            }
            return inside;
        };

        float totalArea = 0.0f;
        bool allCentroidsInside = true;
        bool allVerticesAreCorners = true;
        for (std::size_t t = 0; t + 2 < r.indices.size(); t += 3) {
            const engine::Vertex& va = r.vertices[r.indices[t + 0]];
            const engine::Vertex& vb = r.vertices[r.indices[t + 1]];
            const engine::Vertex& vc = r.vertices[r.indices[t + 2]];
            const engine::Vec3 a(va.x, va.y, va.z);
            const engine::Vec3 b(vb.x, vb.y, vb.z);
            const engine::Vec3 c(vc.x, vc.y, vc.z);

            // Area accumulation: tiling must sum to the polygon area exactly
            // (outside-extension or overlap would both move the sum).
            const engine::Vec3 ab = b - a, ac = c - a;
            totalArea += 0.5f * engine::length(engine::cross(ab, ac));

            // Every vertex must be one of the 5 authored corners.
            for (const engine::Vec3* v : { &a, &b, &c }) {
                bool isCorner = false;
                for (const engine::Vec3& p : poly) {
                    if (std::fabs(v->x - p.x) < 1e-5f &&
                        std::fabs(v->y - p.y) < 1e-5f &&
                        std::fabs(v->z - p.z) < 1e-5f) {
                        isCorner = true;
                    }
                }
                if (!isCorner) allVerticesAreCorners = false;
            }

            // Centroid strictly inside the polygon.
            const float cx = (a.x + b.x + c.x) / 3.0f;
            const float cy = (a.y + b.y + c.y) / 3.0f;
            if (!insidePoly(cx, cy)) {
                allCentroidsInside = false;
            }
        }
        expectNear(totalArea, 3.0f, 1e-4f, "triangles tile the polygon exactly (area sum)");
        expectTrue(allVerticesAreCorners, "no vertex outside the authored corner set");
        expectTrue(allCentroidsInside, "no triangle centroid outside the polygon");
    }

    std::printf("[obj] malformed corner tokens rejected (M5.5)\n");
    {
        engine::LoadObjResult r = engine::loadOBJ(garbagePath);
        expectTrue(r.ok, "garbage-corner file still loads");
        expectTrue(r.triangleCount == 1, "only the valid face loads");
        // "1.5//1", "x//1", "0//1", "2x//1" faces + the 2-corner face.
        expectTrue(r.skippedFaces == 5, "all 5 malformed faces counted as skipped");
        expectTrue(r.warnings.find("skipped 5") != std::string::npos,
                   "skip warning emitted for malformed corners");
        // Nothing from garbage faces leaked into the vertex buffer.
        expectTrue(r.vertices.size() == 3, "only the valid triangle's 3 vertices emitted");
    }

    std::printf("[obj] missing file + normalization option\n");
    {
        engine::LoadObjResult r = engine::loadOBJ(dir + "/does_not_exist.obj");
        expectTrue(!r.ok, "missing file fails cleanly");
        expectTrue(!r.error.empty(), "missing file reports an error string");
        expectTrue(r.vertices.empty(), "missing file yields no geometry");

        // Scale a triangle (unit-ish) up to radius 5 and verify the bounds.
        engine::LoadObjOptions opts;
        opts.centerToOrigin = true;
        opts.targetRadius   = 5.0f;
        engine::LoadObjResult scaled = engine::loadOBJ(triPath, opts);
        expectTrue(scaled.ok, "normalized load succeeds");
        float maxDist = 0.0f;
        for (const engine::Vertex& v : scaled.vertices) {
            const engine::Vec3 p(v.x, v.y, v.z);
            maxDist = std::fmax(maxDist, engine::length(p));
        }
        expectNear(maxDist, 5.0f, 5e-3f, "targetRadius respected (max distance from center)");
    }

    std::printf("[obj] %d checks, %d failure(s)\n", g_checks, g_failures);
    pauseIfInteractive();
    return g_failures == 0 ? 0 : 1;
}
