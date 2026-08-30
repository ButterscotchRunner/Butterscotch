#ifndef _BS_GL_STATE_H
#define _BS_GL_STATE_H

#define GL_MAX_TEXTURE_2D 32

typedef struct {
    GLenum activeTextureSlot;
    GLenum blendSrcRGB;
    GLenum blendDstRGB;
    GLenum blendSrcAlpha;
    GLenum blendDstAlpha;
    GLenum blendEqRGB;
    GLenum blendEqAlpha;
    GLclampf clearColor[4];
    GLint viewport[4];
    GLint scissor[4];
    GLuint currentReadFb;
    GLuint currentDrawFb;
    GLuint currentProgram;
    GLuint currentVAO;
    GLuint boundTextures2D[GL_MAX_TEXTURE_2D];
    GLboolean blend;
    GLboolean depthTest;
    GLboolean scissorTest;
    GLboolean texture2D[GL_MAX_TEXTURE_2D];
} GLStateCache;

static GLStateCache glState;
static GLboolean glStateInitialized = GL_FALSE;

static inline void GLState_init(void) {    
    glState.activeTextureSlot = GL_TEXTURE0;
    glState.blendSrcRGB       = GL_ONE;
    glState.blendDstRGB       = GL_ZERO;
    glState.blendSrcAlpha     = GL_ONE;
    glState.blendDstAlpha     = GL_ZERO;
    glState.blendEqRGB        = GL_FUNC_ADD;
    glState.blendEqAlpha      = GL_FUNC_ADD;
    
    glState.clearColor[0] = -1.0f; glState.clearColor[1] = -1.0f; 
    glState.clearColor[2] = -1.0f; glState.clearColor[3] = -1.0f;
    
    glState.viewport[0] = -1; glState.viewport[1] = -1; 
    glState.viewport[2] = -1; glState.viewport[3] = -1;
    
    glState.scissor[0]  = -1; glState.scissor[1]  = -1; 
    glState.scissor[2]  = -1; glState.scissor[3]  = -1;
    
    glState.currentReadFb  = 0;
    glState.currentDrawFb  = 0;
    glState.currentProgram = 0;
    glState.currentVAO     = 0xFFFFFFFF;
    
    glState.blend       = GL_FALSE;
    glState.depthTest   = GL_FALSE;
    glState.scissorTest = GL_FALSE;
    
    for (int i = 0; i < GL_MAX_TEXTURE_2D; i++) {
        glState.boundTextures2D[i] = 0;
        glState.texture2D[i]       = GL_FALSE;
    }
    
    glStateInitialized = GL_TRUE;
}

static inline void cached_glViewport(int x, int y, int width, int height) {
    if (glState.viewport[0] == x && glState.viewport[1] == y && glState.viewport[2] == width && glState.viewport[3] == height) return;
    glViewport(x, y, width, height);
    glState.viewport[0] = x;
    glState.viewport[1] = y;
    glState.viewport[2] = width;
    glState.viewport[3] = height;
}
#undef glViewport
#define glViewport cached_glViewport

static inline void cached_glScissor(int x, int y, int width, int height) {
    if (glState.scissor[0] == x && glState.scissor[1] == y && glState.scissor[2] == width && glState.scissor[3] == height) return;
    glScissor(x, y, width, height);
    glState.scissor[0] = x;
    glState.scissor[1] = y;
    glState.scissor[2] = width;
    glState.scissor[3] = height;
}
#undef glScissor
#define glScissor cached_glScissor

static inline void cached_glBindFramebuffer(GLenum type, GLuint framebuffer) {
    switch (type) {
        case GL_READ_FRAMEBUFFER:
            if (glState.currentReadFb == framebuffer) return;
            glState.currentReadFb = framebuffer;
            break;
        case GL_DRAW_FRAMEBUFFER:
            if (glState.currentDrawFb == framebuffer) return;
            glState.currentDrawFb = framebuffer;
            break;
        case GL_FRAMEBUFFER:
            if (glState.currentReadFb == framebuffer && glState.currentDrawFb == framebuffer) return;
            glState.currentReadFb = framebuffer;
            glState.currentDrawFb = framebuffer;
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
            params[0] = glState.viewport[0];
            params[1] = glState.viewport[1];
            params[2] = glState.viewport[2];
            params[3] = glState.viewport[3];
            return;
        case GL_FRAMEBUFFER_BINDING:
            params[0] = (GLint)glState.currentDrawFb;
            return;
        case GL_READ_FRAMEBUFFER_BINDING:
            params[0] = (GLint)glState.currentReadFb;
            return;
        default:
            glGetIntegerv(pname, params);
            break;
    }
}
#undef glGetIntegerv
#define glGetIntegerv cached_glGetIntegerv

static inline void cached_glActiveTexture(GLenum texture) {
    if (glState.activeTextureSlot == texture) return;
    glActiveTexture(texture);
    glState.activeTextureSlot = texture;
}
#undef glActiveTexture
#define glActiveTexture cached_glActiveTexture

