#pragma once
//
// Scoped OpenGL 3.3 core function loader.
//
// Loads ONLY the entry points the renderer currently uses. Rationale:
//   - no glad/GLEW/generator dependency for M1
//   - keeps the GL surface area explicit and reviewable
// Upgrade path: replace with glad2 when coverage grows (docs/ARCHITECTURE.md).

#include <cstddef>

namespace engine {
namespace gl {

// ---- GL scalar types (must match the driver ABI) ----
using GLenum     = unsigned int;
using GLboolean  = unsigned char;
using GLbitfield = unsigned int;
using GLint      = int;
using GLsizei    = int;
using GLuint     = unsigned int;
using GLchar     = char;
using GLubyte    = unsigned char;
using GLfloat    = float;
using GLsizeiptr = std::ptrdiff_t;

// GL entry points use stdcall on 32-bit Windows; default convention elsewhere.
#if defined(_WIN32) && (defined(_M_IX86) || defined(__i386__))
    #define ENGINE_GL_CALL __stdcall
#else
    #define ENGINE_GL_CALL
#endif

// ---- Enums used by the engine ----
enum : GLenum {
    Triangles          = 0x0004,
    DepthTest          = 0x0B71,
    ColorBufferBit     = 0x00004000,
    DepthBufferBit     = 0x00000100,
    UnsignedInt        = 0x1405,
    Float              = 0x1406,
    VertexShader       = 0x8B31,
    FragmentShader     = 0x8B30,
    CompileStatus      = 0x8B81,
    LinkStatus         = 0x8B82,
    InfoLogLength      = 0x8B84,
    ArrayBuffer        = 0x8892,
    ElementArrayBuffer = 0x8893,
    StaticDraw         = 0x88E4,
    Version            = 0x1F02,
    NoError            = 0,
};

// ---- Entry points (null until load() succeeds) ----

// Queries
inline const GLubyte* (ENGINE_GL_CALL* GetString)(GLenum name) = nullptr;
inline GLenum         (ENGINE_GL_CALL* GetError)(void)         = nullptr;

// State
inline void (ENGINE_GL_CALL* Enable)(GLenum cap)                                        = nullptr;
inline void (ENGINE_GL_CALL* Viewport)(GLint x, GLint y, GLsizei width, GLsizei height) = nullptr;
inline void (ENGINE_GL_CALL* ClearColor)(GLfloat r, GLfloat g, GLfloat b, GLfloat a)    = nullptr;
inline void (ENGINE_GL_CALL* Clear)(GLbitfield mask)                                    = nullptr;

// Drawing
inline void (ENGINE_GL_CALL* DrawElements)(GLenum mode, GLsizei count, GLenum type, const void* indices) = nullptr;

// Shader / program
inline GLuint (ENGINE_GL_CALL* CreateShader)(GLenum shaderType) = nullptr;
inline void   (ENGINE_GL_CALL* ShaderSource)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length) = nullptr;
inline void   (ENGINE_GL_CALL* CompileShader)(GLuint shader) = nullptr;
inline void   (ENGINE_GL_CALL* GetShaderiv)(GLuint shader, GLenum pname, GLint* params) = nullptr;
inline void   (ENGINE_GL_CALL* GetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog) = nullptr;
inline void   (ENGINE_GL_CALL* DeleteShader)(GLuint shader) = nullptr;
inline GLuint (ENGINE_GL_CALL* CreateProgram)(void) = nullptr;
inline void   (ENGINE_GL_CALL* AttachShader)(GLuint program, GLuint shader) = nullptr;
inline void   (ENGINE_GL_CALL* LinkProgram)(GLuint program) = nullptr;
inline void   (ENGINE_GL_CALL* GetProgramiv)(GLuint program, GLenum pname, GLint* params) = nullptr;
inline void   (ENGINE_GL_CALL* GetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog) = nullptr;
inline void   (ENGINE_GL_CALL* DeleteProgram)(GLuint program) = nullptr;
inline void   (ENGINE_GL_CALL* UseProgram)(GLuint program) = nullptr;

// Uniforms
inline GLint (ENGINE_GL_CALL* GetUniformLocation)(GLuint program, const GLchar* name) = nullptr;
inline void  (ENGINE_GL_CALL* UniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) = nullptr;
inline void  (ENGINE_GL_CALL* Uniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) = nullptr;

// Buffers / vertex arrays
inline void (ENGINE_GL_CALL* GenVertexArrays)(GLsizei n, GLuint* arrays) = nullptr;
inline void (ENGINE_GL_CALL* BindVertexArray)(GLuint array) = nullptr;
inline void (ENGINE_GL_CALL* DeleteVertexArrays)(GLsizei n, const GLuint* arrays) = nullptr;
inline void (ENGINE_GL_CALL* GenBuffers)(GLsizei n, GLuint* buffers) = nullptr;
inline void (ENGINE_GL_CALL* BindBuffer)(GLenum target, GLuint buffer) = nullptr;
inline void (ENGINE_GL_CALL* BufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage) = nullptr;
inline void (ENGINE_GL_CALL* DeleteBuffers)(GLsizei n, const GLuint* buffers) = nullptr;
inline void (ENGINE_GL_CALL* EnableVertexAttribArray)(GLuint index) = nullptr;
inline void (ENGINE_GL_CALL* VertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) = nullptr;

// Resolves every entry point above via getProcAddress (the platform layer
// supplies this, typically wrapping glfwGetProcAddress). Returns false if any
// entry point is missing; failures are printed to stderr.
using ProcAddressFn = void* (*)(const char* name);
bool load(ProcAddressFn getProcAddress);

} // namespace gl
} // namespace engine