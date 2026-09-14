#include "gaussian_blur.h"
#include "src/AgentLog.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace mcoverlay {
namespace {
constexpr GLenum FragmentShader = 0x8B30, VertexShader = 0x8B31;
constexpr GLenum CompileStatus = 0x8B81, LinkStatus = 0x8B82;
constexpr GLenum Framebuffer = 0x8D40, FramebufferBinding = 0x8CA6;
constexpr GLenum ColorAttachment0 = 0x8CE0, FramebufferComplete = 0x8CD5;
constexpr GLenum CurrentProgram = 0x8B8D, ActiveTexture = 0x84E0, Texture0 = 0x84C0;
constexpr GLenum ClampToEdge = 0x812F;
#define GLPROC(ret, name, ...) using name##Fn = ret (APIENTRY*)(__VA_ARGS__); name##Fn name = nullptr
GLPROC(GLuint, CreateShader, GLenum);
GLPROC(void, ShaderSource, GLuint, GLsizei, const char* const*, const GLint*);
GLPROC(void, CompileShader, GLuint);
GLPROC(void, GetShaderiv, GLuint, GLenum, GLint*);
GLPROC(void, DeleteShader, GLuint);
GLPROC(GLuint, CreateProgram);
GLPROC(void, AttachShader, GLuint, GLuint);
GLPROC(void, LinkProgram, GLuint);
GLPROC(void, GetProgramiv, GLuint, GLenum, GLint*);
GLPROC(void, UseProgram, GLuint);
GLPROC(void, DeleteProgram, GLuint);
GLPROC(GLint, GetUniformLocation, GLuint, const char*);
GLPROC(void, Uniform1i, GLint, GLint);
GLPROC(void, Uniform1f, GLint, GLfloat);
GLPROC(void, Uniform2f, GLint, GLfloat, GLfloat);
GLPROC(void, GenFramebuffers, GLsizei, GLuint*);
GLPROC(void, DeleteFramebuffers, GLsizei, const GLuint*);
GLPROC(void, BindFramebuffer, GLenum, GLuint);
GLPROC(void, FramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint);
GLPROC(GLenum, CheckFramebufferStatus, GLenum);
GLPROC(void, SetActiveTexture, GLenum);
GLPROC(void, BlendEquation, GLenum);
#undef GLPROC
template<class T> bool load(T& proc, const char* name, const char* alt = nullptr)
{
    PROC address = wglGetProcAddress(name);
    const auto valid = [](PROC p) { auto v = reinterpret_cast<std::intptr_t>(p); return v > 3 || v < -1; };
    if (!valid(address) && alt) address = wglGetProcAddress(alt);
    static_assert(sizeof(proc) == sizeof(address));
    if (valid(address)) std::memcpy(&proc, &address, sizeof(proc));
    else proc = nullptr;
    return proc != nullptr;
}
void quad(bool flip = false)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, flip ? 1.0F : 0.0F); glVertex2f(-1, -1);
    glTexCoord2f(1, flip ? 1.0F : 0.0F); glVertex2f(1, -1);
    glTexCoord2f(1, flip ? 0.0F : 1.0F); glVertex2f(1, 1);
    glTexCoord2f(0, flip ? 0.0F : 1.0F); glVertex2f(-1, 1);
    glEnd();
}
}
bool GaussianBlur::initialize() noexcept
{
    if (m_attempted) return m_program != 0;
    m_attempted = true;
#define LOAD(name) load(name, "gl" #name)
    bool ok = LOAD(CreateShader) && LOAD(ShaderSource) && LOAD(CompileShader) &&
        LOAD(GetShaderiv) && LOAD(DeleteShader) && LOAD(CreateProgram) &&
        LOAD(AttachShader) && LOAD(LinkProgram) && LOAD(GetProgramiv) &&
        LOAD(UseProgram) && LOAD(DeleteProgram) && LOAD(GetUniformLocation) &&
        LOAD(Uniform1i) && LOAD(Uniform1f) && LOAD(Uniform2f) &&
        load(GenFramebuffers, "glGenFramebuffers", "glGenFramebuffersEXT") &&
        load(DeleteFramebuffers, "glDeleteFramebuffers", "glDeleteFramebuffersEXT") &&
        load(BindFramebuffer, "glBindFramebuffer", "glBindFramebufferEXT") &&
        load(FramebufferTexture2D, "glFramebufferTexture2D", "glFramebufferTexture2DEXT") &&
        load(CheckFramebufferStatus, "glCheckFramebufferStatus", "glCheckFramebufferStatusEXT") &&
        load(SetActiveTexture, "glActiveTexture") && LOAD(BlendEquation);
#undef LOAD
    if (!ok) { log::error("Gaussian blur requires GLSL/FBO support; blur disabled."); return false; }
    const char* vertex = "#version 120\nvoid main(){gl_Position=gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}";
    const char* fragment =
        "#version 120\nuniform sampler2D image;uniform vec2 direction;uniform float sigma;uniform float alpha;"
        "void main(){vec4 sum=vec4(0.0);float norm=0.0;"
        "for(int i=-12;i<=12;i++){float f=float(i);float w=exp(-f*f/(2.0*sigma*sigma));"
        "sum+=texture2D(image,gl_TexCoord[0].xy+direction*f)*w;norm+=w;}"
        "gl_FragColor=vec4((sum/norm).rgb,alpha);}";
    GLuint shaders[2]{CreateShader(VertexShader), CreateShader(FragmentShader)};
    const char* sources[2]{vertex, fragment};
    for (int i = 0; i < 2; ++i) {
        ShaderSource(shaders[i], 1, &sources[i], nullptr);
        CompileShader(shaders[i]);
        GLint compiled = 0;
        GetShaderiv(shaders[i], CompileStatus, &compiled);
        ok = ok && compiled != 0;
    }
    if (ok) {
        m_program = CreateProgram();
        for (GLuint shader : shaders) AttachShader(m_program, shader);
        LinkProgram(m_program);
        GLint linked = 0;
        GetProgramiv(m_program, LinkStatus, &linked);
        ok = linked != 0;
    }
    for (GLuint shader : shaders) DeleteShader(shader);
    if (!ok) { if (m_program) DeleteProgram(m_program); m_program = 0; return false; }
    m_direction = GetUniformLocation(m_program, "direction");
    m_sigma = GetUniformLocation(m_program, "sigma");
    GenFramebuffers(1, &m_fbo);
    glGenTextures(2, m_textures);
    return true;
}

