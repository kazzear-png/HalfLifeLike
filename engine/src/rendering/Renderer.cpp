#include "rendering/Renderer.h"

#include "rendering/Mesh.h"

#include <cstdio>

namespace engine {

void Renderer::init() {
    gl::Enable(gl::DepthTest);
    setClearColor(0.10f, 0.12f, 0.15f, 1.0f);
}

void Renderer::setViewport(int x, int y, int width, int height) {
    gl::Viewport(x, y, width, height);
}

void Renderer::setClearColor(float r, float g, float b, float a) {
    gl::ClearColor(r, g, b, a);
}

void Renderer::clear() {
    m_stats.reset();  // clear() starts a new frame
    gl::Clear(gl::ColorBufferBit | gl::DepthBufferBit);
}

void Renderer::drawIndexed(const Mesh& mesh) {
    if (!mesh.valid()) {
        return;
    }

    mesh.bind();
    gl::DrawElements(gl::Triangles, gl::GLsizei(mesh.indexCount()), gl::UnsignedInt, nullptr);
    m_stats.drawCalls += 1;
    m_stats.triangles += mesh.indexCount() / 3;

    // Debug tripwire for M1: surfaces wrong-state issues instead of silently
    // drawing nothing. Replaced by a proper debug layer later.
    const gl::GLenum error = gl::GetError();
    if (error != gl::NoError) {
        std::fprintf(stderr, "[Renderer] GL error 0x%04x raised during drawIndexed\n", error);
    }
}

} // namespace engine