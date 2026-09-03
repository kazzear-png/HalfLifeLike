#include "rendering/Shader.h"

#include <cstdio>

namespace engine {

namespace {

gl::GLuint compileStage(gl::GLenum stage, const char* source, const char* stageName) {
    const gl::GLuint shader = gl::CreateShader(stage);
    if (shader == 0) {
        std::fprintf(stderr, "[Shader] glCreateShader failed for %s\n", stageName);
        return 0;
    }

    gl::ShaderSource(shader, 1, &source, nullptr);
    gl::CompileShader(shader);

    gl::GLint status = 0;
    gl::GetShaderiv(shader, gl::CompileStatus, &status);
    if (status == 0) {
        gl::GLint logLength = 0;
        gl::GetShaderiv(shader, gl::InfoLogLength, &logLength);
        if (logLength > 0) {  // InfoLogLength includes the NUL terminator
            char* log = new char[logLength];
            gl::GetShaderInfoLog(shader, logLength, nullptr, log);
            std::fprintf(stderr, "[Shader] %s compile error:\n%s\n", stageName, log);
            delete[] log;
        } else {
            std::fprintf(stderr, "[Shader] %s compile error (no log available)\n", stageName);
        }
        gl::DeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

Shader::Shader(Shader&& other) noexcept : m_handle(other.m_handle) {
    other.m_handle = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_handle != 0) gl::DeleteProgram(m_handle);
        m_handle = other.m_handle;
        other.m_handle = 0;
    }
    return *this;
}

Shader::~Shader() {
    if (m_handle != 0) gl::DeleteProgram(m_handle);
}

Shader Shader::fromSource(const char* vertexSource, const char* fragmentSource) {
    Shader result;

    const gl::GLuint vs = compileStage(gl::VertexShader, vertexSource, "vertex shader");
    if (vs == 0) return result;

    const gl::GLuint fs = compileStage(gl::FragmentShader, fragmentSource, "fragment shader");
    if (fs == 0) {
        gl::DeleteShader(vs);
        return result;
    }

    const gl::GLuint program = gl::CreateProgram();
    gl::AttachShader(program, vs);
    gl::AttachShader(program, fs);
    gl::LinkProgram(program);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);

    gl::GLint status = 0;
    gl::GetProgramiv(program, gl::LinkStatus, &status);
    if (status == 0) {
        gl::GLint logLength = 0;
        gl::GetProgramiv(program, gl::InfoLogLength, &logLength);
        if (logLength > 0) {
            char* log = new char[logLength];
            gl::GetProgramInfoLog(program, logLength, nullptr, log);
            std::fprintf(stderr, "[Shader] link error:\n%s\n", log);
            delete[] log;
        } else {
            std::fprintf(stderr, "[Shader] link error (no log available)\n");
        }
        gl::DeleteProgram(program);
        return result;
    }

    result.m_handle = program;
    return result;
}

void Shader::bind() const {
    if (m_handle != 0) {
        gl::UseProgram(m_handle);
    }
}

void Shader::setMat4(const char* name, const Mat4& value) {
    const gl::GLint location = gl::GetUniformLocation(m_handle, name);
    if (location >= 0) {
        // Column-major storage matches Mat4's layout; no transpose needed.
        gl::UniformMatrix4fv(location, 1, 0, value.data());
    }
}

void Shader::setFloat4(const char* name, float x, float y, float z, float w) {
    const gl::GLint location = gl::GetUniformLocation(m_handle, name);
    if (location >= 0) {
        gl::Uniform4f(location, x, y, z, w);
    }
}

void Shader::setFloat(const char* name, float value) {
    const gl::GLint location = gl::GetUniformLocation(m_handle, name);
    if (location >= 0) {
        gl::Uniform1f(location, value);
    }
}

void Shader::setInt(const char* name, int value) {
    const gl::GLint location = gl::GetUniformLocation(m_handle, name);
    if (location >= 0) {
        gl::Uniform1i(location, value);
    }
}

void Shader::setFloat3(const char* name, float x, float y, float z) {
    const gl::GLint location = gl::GetUniformLocation(m_handle, name);
    if (location >= 0) {
        gl::Uniform3f(location, x, y, z);
    }
}

void Shader::setFloat3(const char* name, const Vec3& v) {
    setFloat3(name, v.x, v.y, v.z);
}

void Shader::setFloat3Array(const char* name, const float* xyz, int count) {
    if (xyz == nullptr || count <= 0) {
        return;
    }
    const gl::GLint location = gl::GetUniformLocation(m_handle, name);
    if (location >= 0) {
        gl::Uniform3fv(location, gl::GLsizei(count), xyz);
    }
}

} // namespace engine