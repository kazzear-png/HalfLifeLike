#pragma once
//
// Scoped OpenGL 3.3 core function loader.
//
// Loads ONLY the entry points the renderer currently uses. Rationale:
//   - no glad/GLEW/generator dependency for M1
//   - keeps the GL surface area explicit and reviewable
// Upgrade path: replace with glad2 when coverage grows (docs/ARCHITECTURE.md).

#include <cstddef>
#include <cstdint>

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
using GLuint64   = std::uint64_t;
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
    UnsignedByte       = 0x1401,
    HalfFloat          = 0x140B,
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

    // M3: HDR pipeline + textures
    Texture2D          = 0x0DE1,
    RGBA               = 0x1908,
    RGBA8              = 0x8058,
    RGBA16F            = 0x881A,   // half-float HDR color target (renderable in core GL 3.0+)
    TextureMinFilter   = 0x2801,
    TextureMagFilter   = 0x2800,
    TextureWrapS       = 0x2802,
    TextureWrapT       = 0x2803,
    Nearest            = 0x2600,
    Linear             = 0x2601,
    ClampToEdge        = 0x812F,
    Texture0           = 0x84C0,

    // M3: framebuffer objects
    Framebuffer           = 0x8D40,
    ReadFramebuffer       = 0x8CA8,
    DrawFramebuffer       = 0x8CA9,
    Renderbuffer          = 0x8D41,
    ColorAttachment0      = 0x8CE0,
    DepthAttachment       = 0x8D00,
    DepthComponent24      = 0x81A6,
    FramebufferComplete   = 0x8CD5,

    // M3.3: timer queries (benchmark GPU frame time)
    TimeElapsed          = 0x88BF,
    QueryResult          = 0x8866,
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
inline void  (ENGINE_GL_CALL* Uniform1f)(GLint location, GLfloat v0) = nullptr;
inline void  (ENGINE_GL_CALL* Uniform1i)(GLint location, GLint v0) = nullptr;
inline void  (ENGINE_GL_CALL* Uniform3f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) = nullptr;
inline void  (ENGINE_GL_CALL* Uniform3fv)(GLint location, GLsizei count, const GLfloat* value) = nullptr;

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

// M3: attribute-less drawing (fullscreen tonemap triangle via gl_VertexID)
inline void (ENGINE_GL_CALL* DrawArrays)(GLenum mode, GLint first, GLsizei count) = nullptr;

// M3: render state
inline void (ENGINE_GL_CALL* Disable)(GLenum cap) = nullptr;

// M3: textures
inline void (ENGINE_GL_CALL* GenTextures)(GLsizei n, GLuint* textures) = nullptr;
inline void (ENGINE_GL_CALL* DeleteTextures)(GLsizei n, const GLuint* textures) = nullptr;
inline void (ENGINE_GL_CALL* BindTexture)(GLenum target, GLuint texture) = nullptr;
inline void (ENGINE_GL_CALL* ActiveTexture)(GLenum unit) = nullptr;
inline void (ENGINE_GL_CALL* TexImage2D)(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* data) = nullptr;
inline void (ENGINE_GL_CALL* TexParameteri)(GLenum target, GLenum pname, GLint param) = nullptr;

// M3: framebuffer objects
inline void (ENGINE_GL_CALL* GenFramebuffers)(GLsizei n, GLuint* framebuffers) = nullptr;
inline void (ENGINE_GL_CALL* DeleteFramebuffers)(GLsizei n, const GLuint* framebuffers) = nullptr;
inline void (ENGINE_GL_CALL* BindFramebuffer)(GLenum target, GLuint framebuffer) = nullptr;
inline void (ENGINE_GL_CALL* FramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) = nullptr;
inline void (ENGINE_GL_CALL* GenRenderbuffers)(GLsizei n, GLuint* renderbuffers) = nullptr;
inline void (ENGINE_GL_CALL* DeleteRenderbuffers)(GLsizei n, const GLuint* renderbuffers) = nullptr;
inline void (ENGINE_GL_CALL* BindRenderbuffer)(GLenum target, GLuint renderbuffer) = nullptr;
inline void (ENGINE_GL_CALL* RenderbufferStorage)(GLenum target, GLenum internalFormat, GLsizei width, GLsizei height) = nullptr;
inline void (ENGINE_GL_CALL* RenderbufferStorageMultisample)(GLenum target, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height) = nullptr;
inline void (ENGINE_GL_CALL* FramebufferRenderbuffer)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) = nullptr;
inline GLenum (ENGINE_GL_CALL* CheckFramebufferStatus)(GLenum target) = nullptr;
inline void (ENGINE_GL_CALL* BlitFramebuffer)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) = nullptr;

// M3: readback (verification screenshots)
inline void (ENGINE_GL_CALL* ReadPixels)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* data) = nullptr;

// M3.3: timer queries (benchmark GPU frame timing; one-frame-lag readback)
inline void (ENGINE_GL_CALL* GenQueries)(GLsizei n, GLuint* ids) = nullptr;
inline void (ENGINE_GL_CALL* DeleteQueries)(GLsizei n, const GLuint* ids) = nullptr;
inline void (ENGINE_GL_CALL* BeginQuery)(GLenum target, GLuint id) = nullptr;
inline void (ENGINE_GL_CALL* EndQuery)(GLenum target) = nullptr;
inline void (ENGINE_GL_CALL* GetQueryObjectui64v)(GLuint id, GLenum pname, GLuint64* params) = nullptr;

// Resolves every entry point above via getProcAddress (the platform layer
// supplies this, typically wrapping glfwGetProcAddress). Returns false if any
// entry point is missing; failures are printed to stderr.
using ProcAddressFn = void* (*)(const char* name);
bool load(ProcAddressFn getProcAddress);

} // namespace gl
} // namespace engine