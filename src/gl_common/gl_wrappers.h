#if !defined(_BS_GL_WRAPPERS_H_) && !defined(__EMSCRIPTEN__) && !defined(PLATFORM_PS3) && !defined(__ANDROID__)
#define _BS_GL_WRAPPERS_H_

static inline void rt_glBindVertexArray(GLuint vao) {
    if (glBindVertexArray) glBindVertexArray(vao);
    else glBindVertexArrayOES(vao);
}
#undef glBindVertexArray
#define glBindVertexArray rt_glBindVertexArray

static inline void rt_glGenVertexArrays(GLsizei n, GLuint* arrays) {
    if (glGenVertexArrays) glGenVertexArrays(n, arrays);
    else glGenVertexArraysOES(n, arrays);
}
#undef glGenVertexArrays
#define glGenVertexArrays rt_glGenVertexArrays

static inline void rt_glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    if (glDeleteVertexArrays) glDeleteVertexArrays(n, arrays);
    else glDeleteVertexArraysOES(n, arrays);
}
#undef glDeleteVertexArrays
#define glDeleteVertexArrays rt_glDeleteVertexArrays

static inline void rt_glGenFramebuffers(GLsizei n, GLuint* ids) {
    if (glGenFramebuffers) glGenFramebuffers(n, ids);
    else glGenFramebuffersEXT(n, ids);
}
#undef glGenFramebuffers
#define glGenFramebuffers rt_glGenFramebuffers

static inline void rt_glBindFramebuffer(GLenum target, GLuint fb) {
    if (glBindFramebuffer) glBindFramebuffer(target, fb);
    else glBindFramebufferEXT(target, fb);
}
#undef glBindFramebuffer
#define glBindFramebuffer rt_glBindFramebuffer

static inline void rt_glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    if (glFramebufferTexture2D) glFramebufferTexture2D(target, attachment, textarget, texture, level);
    else glFramebufferTexture2DEXT(target, attachment, textarget, texture, level);
}
#undef glFramebufferTexture2D
#define glFramebufferTexture2D rt_glFramebufferTexture2D

static inline void rt_glDeleteFramebuffers(GLsizei n, const GLuint* ids) {
    if (glDeleteFramebuffers) glDeleteFramebuffers(n, ids);
    else glDeleteFramebuffersEXT(n, ids);
}
#undef glDeleteFramebuffers
#define glDeleteFramebuffers rt_glDeleteFramebuffers

static inline GLenum rt_glCheckFramebufferStatus(GLenum target) {
    if (glCheckFramebufferStatus) return glCheckFramebufferStatus(target);
    else return glCheckFramebufferStatusEXT(target);
}
#undef glCheckFramebufferStatus
#define glCheckFramebufferStatus rt_glCheckFramebufferStatus

static inline void rt_glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                                        GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                                        GLbitfield mask, GLenum filter) {
    if (glBlitFramebuffer) glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
    else glBlitFramebufferEXT(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}
#undef glBlitFramebuffer
#define glBlitFramebuffer rt_glBlitFramebuffer

#endif
