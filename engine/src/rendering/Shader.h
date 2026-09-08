#pragma once

#include "math/Mat4.h"
#include "rendering/GL.h"

#include <string>
#include <unordered_map>

namespace engine {

// Move-only GLSL program wrapper. GL resources must be destroyed while the
// context is alive (i.e. before Application is destroyed).
class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    // Compiles + links from GLSL source. Invalid shader (handle 0) on failure;
    // errors are printed to stderr.
    static Shader fromSource(const char* vertexSource, const char* fragmentSource);

    void bind() const;
    void setMat4(const char* name, const Mat4& value);
    void setFloat4(const char* name, float x, float y, float z, float w);

    // M3 additions (lighting / material uniforms):
    void setFloat(const char* name, float value);
    void setInt(const char* name, int value);
    void setFloat3(const char* name, float x, float y, float z);
    void setFloat3(const char* name, const Vec3& v);   // overload for Vec3
    // Uploads an array of vec3 uniforms (count elements of 3 floats each).
    void setFloat3Array(const char* name, const float* xyz, int count);
    // Uploads an array of vec4 uniforms (count elements of 4 floats each).
    // M5.1: one driver call for the whole array instead of per-element
    // uploads; query with the name + "[0]" (spec-safe array form).
    void setFloat4Array(const char* name, const float* xyzw, int count);

    bool valid() const { return m_handle != 0; }

    // M3: raw GL program handle for advanced callers (e.g. the Renderer's
    // tonemap pass querying uniform locations). 0 when invalid.
    gl::GLuint nativeHandle() const { return m_handle; }

private:
    gl::GLuint m_handle = 0;

    // M5.1 PERF: uniform-location cache. glGetUniformLocation() is a string
    // -> hash lookup inside the driver; calling it per uniform update cost
    // hundreds of string lookups per frame (the M5.1 benchmark measured this
    // as the CPU-side bottleneck after glGetError was removed). Locations are
    // queried once on first use and reused for the lifetime of the program.
    // "mutable": setXXX() methods stay non-const-friendly and lookups can
    // lazily populate the cache even through const access paths.
    //
    // Invariant: entries are only valid for THIS m_handle (locations are
    // per-program). Move construction/assignment therefore carries the cache
    // with the handle; a moved-from shader drops both. Cached -1 results are
    // intentional: optimized-out uniforms skip the driver query too.
    mutable std::unordered_map<std::string, gl::GLint> m_uniformCache;

    gl::GLint getUniformLocation(const char* name) const;
};

} // namespace engine