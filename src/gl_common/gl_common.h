#pragma once

#include "common.h"
#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#elif PLATFORM_PS3
#include "ps3gl.h"
#include "rsxutil.h"
#else
#include <glad/glad.h>
#endif

// ===[ Main FBO ]===

// Allocates or recreates the main render-target FBO at (width, height).
// On entry *fbo must already be a valid glGenFramebuffers handle.
// The texture at *fboTexture is recreated each call (or allocated if 0).
// Leaves the FBO bound on return.
void GLCommon_resizeMainFBO(GLuint* fboTexture, GLuint fbo, int32_t* fboWidth, int32_t* fboHeight, int32_t width, int32_t height);

// Computes the letterboxed destination rect for a gameW x gameH frame inside a windowW x windowH window.
// All four outputs are pixel coordinates with (0,0) at the bottom-left of the window (OpenGL convention).
void GLCommon_computeLetterbox(int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH, int32_t* outStartX, int32_t* outStartY, int32_t* outEndX, int32_t* outEndY);

// Blits the main FBO to the default framebuffer with letterboxing.
void GLCommon_beginLetterboxBlit(GLuint fbo);
void GLCommon_endLetterboxBlit(int32_t fboWidth, int32_t fboHeight, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH);

// ===[ Surface arrays ]===

// Returns a free slot index, growing the surfaces arrays if all slots are in use.
// The newly returned slot has surfaces[i] == 0 and all dimensions zeroed.
uint32_t GLCommon_findOrAllocateSurfaceSlot(GLuint** surfaces, GLuint** surfaceTexture, int32_t** surfaceWidth, int32_t** surfaceHeight, uint32_t* count);

// Blits a region between two surface FBOs (or the main FBO when id == -1).
// If part == false, ignores src{X,Y,W,H} and copies the whole source to a matching-size box at (dstX, dstY) on the destination.
// If part == true, copies a src{W,H}-sized region starting at (srcX, srcY) on the source (top-down GML coords) to (dstX, dstY) on the destination.
// Silently returns if either id is invalid.
void GLCommon_surfaceBlit(GLuint mainFbo, int32_t mainFboWidth, int32_t mainFboHeight, GLuint* surfaces, int32_t* surfaceWidth, int32_t* surfaceHeight, uint32_t count, int32_t dstId, int32_t dstX, int32_t dstY, int32_t srcId, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, bool part);

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
