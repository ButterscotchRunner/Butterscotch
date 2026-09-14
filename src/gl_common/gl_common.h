#ifndef _BS_GL_COMMON_H_
#define _BS_GL_COMMON_H_

#include "common.h"
#include "renderer.h"
#include <stdint.h>
#include "data_win.h"
#include "debug_font/debug_font.h"

struct GLRenderer;
typedef struct GLRenderer GLRenderer;

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__) || defined(__SWITCH__)
#include <GLES3/gl3.h>
#elif PLATFORM_PS3
#include "ps3gl.h"
#include "rsxutil.h"
#elif PLATFORM_VITA
#include <vitaGL.h>
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

// Utils

static inline uint8_t floatToUnormByte(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

// Primitives and vertices

typedef struct {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a;
} GlVertex;

typedef struct {
    int32_t type;
    int32_t vertexCount;
    GLuint textureId;
    bool hasTexture;
} GlPrimitive;

void GlPrimitive_reset(GlPrimitive* primitive);

void GLCommon_primitiveBegin(GlPrimitive* primitive, int32_t type, int32_t textureId);
void GLCommon_primitiveBeginTexture(GLRenderer* gl, int32_t primitiveType, GLuint resolvedTexture);
bool GLCommon_primitivePrepare(
    GlPrimitive* primitive, GLuint whiteTexture,
    GLenum* mode, GLuint* textureId
);
void GLCommon_drawVertex(
    GLRenderer* gl,
    float x, float y, float z,
    uint32_t color, float alpha,
    float u, float v
);

// ===[ Debug UI font (drawTextUI) ]===

// Embedded debug-font state backing drawTextUI. Embedded in each GL renderer
// struct (modern + legacy) so drawTextUI needs no game fonts and no static
// globals. The atlas texture is uploaded lazily on first use.
typedef struct {
    Font font;
    FontGlyph glyphs[DEBUGFONT_GLYPH_COUNT];
    TexturePageItem tpag;
    bool initialized;
    GLuint texture; // 0 = not uploaded yet
} GLDebugUIFont;

// Builds the synthetic Font from the embedded atlas. Idempotent.
void GLCommon_initDebugUIFont(GLDebugUIFont* ui);

// Uploads the atlas texture if not yet uploaded. Returns false on failure.
bool GLCommon_ensureDebugFontTexture(GLDebugUIFont* ui);

// Deletes the atlas texture if uploaded (safe to call when texture == 0).
void GLCommon_deleteDebugFontTexture(GLDebugUIFont* ui);

// Y-adjust to add to a glyph's local Y when it belongs to the renderer's
// embedded debug UI font (the debug atlas has per-glyph yoffsets, GameMaker
// fonts don't). Returns 0 for regular game fonts.
static inline float GLCommon_debugUIFontYOffset(GLDebugUIFont* ui, Font* font, FontGlyph* glyph) {
    if (font == &ui->font && DEBUGFONT_FIRST_CP <= glyph->character && glyph->character <= DEBUGFONT_LAST_CP)
        return (float) debugFontGlyphs[glyph->character - DEBUGFONT_FIRST_CP].yoffset;
    return 0.0f;
}

// Common GL Renderer struct

enum GlMode {
    GL_MODE_LEGACY = 0,
    GL_MODE_MODERN = 1
};

struct GLRenderer {
    Renderer base; // Must be first field for struct embedding
    enum GlMode glMode;

    GlVertex* vertexData; // MAX_QUADS * VERTICES_PER_QUAD vertices
    GlPrimitive currentPrimitive;

    GLuint* glTextures;       // one GL texture per TXTR page
    int32_t* textureWidths;   // needed for UV normalization
    int32_t* textureHeights;
    bool* textureLoaded;      // lazy loading: true once PNG decoded and uploaded
    uint32_t textureCount;

    GLuint whiteTexture; // 1x1 white pixel for drawing primitives (rectangles, lines, etc.)

    // Embedded debug UI font backing drawTextUI (see gl_common.h).
    GLDebugUIFont debugUI;

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

    // Blending mode + factors
    bool blendEnable;
    int32_t currentBlendMode;
    int32_t currentSFactor;
    int32_t currentDFactor;
    int32_t currentSFactorAlpha;
    int32_t currentDFactorAlpha;

    bool alphaTestEnable;
    float alphaTestRef;
};

#endif /* _BS_GL_COMMON_H_ */
