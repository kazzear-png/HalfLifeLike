//
// Milestone 2: event-driven input + FPS fly camera.
//   click     lock the cursor and look with the mouse
//   ESC       unlock (when locked); when already unlocked, quit
//   W/A/S/D   fly (W follows the view direction, including pitch)
//   E / Q     up / down
//   SPACE     pause/resume the quad's spin

#include "core/Application.h"
#include "math/Mat4.h"
#include "math/Vec3.h"
#include "rendering/Camera.h"
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
    desc.title  = "Sandbox - M2 Fly Camera";
    desc.width  = 1280;
    desc.height = 720;
    desc.vsync  = true;

    engine::Application app(desc);
    if (!app.valid()) {
        std::fprintf(stderr, "Engine initialization failed; see messages above.\n");
        return 1;
    }

    // --- static scene: spinning quad (M1 regression target) + reference floor ---
    const engine::Vertex quadVerts[] = {
        // position                color
        { -0.5f, -0.5f, 0.0f,     0.92f, 0.26f, 0.21f },  // bottom-left  red
        {  0.5f, -0.5f, 0.0f,     0.30f, 0.69f, 0.31f },  // bottom-right green
        {  0.5f,  0.5f, 0.0f,     0.26f, 0.58f, 0.97f },  // top-right     blue
        { -0.5f,  0.5f, 0.0f,     0.99f, 0.75f, 0.19f },  // top-left      yellow
    };
    const std::uint32_t quadIndices[] = { 0, 1, 2, 0, 2, 3 };

    const engine::Vertex floorVerts[] = {
        { -6.0f, -0.75f, -6.0f,   0.30f, 0.32f, 0.36f },
        {  6.0f, -0.75f, -6.0f,   0.30f, 0.32f, 0.36f },
        {  6.0f, -0.75f,  6.0f,   0.30f, 0.32f, 0.36f },
        { -6.0f, -0.75f,  6.0f,   0.30f, 0.32f, 0.36f },
    };
    const std::uint32_t floorIndices[] = { 0, 1, 2, 0, 2, 3 };

    engine::Shader shader = engine::Shader::fromSource(kVertexShader, kFragmentShader);
    engine::Mesh quad;
    engine::Mesh floor;
    if (!shader.valid() || !quad.create(quadVerts, 4, quadIndices, 6)
                         || !floor.create(floorVerts, 4, floorIndices, 6)) {
        std::fprintf(stderr, "Failed to create shader or meshes; see messages above.\n");
        return 1;
    }

    engine::Camera camera;
    camera.setPosition(engine::Vec3(0.0f, 0.6f, 2.8f));
    camera.setPitch(-0.25f);  // slight downward tilt to see floor + quad

    constexpr float kMouseSensitivity = 0.0025f;  // radians per pixel
    constexpr float kMoveSpeed        = 2.5f;     // units per second

    float spinAngle = 0.0f;
    bool spinning = true;

    app.run([&](float dt) {
        engine::Input& in = app.input();
        engine::Renderer& renderer = app.renderer();

        // --- input ---
        if (in.pressed(engine::Key::Escape)) {
            if (in.cursorLocked()) {
                in.setCursorMode(engine::CursorMode::Normal);
            } else {
                app.window().requestClose();
            }
        }
        if (!in.cursorLocked() && in.mousePressed(engine::MouseButton::Left)) {
            in.setCursorMode(engine::CursorMode::Locked);
        }
        if (in.pressed(engine::Key::Space)) {
            spinning = !spinning;
        }

        if (in.cursorLocked()) {
            camera.addYaw(in.mouseDX() * kMouseSensitivity);
            camera.addPitch(-in.mouseDY() * kMouseSensitivity);  // mouse down = look down
        }

        // --- movement (fly-cam: W follows full view direction) ---
        engine::Vec3 move(0.0f, 0.0f, 0.0f);
        if (in.isKeyDown(engine::Key::W)) move = move + camera.forward();
        if (in.isKeyDown(engine::Key::S)) move = move - camera.forward();
        if (in.isKeyDown(engine::Key::D)) move = move + camera.right();
        if (in.isKeyDown(engine::Key::A)) move = move - camera.right();
        if (in.isKeyDown(engine::Key::E)) move = move + engine::Vec3(0.0f, 1.0f, 0.0f);
        if (in.isKeyDown(engine::Key::Q)) move = move - engine::Vec3(0.0f, 1.0f, 0.0f);
        if (engine::length(move) > 0.0f) {
            camera.setPosition(camera.position() + engine::normalize(move) * (kMoveSpeed * dt));
        }

        if (spinning) {
            spinAngle += dt * 0.8f;
        }

        // --- camera projection (per frame: resize-safe aspect) ---
        int fbw = 0, fbh = 0;
        app.window().getFramebufferSize(fbw, fbh);
        const float aspect = fbh > 0 ? static_cast<float>(fbw) / static_cast<float>(fbh) : 1.0f;
        camera.setPerspective(0.7854f /* 45 deg */, aspect, 0.1f, 100.0f);
        const engine::Mat4 vp = camera.viewProjection();

        // --- draw ---
        renderer.setViewport(0, 0, fbw, fbh);
        renderer.clear();

        shader.bind();
        shader.setMat4("uMVP", vp);  // floor: identity model
        renderer.drawIndexed(floor);

        shader.setMat4("uMVP", vp * engine::Mat4::rotateY(spinAngle));  // spinning quad
        renderer.drawIndexed(quad);
    });

    // shader/quad/floor destroyed here, BEFORE `app` (and its GL context).
    return 0;
}