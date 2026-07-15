#ifndef _BS_GL_COMMON_H_
#define _BS_GL_COMMON_H_

#include "common.h"
#include <stdint.h>

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#include <GLES3/gl3.h>
#elif PLATFORM_PS3
#include "ps3gl.h"
#include "rsxutil.h"
#else
#include <glad/glad.h>
#endif

// ===[ Letterbox blit ]===

// Computes the letterboxed destination rect for a gameW x gameH frame inside a windowW x windowH window.
// All four outputs are pixel coordinates with (0,0) at the bottom-left of the window (OpenGL convention).
void GLCommon_computeLetterbox(int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH, int32_t* outStartX, int32_t* outStartY, int32_t* outEndX, int32_t* outEndY);

// Blits the given FBO (typically the application_surface) into hostFbo with letterboxing (hostFbo 0 == the window).
void GLCommon_beginLetterboxBlit(GLuint fbo, GLuint hostFbo);
void GLCommon_endLetterboxBlit(int32_t fboWidth, int32_t fboHeight, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH, GLuint hostFbo);

// ===[ Surface arrays ]===

// Texture handle flag distinguishing a surface texture (surface_get_texture) from a sprite (tpag+1) handle.
// Encoded as (GL_SURFACE_TEXTURE_FLAG | surfaceID); tpag counts never approach this, so the two can't collide.
#define GL_SURFACE_TEXTURE_FLAG 0x40000000u

// Returns a free slot index, growing the surfaces arrays if all slots are in use.
// The newly returned slot has surfaces[i] == 0 and all dimensions zeroed.
uint32_t GLCommon_findOrAllocateSurfaceSlot(GLuint** surfaces, GLuint** surfaceTexture, int32_t** surfaceWidth, int32_t** surfaceHeight, uint32_t* count);

// Blits a region between two surface FBOs.
// If part == false, ignores src{X,Y,W,H} and copies the whole source to a matching-size box at (dstX, dstY) on the destination.
// If part == true, copies a src{W,H}-sized region starting at (srcX, srcY) on the source (top-down GML coords) to (dstX, dstY) on the destination.
// Silently returns if either id is invalid.
void GLCommon_surfaceBlit(GLuint* surfaces, int32_t* surfaceWidth, int32_t* surfaceHeight, uint32_t count, int32_t dstId, int32_t dstX, int32_t dstY, int32_t srcId, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, bool part);

// Reads a surface's pixels into a top-down RGBA8 buffer of size width*height*4.
// Returns false on invalid surfaceId.
// Saves/restores GL_FRAMEBUFFER_BINDING and GL_PACK_ALIGNMENT around the read.
bool GLCommon_surfaceGetPixels(GLuint* surfaces, int32_t* surfaceWidth, int32_t* surfaceHeight, uint32_t count, int32_t surfaceId, uint8_t* outRGBA);

// ===[ Blend mode translation ]===

// Maps a bm_* factor constant (bm_zero, bm_src_alpha, etc.) to a GL blend factor.
GLenum GLCommon_blendFactorToGL(int factor);

// Maps a bm_* mode constant (bm_normal, bm_add, bm_subtract, ...) to a GL blend equation.
GLenum GLCommon_blendModeToEquation(int mode);

// Maps a bm_* mode constant to its conventional source blend factor.
GLenum GLCommon_blendModeToSFactor(int mode);

// Maps a bm_* mode constant to its conventional destination blend factor.
GLenum GLCommon_blendModeToDFactor(int mode);

#ifndef PLATFORM_PS3

// ===[ GL version queries ]===

typedef struct {
    int major;
    int minor;
    bool isGLES;
} GLVer;

// Returns the parsed GL version by reading glGetString(GL_VERSION).
GLVer GLCommon_getGLVersion(void);

#endif

#endif /* _BS_GL_COMMON_H_ */
