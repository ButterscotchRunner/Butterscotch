#pragma once

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
    int32_t gameW; // game resolution (for FBO sizing)
    int32_t gameH;

    // Original counts from data.win (dynamic slots start at these indices)
    uint32_t originalTexturePageCount;
    uint32_t originalTpagCount;
    uint32_t originalSpriteCount;

#ifndef PLATFORM_PS3
    // FBO for render-to-texture (game renders here, then blitted to screen)
    GLuint fbo;
    GLuint fboTexture;
    int32_t fboWidth;
    int32_t fboHeight;

    // GML surfaces (each is an FBO with a backing color texture)
    GLuint* surfaces;
    GLuint* surfaceTexture;
    int32_t* surfaceWidth;
    int32_t* surfaceHeight;
    uint32_t surfaceCount;
#endif
} GLLegacyRenderer;

Renderer* GLLegacyRenderer_create(void);
