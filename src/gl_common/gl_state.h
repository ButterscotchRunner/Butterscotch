#ifndef _BS_GL_STATE_H
#define _BS_GL_STATE_H

static GLenum activeTextureSlot = GL_TEXTURE0;
static GLint viewport[4];
static GLint scissor[4];
static GLuint currentReadFb = 0;
static GLuint currentDrawFb = 0;
static GLuint boundTextures2D[32];
static GLboolean blend = GL_FALSE;
static GLboolean depthTest = GL_FALSE;
static GLboolean scissorTest = GL_FALSE;
static GLboolean texture2D = GL_FALSE;

static void cached_glViewport(int x, int y, int width, int height) {
    if (viewport[0] == x && viewport[1] == y && viewport[2] == width && viewport[3] == height) return;
    glViewport(x, y, width, height);
    viewport[0] = x;
    viewport[1] = y;
    viewport[2] = width;
    viewport[3] = height;
}
#undef glViewport
#define glViewport cached_glViewport

static void cached_glScissor(int x, int y, int width, int height) {
    if (scissor[0] == x && scissor[1] == y && scissor[2] == width && scissor[3] == height) return;
    glScissor(x, y, width, height);
    scissor[0] = x;
    scissor[1] = y;
    scissor[2] = width;
    scissor[3] = height;
}
#undef glScissor
#define glScissor cached_glScissor

static void cached_glBindFramebuffer(GLenum type, GLuint framebuffer) {
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

static void cached_glGetIntegerv(GLenum pname, GLint* params) {
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

static void cached_glActiveTexture(GLenum texture) {
    if (activeTextureSlot == texture) return;
    glActiveTexture(texture);
    activeTextureSlot = texture;
}

#undef glActiveTexture
#define glActiveTexture cached_glActiveTexture

static void cached_glBindTexture(GLenum target, GLuint texture) {
    if (target == GL_TEXTURE_2D) {
        int slot_idx = activeTextureSlot - GL_TEXTURE0;
        if (boundTextures2D[slot_idx] == texture) return;
        boundTextures2D[slot_idx] = texture;
    }
    glBindTexture(target, texture);
}

#undef glBindTexture
#define glBindTexture cached_glBindTexture

static void cached_glEnable(GLenum cap) {
    switch (cap) {
        case GL_BLEND:
            if (blend) return;
            blend = GL_TRUE;
            break;
        case GL_DEPTH_TEST:
            if (depthTest) return;
            depthTest = GL_TRUE;
            break;
        case GL_SCISSOR_TEST:
            if (scissorTest) return;
            scissorTest = GL_TRUE;
            break;
        case GL_TEXTURE_2D:
            if (texture2D) return;
            texture2D = GL_TRUE;
            break;
    }
    glEnable(cap);
}

#undef glEnable
#define glEnable cached_glEnable

static void cached_glDisable(GLenum cap) {
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
        case GL_TEXTURE_2D:
            if (!texture2D) return;
            texture2D = GL_FALSE;
            break;
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
        case GL_TEXTURE_2D:
            return texture2D;
        default: 
            return glIsEnabled(cap);
    }
}

#undef glIsEnabled
#define glIsEnabled cached_glIsEnabled

#endif // _BS_GL_STATE_H
