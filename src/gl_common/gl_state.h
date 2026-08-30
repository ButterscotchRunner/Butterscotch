#ifndef _BS_GL_STATE_H
#define _BS_GL_STATE_H

#define GL_MAX_TEXTURE_2D 32

static GLenum activeTextureSlot = GL_TEXTURE0;
static GLenum blendSrcRGB = GL_ONE, blendDstRGB = GL_ZERO;
static GLenum blendSrcAlpha = GL_ONE, blendDstAlpha = GL_ZERO;
static GLenum blendEqRGB = GL_FUNC_ADD, blendEqAlpha = GL_FUNC_ADD;
static GLclampf clearColor[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
static GLint viewport[4] = {-1, -1, -1, -1};
static GLint scissor[4] = {-1, -1, -1, -1};
static GLuint currentReadFb = 0;
static GLuint currentDrawFb = 0;
static GLuint currentProgram = 0;
static GLuint currentVAO = 0xFFFFFFFF;
static GLuint boundTextures2D[GL_MAX_TEXTURE_2D] = {0};
static GLboolean blend = GL_FALSE;
static GLboolean depthTest = GL_FALSE;
static GLboolean scissorTest = GL_FALSE;
static GLboolean texture2D[GL_MAX_TEXTURE_2D] = {GL_FALSE};

static inline void cached_glViewport(int x, int y, int width, int height) {
    if (viewport[0] == x && viewport[1] == y && viewport[2] == width && viewport[3] == height) return;
    glViewport(x, y, width, height);
    viewport[0] = x;
    viewport[1] = y;
    viewport[2] = width;
    viewport[3] = height;
}
#undef glViewport
#define glViewport cached_glViewport

static inline void cached_glScissor(int x, int y, int width, int height) {
    if (scissor[0] == x && scissor[1] == y && scissor[2] == width && scissor[3] == height) return;
    glScissor(x, y, width, height);
    scissor[0] = x;
    scissor[1] = y;
    scissor[2] = width;
    scissor[3] = height;
}
#undef glScissor
#define glScissor cached_glScissor

static inline void cached_glBindFramebuffer(GLenum type, GLuint framebuffer) {
    switch (type) {
        case GL_READ_FRAMEBUFFER:
            if (currentReadFb == framebuffer) return;
            currentReadFb = framebuffer;
            break;
        case GL_DRAW_FRAMEBUFFER:
            if (currentDrawFb == framebuffer) return;
            currentDrawFb = framebuffer;
            break;
        case GL_FRAMEBUFFER:
            if (currentReadFb == framebuffer && currentDrawFb == framebuffer) return;
            currentReadFb = framebuffer;
            currentDrawFb = framebuffer;
            break;
        default:
            return;
    }

    glBindFramebuffer(type, framebuffer);
}
#undef glBindFramebuffer
#define glBindFramebuffer cached_glBindFramebuffer

static inline void cached_glGetIntegerv(GLenum pname, GLint* params) {
    if (!params) return;

    switch (pname) {
        case GL_VIEWPORT:
            params[0] = viewport[0];
            params[1] = viewport[1];
            params[2] = viewport[2];
            params[3] = viewport[3];
            return;
        case GL_FRAMEBUFFER_BINDING:
            params[0] = (GLint)currentDrawFb;
            return;
        case GL_READ_FRAMEBUFFER_BINDING:
            params[0] = (GLint)currentReadFb;
            return;
        default:
            glGetIntegerv(pname, params);
            break;
    }
}
#undef glGetIntegerv
#define glGetIntegerv cached_glGetIntegerv

static inline void cached_glActiveTexture(GLenum texture) {
    if (activeTextureSlot == texture) return;
    glActiveTexture(texture);
    activeTextureSlot = texture;
}

#undef glActiveTexture
#define glActiveTexture cached_glActiveTexture

static inline void cached_glBindTexture(GLenum target, GLuint texture) {
    if (target == GL_TEXTURE_2D) {
        int slotIndex = activeTextureSlot - GL_TEXTURE0;
        if (slotIndex >= 0 && slotIndex < GL_MAX_TEXTURE_2D) {
            if (boundTextures2D[slotIndex] == texture) return;
            boundTextures2D[slotIndex] = texture;
        }
    }
    glBindTexture(target, texture);
}

#undef glBindTexture
#define glBindTexture cached_glBindTexture

static inline void cached_glUseProgram(GLuint program) {
    if (currentProgram == program) return;
    glUseProgram(program);
    currentProgram = program;
}

#undef glUseProgram
#define glUseProgram cached_glUseProgram

static inline void cached_glBindVertexArray(GLuint vao) {
    if (currentVAO == vao) return;
    glBindVertexArray(vao);
    currentVAO = vao;
}

#undef glBindVertexArray
#define glBindVertexArray cached_glBindVertexArray

static inline void cached_glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    for (GLsizei i = 0; i < n; i++) {
        if (currentVAO == arrays[i]) currentVAO = 0;
    }
    glDeleteVertexArrays(n, arrays);
}
#undef glDeleteVertexArrays
#define glDeleteVertexArrays cached_glDeleteVertexArrays