static inline void cached_glBindTexture(GLenum target, GLuint texture) {
    if (target == GL_TEXTURE_2D) {
        int slotIndex = glState.activeTextureSlot - GL_TEXTURE0;
        if (slotIndex >= 0 && slotIndex < GL_MAX_TEXTURE_2D) {
            if (glState.boundTextures2D[slotIndex] == texture) return;
            glState.boundTextures2D[slotIndex] = texture;
        }
    }
    glBindTexture(target, texture);
}
#undef glBindTexture
#define glBindTexture cached_glBindTexture

static inline void cached_glUseProgram(GLuint program) {
    if (glState.currentProgram == program) return;
    glUseProgram(program);
    glState.currentProgram = program;
}
#undef glUseProgram
#define glUseProgram cached_glUseProgram

static inline void cached_glBindVertexArray(GLuint vao) {
    if (glState.currentVAO == vao) return;
    glBindVertexArray(vao);
    glState.currentVAO = vao;
}
#undef glBindVertexArray
#define glBindVertexArray cached_glBindVertexArray

static inline void cached_glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    for (GLsizei i = 0; i < n; i++) {
        if (glState.currentVAO == arrays[i]) glState.currentVAO = 0;
    }
    glDeleteVertexArrays(n, arrays);
}
#undef glDeleteVertexArrays
#define glDeleteVertexArrays cached_glDeleteVertexArrays

static inline void cached_glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
    if (glState.blendSrcRGB == srcRGB && glState.blendDstRGB == dstRGB && 
        glState.blendSrcAlpha == srcAlpha && glState.blendDstAlpha == dstAlpha) return;
    
    glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
    
    glState.blendSrcRGB   = srcRGB;
    glState.blendDstRGB   = dstRGB;
    glState.blendSrcAlpha = srcAlpha;
    glState.blendDstAlpha = dstAlpha;
}
#undef glBlendFuncSeparate
#define glBlendFuncSeparate cached_glBlendFuncSeparate

static inline void cached_glBlendFunc(GLenum sfactor, GLenum dfactor) {
    cached_glBlendFuncSeparate(sfactor, dfactor, sfactor, dfactor);
}
#undef glBlendFunc
#define glBlendFunc cached_glBlendFunc

static inline void cached_glBlendEquationSeparate(GLenum eqRGB, GLenum eqAlpha) {
    if (glState.blendEqRGB == eqRGB && glState.blendEqAlpha == eqAlpha) return;
    glBlendEquationSeparate(eqRGB, eqAlpha);
    glState.blendEqRGB = eqRGB;
    glState.blendEqAlpha = eqAlpha;
}
#undef glBlendEquationSeparate
#define glBlendEquationSeparate cached_glBlendEquationSeparate

static inline void cached_glBlendEquation(GLenum mode) {
    cached_glBlendEquationSeparate(mode, mode);
}
#undef glBlendEquation
#define glBlendEquation cached_glBlendEquation

static inline void cached_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    if (glState.clearColor[0] == r && glState.clearColor[1] == g && glState.clearColor[2] == b && glState.clearColor[3] == a) return;
    glClearColor(r, g, b, a);
    glState.clearColor[0] = r;
    glState.clearColor[1] = g;
    glState.clearColor[2] = b;
    glState.clearColor[3] = a;
}
#undef glClearColor
#define glClearColor cached_glClearColor

static inline void cached_glEnable(GLenum cap) {
    switch (cap) {
        case GL_BLEND:
            if (glState.blend) return;
            glState.blend = GL_TRUE;
            break;
        case GL_DEPTH_TEST:
            if (glState.depthTest) return;
            glState.depthTest = GL_TRUE;
            break;
        case GL_SCISSOR_TEST:
            if (glState.scissorTest) return;
            glState.scissorTest = GL_TRUE;
            break;
        case GL_TEXTURE_2D: {
            int slotIndex = (int)glState.activeTextureSlot - GL_TEXTURE0;
            if (slotIndex >= 0 && slotIndex < GL_MAX_TEXTURE_2D) {
                if (glState.texture2D[slotIndex]) return;
                glState.texture2D[slotIndex] = GL_TRUE;
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
            if (!glState.blend) return;
            glState.blend = GL_FALSE;
            break;
        case GL_DEPTH_TEST:
            if (!glState.depthTest) return;
            glState.depthTest = GL_FALSE;
            break;
        case GL_SCISSOR_TEST:
            if (!glState.scissorTest) return;
            glState.scissorTest = GL_FALSE;
            break;
        case GL_TEXTURE_2D: {
            int slotIndex = (int)glState.activeTextureSlot - GL_TEXTURE0;
            if (slotIndex >= 0 && slotIndex < GL_MAX_TEXTURE_2D) {
                if (!glState.texture2D[slotIndex]) return;
                glState.texture2D[slotIndex] = GL_FALSE;
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
        case GL_BLEND:        return glState.blend;
        case GL_DEPTH_TEST:   return glState.depthTest;
        case GL_SCISSOR_TEST: return glState.scissorTest;
        case GL_TEXTURE_2D: {
            int slotIndex = (int)glState.activeTextureSlot - GL_TEXTURE0;
            if (slotIndex >= 0 && slotIndex < GL_MAX_TEXTURE_2D) {
                return glState.texture2D[slotIndex];
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
