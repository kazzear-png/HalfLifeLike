#pragma once

#include "math/Mat4.h"
#include "rendering/GL.h"

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

    bool valid() const { return m_handle != 0; }

    // M3: raw GL program handle for advanced callers (e.g. the Renderer's
    // tonemap pass querying uniform locations). 0 when invalid.
    gl::GLuint nativeHandle() const { return m_handle; }

private:
    gl::GLuint m_handle = 0;
};

} // namespace engine