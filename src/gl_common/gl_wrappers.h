#if !defined(_BS_GL_WRAPPERS_H_) && !defined(__EMSCRIPTEN__) && !defined(PLATFORM_PS3) && !defined(__ANDROID__)
#define _BS_GL_WRAPPERS_H_

#include "gl_renderer.h"

static inline void gl_init_wrappers(void) {
    if (!glBindVertexArray)
        glBindVertexArray = glBindVertexArrayOES;

    if (!glGenVertexArrays)
        glGenVertexArrays = glGenVertexArraysOES;

    if (!glDeleteVertexArrays)
        glDeleteVertexArrays = glDeleteVertexArraysOES;

    if (!glGenFramebuffers)
        glGenFramebuffers = glGenFramebuffersEXT;

    if (!glBindFramebuffer)
        glBindFramebuffer = glBindFramebufferEXT;

    if (!glFramebufferTexture2D)
        glFramebufferTexture2D = glFramebufferTexture2DEXT;

    if (!glDeleteFramebuffers)
        glDeleteFramebuffers = glDeleteFramebuffersEXT;

    if (!glCheckFramebufferStatus)
        glCheckFramebufferStatus = glCheckFramebufferStatusEXT;

    if (!glBlitFramebuffer)
        glBlitFramebuffer = glBlitFramebufferEXT;

    if (!glBlendEquation)
        glBlendEquation = glBlendEquationEXT;

    if (!glBlendFuncSeparate)
        glBlendFuncSeparate = glBlendFuncSeparateEXT;
}

static inline void glBindFramebufferCached(GLRenderer* gl, GLenum target, GLuint fbo) {
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
        gl->state.currentFbo = fbo;
    glBindFramebuffer(target, fbo);
}

static inline void glViewportCached(GLRenderer* gl, int32_t x, int32_t y, int32_t w, int32_t h) {
    gl->state.viewport[0] = x; gl->state.viewport[1] = y;
    gl->state.viewport[2] = w; gl->state.viewport[3] = h;
    glViewport(x, y, w, h);
}

static inline void glSetCap(GLRenderer* gl, GLenum cap, bool enable) {
    if (cap == GL_SCISSOR_TEST) gl->state.scissorEnabled = enable;
    else if (cap == GL_BLEND) gl->state.blendEnabled = enable;
    if (enable) glEnable(cap);
    else glDisable(cap);
}

#endif