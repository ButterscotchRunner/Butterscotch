#ifndef _BS_GL_LEGACY_RENDERER_H_
#define _BS_GL_LEGACY_RENDERER_H_

#include "common.h"
#include "renderer.h"
#include "runner.h"
#ifdef PLATFORM_PS3
#include "ps3gl.h"
#include "rsxutil.h"
#else
#include <glad/glad.h>
#endif

// ===[ GLLegacyRenderer Struct ]===
// Exposed in the header so platform-specific code (main.c) can access FBO fields for screenshots.
typedef struct {
    Renderer base; // Must be first field for struct embedding

    GLuint* glTextures;       // one GL texture per TXTR page
    int32_t* textureWidths;   // needed for UV normalization
    int32_t* textureHeights;
    bool* textureLoaded;      // lazy loading: true once PNG decoded and uploaded
    uint32_t textureCount;

    GLuint whiteTexture; // 1x1 white pixel for drawing primitives (rectangles, lines, etc.)

    int32_t windowW; // stored from beginFrame for endFrame blit
    int32_t windowH;
    int32_t gameW; // game width (matches the application_surface size)
    int32_t gameH; // game height (matches the application_surface size)

    // Original counts from data.win (dynamic slots start at these indices)
    uint32_t originalTexturePageCount;
    uint32_t originalTpagCount;
    uint32_t originalSpriteCount;

    bool colorWriteR, colorWriteG, colorWriteB, colorWriteA;

    // GML surfaces (each is an FBO with a backing color texture)
    GLuint* surfaces;
    GLuint* surfaceTexture;
    int32_t* surfaceWidth;
    int32_t* surfaceHeight;
    uint32_t surfaceCount;

    // True if the GPU doesn't support NPOT textures (GL < 2.0), requiring
    // FBO color-attachment textures to have power-of-two dimensions.
    bool needsPOT;

    // Blending mode + factors
    int32_t currentBlendMode;
    int32_t currentSFactor;
    int32_t currentDFactor;
    int32_t currentSFactorAlpha;
    int32_t currentDFactorAlpha;
} GLLegacyRenderer;

bool GLLegacyRenderer_ensureTextureLoaded(GLLegacyRenderer* gl, uint32_t pageId);
Renderer* GLLegacyRenderer_create(void);

#endif /* _BS_GL_LEGACY_RENDERER_H_ */
