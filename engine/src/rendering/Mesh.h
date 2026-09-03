#pragma once
//
// Static indexed mesh: VAO + VBO + EBO. Vertex layout since M3 (lit pipeline):
//   location 0 = vec3 position
//   location 1 = vec3 normal
//   location 2 = vec3 color (albedo multiplier; OBJ import emits white)
// Flexible layouts and dynamic buffers land with the mesh/material systems.

#include "rendering/GL.h"

#include <cstdint>

namespace engine {

struct Vertex {
    float x, y, z;    // position
    float nx, ny, nz; // normal (unit length)
    float r, g, b;    // color (albedo multiplier)
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    // Uploads vertex/index data and wires the vertex layout.
    bool create(const Vertex* vertices, std::uint32_t vertexCount,
                const std::uint32_t* indices, std::uint32_t indexCount);

    void bind() const;
    void release();

    bool valid() const { return m_vao != 0; }
    std::uint32_t indexCount() const { return m_indexCount; }

private:
    gl::GLuint m_vao = 0;
    gl::GLuint m_vbo = 0;
    gl::GLuint m_ebo = 0;
    std::uint32_t m_indexCount = 0;
};

} // namespace engine