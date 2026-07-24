#if !defined(_BS_GL_WRAPPERS_H_) && !defined(__EMSCRIPTEN__) && !defined(PLATFORM_PS3) && !defined(__ANDROID__)
#define _BS_GL_WRAPPERS_H_

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

#endif
