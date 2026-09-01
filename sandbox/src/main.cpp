//
// Milestone 1: render a quad through a real 3D pipeline.
// SPACE = pause/resume spin. ESC or close button = quit.

#include "core/Application.h"
#include "math/Mat4.h"
#include "rendering/Mesh.h"
#include "rendering/Shader.h"

#include <cstdint>
#include <cstdio>

namespace {

const char* kVertexShader = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aColor;

uniform mat4 uMVP;

out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
)GLSL";

const char* kFragmentShader = R"GLSL(
#version 330 core
in vec3 vColor;
out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor, 1.0);
}
)GLSL";

} // namespace

int main() {
    engine::WindowDesc desc;
    desc.title  = "Sandbox - M1 Hello Quad";
    desc.width  = 1280;
    desc.height = 720;
    desc.vsync  = true;

    engine::Application app(desc);
    if (!app.valid()) {
        std::fprintf(stderr, "Engine initialization failed; see messages above.\n");
        return 1;
    }

    // Unit quad in the XY plane: 4 vertices, 2 triangles, one color per corner.
    const engine::Vertex vertices[] = {
        // position                 color
        { -0.5f, -0.5f, 0.0f,      0.92f, 0.26f, 0.21f },  // bottom-left  red
        {  0.5f, -0.5f, 0.0f,      0.30f, 0.69f, 0.31f },  // bottom-right green
        {  0.5f,  0.5f, 0.0f,      0.26f, 0.58f, 0.97f },  // top-right     blue
        { -0.5f,  0.5f, 0.0f,      0.99f, 0.75f, 0.19f },  // top-left      yellow
    };
    const std::uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };

    engine::Shader shader = engine::Shader::fromSource(kVertexShader, kFragmentShader);
    engine::Mesh quad;
    if (!shader.valid() || !quad.create(vertices, 4, indices, 6)) {
        std::fprintf(stderr, "Failed to create shader or mesh; see messages above.\n");
        return 1;
    }

    // Static camera looking at the quad from above/right/front.
    const engine::Vec3 eye   (1.6f, 1.1f, 2.6f);
    const engine::Vec3 target(0.0f, 0.0f, 0.0f);
    const engine::Vec3 up    (0.0f, 1.0f, 0.0f);

    float spinAngle = 0.0f;
    bool spinning = true;
    bool wasSpaceDown = false;

    app.run([&](float dt) {
        // --- input (edge-triggered pause) ---
        const bool spaceDown = app.window().isKeyDown(engine::Key::Space);
        if (spaceDown && !wasSpaceDown) {
            spinning = !spinning;
        }
        wasSpaceDown = spaceDown;

        if (app.window().isKeyDown(engine::Key::Escape)) {
            app.window().requestClose();
        }

        if (spinning) {
            spinAngle += dt * 0.8f;  // radians per second
        }

        // --- transforms (recomputed per frame so resizes keep aspect) ---
        int fbw = 0, fbh = 0;
        app.window().getFramebufferSize(fbw, fbh);
        const float aspect = fbh > 0 ? static_cast<float>(fbw) / static_cast<float>(fbh) : 1.0f;

        const engine::Mat4 model = engine::Mat4::rotateY(spinAngle);
        const engine::Mat4 view  = engine::Mat4::lookAt(eye, target, up);
        const engine::Mat4 proj  = engine::Mat4::perspective(0.7854f /* 45 deg */, aspect, 0.1f, 100.0f);
        const engine::Mat4 mvp   = proj * view * model;

        // --- draw ---
        engine::Renderer& renderer = app.renderer();
        renderer.setViewport(0, 0, fbw, fbh);
        renderer.clear();

        shader.bind();
        shader.setMat4("uMVP", mvp);
        renderer.drawIndexed(quad);
    });

    // shader/quad are destroyed here, BEFORE `app` (and its GL context).
    return 0;
}