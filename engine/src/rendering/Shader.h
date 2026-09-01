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

    bool valid() const { return m_handle != 0; }

private:
    gl::GLuint m_handle = 0;
};

} // namespace engine