bool GaussianBlur::draw(GLuint source, int width, int height, float sigma, float opacity) noexcept
{
    if (!source || width < 2 || height < 2 || opacity <= 0.0F || !initialize()) return false;
    GLint program = 0, framebuffer = 0, active = 0, matrix = 0, texture = 0;
    glGetIntegerv(CurrentProgram, &program);
    glGetIntegerv(FramebufferBinding, &framebuffer);
    glGetIntegerv(ActiveTexture, &active);
    glGetIntegerv(GL_MATRIX_MODE, &matrix);
    SetActiveTexture(Texture0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST); glDisable(GL_LIGHTING); glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_TEXTURE_2D);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
    const int w = std::max(1, width / 4), h = std::max(1, height / 4);
    if (w != m_width || h != m_height) {
        for (GLuint t : m_textures) {
            glBindTexture(GL_TEXTURE_2D, t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ClampToEdge);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ClampToEdge);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        m_width = w; m_height = h;
    }
    UseProgram(m_program);
    Uniform1i(GetUniformLocation(m_program, "image"), 0);
    Uniform1f(m_sigma, std::clamp(sigma, 0.5F, 6.0F));
    const GLint alphaLocation = GetUniformLocation(m_program, "alpha");
    Uniform1f(alphaLocation, 1.0F);
    BindFramebuffer(Framebuffer, m_fbo);
    glDrawBuffer(ColorAttachment0);
    glViewport(0, 0, w, h);
    bool ok = true;
    for (int pass = 0; pass < 2; ++pass) {
        FramebufferTexture2D(Framebuffer, ColorAttachment0, GL_TEXTURE_2D, m_textures[pass], 0);
        if (CheckFramebufferStatus(Framebuffer) != FramebufferComplete) { ok = false; break; }
        glBindTexture(GL_TEXTURE_2D, pass == 0 ? source : m_textures[0]);
        Uniform2f(m_direction, pass == 0 ? 1.0F / static_cast<float>(w) : 0.0F,
                              pass == 1 ? 1.0F / static_cast<float>(h) : 0.0F);
        quad();
    }
    BindFramebuffer(Framebuffer, static_cast<GLuint>(framebuffer));
    // Restore draw buffer/viewport before compositing to the game's framebuffer.
    glPopAttrib();
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    if (ok) {
        UseProgram(m_program);
        Uniform2f(m_direction, 0, 0);
        Uniform1f(alphaLocation, std::clamp(opacity, 0.0F, 1.0F));
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
        glDisable(GL_ALPHA_TEST); glDisable(GL_LIGHTING);
        glDisable(GL_STENCIL_TEST); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_TEXTURE_2D); glEnable(GL_BLEND);
        BlendEquation(0x8006); // GL_FUNC_ADD
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glBindTexture(GL_TEXTURE_2D, m_textures[1]);
        glColor4f(1, 1, 1, std::clamp(opacity, 0.0F, 1.0F));
        quad();
    }
    glPopAttrib();
    glMatrixMode(GL_TEXTURE); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(static_cast<GLenum>(matrix));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    SetActiveTexture(static_cast<GLenum>(active));
    UseProgram(static_cast<GLuint>(program));
    return ok;
}
void GaussianBlur::release() noexcept
{
    if (m_program && DeleteProgram) DeleteProgram(m_program);
    if (m_fbo && DeleteFramebuffers) DeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(2, m_textures);
    abandon();
}
void GaussianBlur::abandon() noexcept
{
    m_program = m_fbo = m_textures[0] = m_textures[1] = 0;
    m_width = m_height = 0;
    m_attempted = false;
}
}
