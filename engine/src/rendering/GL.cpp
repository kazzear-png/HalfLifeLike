#include "rendering/GL.h"

#include <cstdio>

#if defined(_WIN32)
// Some Windows drivers do not resolve GL 1.1 entry points through
// wglGetProcAddress; fall back to opengl32.dll for anything missing.
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace engine {
namespace gl {

bool load(ProcAddressFn getProcAddress) {
    if (getProcAddress == nullptr) {
        std::fprintf(stderr, "[GLLoader] No proc-address callback provided.\n");
        return false;
    }

#if defined(_WIN32)
    HMODULE opengl32 = nullptr;  // process-lifetime; intentionally never freed
#endif

    auto resolve = [&](const char* glName) -> void* {
        void* proc = getProcAddress(glName);
#if defined(_WIN32)
        if (proc == nullptr) {
            if (opengl32 == nullptr) {
                opengl32 = LoadLibraryA("opengl32.dll");
            }
            if (opengl32 != nullptr) {
                proc = reinterpret_cast<void*>(GetProcAddress(opengl32, glName));
            }
        }
#endif
        return proc;
    };

    bool ok = true;

#define ENGINE_GL_LOAD(fn)                                                          \
    do {                                                                            \
        fn = reinterpret_cast<decltype(fn)>(resolve("gl" #fn));                     \
        if (fn == nullptr) {                                                        \
            std::fprintf(stderr, "[GLLoader] Missing entry point: gl%s\n", #fn);    \
            ok = false;                                                             \
        }                                                                           \
    } while (0)

    ENGINE_GL_LOAD(GetString);
    ENGINE_GL_LOAD(GetError);
    ENGINE_GL_LOAD(Enable);
    ENGINE_GL_LOAD(Viewport);
    ENGINE_GL_LOAD(ClearColor);
    ENGINE_GL_LOAD(Clear);
    ENGINE_GL_LOAD(DrawElements);
    ENGINE_GL_LOAD(CreateShader);
    ENGINE_GL_LOAD(ShaderSource);
    ENGINE_GL_LOAD(CompileShader);
    ENGINE_GL_LOAD(GetShaderiv);
    ENGINE_GL_LOAD(GetShaderInfoLog);
    ENGINE_GL_LOAD(DeleteShader);
    ENGINE_GL_LOAD(CreateProgram);
    ENGINE_GL_LOAD(AttachShader);
    ENGINE_GL_LOAD(LinkProgram);
    ENGINE_GL_LOAD(GetProgramiv);
    ENGINE_GL_LOAD(GetProgramInfoLog);
    ENGINE_GL_LOAD(DeleteProgram);
    ENGINE_GL_LOAD(UseProgram);
    ENGINE_GL_LOAD(GetUniformLocation);
    ENGINE_GL_LOAD(UniformMatrix4fv);
    ENGINE_GL_LOAD(Uniform4f);
    ENGINE_GL_LOAD(GenVertexArrays);
    ENGINE_GL_LOAD(BindVertexArray);
    ENGINE_GL_LOAD(DeleteVertexArrays);
    ENGINE_GL_LOAD(GenBuffers);
    ENGINE_GL_LOAD(BindBuffer);
    ENGINE_GL_LOAD(BufferData);
    ENGINE_GL_LOAD(DeleteBuffers);
    ENGINE_GL_LOAD(EnableVertexAttribArray);
    ENGINE_GL_LOAD(VertexAttribPointer);

#undef ENGINE_GL_LOAD

    return ok;
}

} // namespace gl
} // namespace engine