static inline void cached_glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
    if (blendSrcRGB == srcRGB && blendDstRGB == dstRGB && blendSrcAlpha == srcAlpha && blendDstAlpha == dstAlpha) return;
    
    glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
    
    blendSrcRGB   = srcRGB;
    blendDstRGB   = dstRGB;
    blendSrcAlpha = srcAlpha;
    blendDstAlpha = dstAlpha;
}
#undef glBlendFuncSeparate
#define glBlendFuncSeparate cached_glBlendFuncSeparate

static inline void cached_glBlendFunc(GLenum sfactor, GLenum dfactor) {
    cached_glBlendFuncSeparate(sfactor, dfactor, sfactor, dfactor);
}
#undef glBlendFunc
#define glBlendFunc cached_glBlendFunc

static inline void cached_glBlendEquationSeparate(GLenum eqRGB, GLenum eqAlpha) {
    if (blendEqRGB == eqRGB && blendEqAlpha == eqAlpha) return;
    glBlendEquationSeparate(eqRGB, eqAlpha);
    blendEqRGB = eqRGB;
    blendEqAlpha = eqAlpha;
}
#undef glBlendEquationSeparate
#define glBlendEquationSeparate cached_glBlendEquationSeparate

static inline void cached_glBlendEquation(GLenum mode) {
    cached_glBlendEquationSeparate(mode, mode);
}
#undef glBlendEquation
#define glBlendEquation cached_glBlendEquation

static inline void cached_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    if (clearColor[0] == r && clearColor[1] == g && clearColor[2] == b && clearColor[3] == a) return;
    glClearColor(r, g, b, a);
    clearColor[0] = r;
    clearColor[1] = g;
    clearColor[2] = b;
    clearColor[3] = a;
}

#undef glClearColor
#define glClearColor cached_glClearColor

static inline void cached_glEnable(GLenum cap) {
    switch (cap) {
        case GL_BLEND:
            if (blend) return;
            blend = GL_TRUE;
            break;
        case GL_DEPTH_TEST: {
            if (depthTest) return;
            depthTest = GL_TRUE;
            break;
        }
        case GL_SCISSOR_TEST: {
            if (scissorTest) return;
            scissorTest = GL_TRUE;
            break;
        }
        case GL_TEXTURE_2D: {
            int slotIndex = (int)activeTextureSlot - GL_TEXTURE0;
            if (slotIndex >= 0 && slotIndex < GL_MAX_TEXTURE_2D) {
                if (texture2D[slotIndex]) return;
                texture2D[slotIndex] = GL_TRUE;
            }
            break;
        }
    }
    glEnable(cap);
}

#undef glEnable
#define glEnable cached_glEnable

static inline void cached_glDisable(GLenum cap) {
    switch (cap) {
        case GL_BLEND:
            if (!blend) return;
            blend = GL_FALSE;
            break;
        case GL_DEPTH_TEST:
            if (!depthTest) return;
            depthTest = GL_FALSE;
            break;
        case GL_SCISSOR_TEST:
            if (!scissorTest) return;
            scissorTest = GL_FALSE;
            break;
        case GL_TEXTURE_2D: {
            int slotIndex = (int)activeTextureSlot - GL_TEXTURE0;
            if (slotIndex >= 0 && slotIndex < GL_MAX_TEXTURE_2D) {
                if (!texture2D[slotIndex]) return;
                texture2D[slotIndex] = GL_FALSE;
            }
            break;
        }
    }
    glDisable(cap);
}

#undef glDisable
#define glDisable cached_glDisable

static inline GLboolean cached_glIsEnabled(GLenum cap) {
    switch (cap) {
        case GL_BLEND:
            return blend;
        case GL_DEPTH_TEST:
            return depthTest;
        case GL_SCISSOR_TEST:
            return scissorTest;
        case GL_TEXTURE_2D: {
            int slotIndex = (int)activeTextureSlot - GL_TEXTURE0;
            if (slotIndex >= 0 && slotIndex < GL_MAX_TEXTURE_2D) {
                return texture2D[slotIndex];
            }
            return glIsEnabled(cap);
        }
        default:
            return glIsEnabled(cap);
    }
}

#undef glIsEnabled
#define glIsEnabled cached_glIsEnabled

#endif // _BS_GL_STATE_H
