#include "rendering/Mesh.h"

#include <cstddef>  // offsetof

namespace engine {

Mesh::Mesh(Mesh&& other) noexcept
    : m_vao(other.m_vao), m_vbo(other.m_vbo), m_ebo(other.m_ebo),
      m_indexCount(other.m_indexCount) {
    other.m_vao = 0;
    other.m_vbo = 0;
    other.m_ebo = 0;
    other.m_indexCount = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        release();
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        m_ebo = other.m_ebo;
        m_indexCount = other.m_indexCount;
        other.m_vao = 0;
        other.m_vbo = 0;
        other.m_ebo = 0;
        other.m_indexCount = 0;
    }
    return *this;
}

Mesh::~Mesh() {
    release();
}

bool Mesh::create(const Vertex* vertices, std::uint32_t vertexCount,
                  const std::uint32_t* indices, std::uint32_t indexCount) {
    if (vertices == nullptr || indices == nullptr || vertexCount == 0 || indexCount == 0) {
        return false;
    }
    if (m_vao != 0) {
        release();
    }

    gl::GenVertexArrays(1, &m_vao);
    gl::BindVertexArray(m_vao);

    gl::GenBuffers(1, &m_vbo);
    gl::BindBuffer(gl::ArrayBuffer, m_vbo);
    gl::BufferData(gl::ArrayBuffer,
                   gl::GLsizeiptr(vertexCount) * gl::GLsizeiptr(sizeof(Vertex)),
                   vertices, gl::StaticDraw);

    gl::GenBuffers(1, &m_ebo);
    gl::BindBuffer(gl::ElementArrayBuffer, m_ebo);
    gl::BufferData(gl::ElementArrayBuffer,
                   gl::GLsizeiptr(indexCount) * gl::GLsizeiptr(sizeof(std::uint32_t)),
                   indices, gl::StaticDraw);

    // Layout: location 0 = vec3 position, location 1 = vec3 color.
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::Float, gl::GLboolean(0), sizeof(Vertex),
                            reinterpret_cast<const void*>(offsetof(Vertex, x)));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 3, gl::Float, gl::GLboolean(0), sizeof(Vertex),
                            reinterpret_cast<const void*>(offsetof(Vertex, r)));

    // Unbind the VAO first: the element-buffer binding is VAO state and must
    // stay attached to it.
    gl::BindVertexArray(0);
    gl::BindBuffer(gl::ArrayBuffer, 0);

    m_indexCount = indexCount;
    return m_vao != 0;
}

void Mesh::bind() const {
    if (m_vao != 0) {
        gl::BindVertexArray(m_vao);
    }
}

void Mesh::release() {
    if (m_ebo != 0) { gl::DeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_vbo != 0) { gl::DeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao != 0) { gl::DeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_indexCount = 0;
}

} // namespace engine