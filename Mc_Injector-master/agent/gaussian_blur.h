#pragma once
#include <windows.h>
#include <gl/GL.h>

namespace mcoverlay {
// Two separable convolution passes at quarter resolution; no shifted copies,
// CPU readbacks or per-frame shader compilation. Owned by one GL context.
class GaussianBlur final {
public:
    bool draw(GLuint source, int width, int height, float sigma, float opacity) noexcept;
    void release() noexcept;
    void abandon() noexcept; // context lost: forget, never delete in another HGLRC
private:
    bool initialize() noexcept;
    bool m_attempted = false;
    GLuint m_program = 0, m_fbo = 0, m_textures[2]{};
    int m_width = 0, m_height = 0;
    GLint m_direction = -1, m_sigma = -1;
};
}
