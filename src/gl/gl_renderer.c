#include "gl_renderer.h"
#include "matrix_math.h"
#include "text_utils.h"

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <glad/glad.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "stb_image.h"
#include "stb_ds.h"
#include "utils.h"
#include "image_decoder.h"
#include "gl_common.h"

// ===[ Constants ]===
#define MAX_QUADS 4096
#define FLOATS_PER_VERTEX 8  // x, y, u, v, r, g, b, a
#define VERTICES_PER_QUAD 4
#define INDICES_PER_QUAD 6

// ===[ Shader Sources ]===
#ifdef ENABLE_GLES
    #define GLSL_VERSION_DIRECTIVE "#version 300 es\n"
    #define GLSL_VERTEX_PRECISION  "precision highp float;\n"
    #define GLSL_FRAGMENT_PRECISION "precision mediump float;\n"
#else
    #define GLSL_VERSION_DIRECTIVE "#version 410 core\n"
    #define GLSL_VERTEX_PRECISION  ""
    #define GLSL_FRAGMENT_PRECISION ""
#endif

static const char* vertexShaderSource =
    GLSL_VERSION_DIRECTIVE
    GLSL_VERTEX_PRECISION
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec2 aTexCoord;\n"
    "layout(location = 2) in vec4 aColor;\n"
    "uniform mat4 uProjection;\n"
    "out vec2 vTexCoord;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);\n"
    "    vTexCoord = aTexCoord;\n"
    "    vColor = aColor;\n"
    "}\n";

static const char* fragmentShaderSource =
    GLSL_VERSION_DIRECTIVE
    GLSL_FRAGMENT_PRECISION
    "in vec2 vTexCoord;\n"
    "in vec4 vColor;\n"
    "uniform sampler2D uTexture;\n"
    "uniform float uAlphaTestRef;\n" // negative = disabled
    "uniform vec4 uFogColor;\n" // rgb = fog color, a = enable flag (0 or 1)
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 c = texture(uTexture, vTexCoord) * vColor;\n"
    "    if (uAlphaTestRef >= c.a) discard;\n"
    "    c.rgb = mix(c.rgb, uFogColor.rgb, uFogColor.a);\n"
    "    fragColor = c;\n"
    "}\n";


// ===[ Shader Compilation ]===

static GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        fprintf(stderr, "GL: Shader compilation failed: %s\n", infoLog);
        abort();
    }
    return shader;
}

static GLuint linkProgram(GLuint vertShader, GLuint fragShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        fprintf(stderr, "GL: Shader linking failed: %s\n", infoLog);
        abort();
    }
    return program;
}

// ===[ Batch Flush ]===

static void flushBatch(GLRenderer* gl) {
    if (gl->quadCount == 0) return;

    int32_t vertexCount = gl->quadCount * VERTICES_PER_QUAD;
    int32_t indexCount = gl->quadCount * INDICES_PER_QUAD;

    // Bind the VAO so the EBO binding it carries is what glDrawElements uses.
    // Without this, glDrawElements would treat the nullptr indices arg as a literal pointer to client memory and SEGV inside the driver during async upload.
    glBindVertexArray(gl->vao);

    glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexCount * FLOATS_PER_VERTEX * sizeof(float), gl->vertexData);

    glBindTexture(GL_TEXTURE_2D, gl->currentTextureId);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);

    gl->quadCount = 0;
}

// ===[ Vtable Implementations ]===

static void glInit(Renderer* renderer, DataWin* dataWin) {
    GLRenderer* gl = (GLRenderer*) renderer;
    renderer->dataWin = dataWin;

    // Compile shaders
    GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    gl->shaderProgram = linkProgram(vertShader, fragShader);
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    gl->uProjection = glGetUniformLocation(gl->shaderProgram, "uProjection");
    gl->uTexture = glGetUniformLocation(gl->shaderProgram, "uTexture");
    gl->uAlphaTestRef = glGetUniformLocation(gl->shaderProgram, "uAlphaTestRef");
    gl->uFogColor = glGetUniformLocation(gl->shaderProgram, "uFogColor");
    gl->alphaTestEnable = false;
    gl->alphaTestRef = 0.0f;
    gl->fogEnable = false;
    gl->fogColor = 0;
    glUseProgram(gl->shaderProgram);
    glUniform1f(gl->uAlphaTestRef, -1.0f);
    glUniform4f(gl->uFogColor, 0.0f, 0.0f, 0.0f, 0.0f);

    // Create VAO/VBO/EBO
    glGenVertexArrays(1, &gl->vao);
    glGenBuffers(1, &gl->vbo);
    glGenBuffers(1, &gl->ebo);

    glBindVertexArray(gl->vao);

    // VBO: sized for max quads
    int32_t vboSize = MAX_QUADS * VERTICES_PER_QUAD * FLOATS_PER_VERTEX * (int32_t) sizeof(float);
    glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
    glBufferData(GL_ARRAY_BUFFER, vboSize, nullptr, GL_DYNAMIC_DRAW);

    // EBO: pre-fill with quad index pattern (0,1,2,2,3,0 repeated)
    int32_t eboSize = MAX_QUADS * INDICES_PER_QUAD * (int32_t) sizeof(uint32_t);
    uint32_t* indices = safeMalloc(eboSize);
    for (int32_t i = 0; MAX_QUADS > i; i++) {
        uint32_t base = (uint32_t) i * 4;
        indices[i * 6 + 0] = base + 0;
        indices[i * 6 + 1] = base + 1;
        indices[i * 6 + 2] = base + 2;
        indices[i * 6 + 3] = base + 2;
        indices[i * 6 + 4] = base + 3;
        indices[i * 6 + 5] = base + 0;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, eboSize, indices, GL_STATIC_DRAW);
    free(indices);

    // Vertex attributes: pos(2f), texcoord(2f), color(4f)
    int32_t stride = FLOATS_PER_VERTEX * (int32_t) sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*) 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*) (2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*) (4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // Allocate CPU-side vertex buffer
    gl->vertexData = safeMalloc(MAX_QUADS * VERTICES_PER_QUAD * FLOATS_PER_VERTEX * sizeof(float));

    // Prepare texture slots for lazy loading (PNG decode deferred to first use)
    gl->textureCount = dataWin->txtr.count;
    gl->glTextures = safeMalloc(gl->textureCount * sizeof(GLuint));
    gl->textureWidths = safeMalloc(gl->textureCount * sizeof(int32_t));
    gl->textureHeights = safeMalloc(gl->textureCount * sizeof(int32_t));
    gl->textureLoaded = safeMalloc(gl->textureCount * sizeof(bool));

    glGenTextures((GLsizei) gl->textureCount, gl->glTextures);

    for (uint32_t i = 0; gl->textureCount > i; i++) {
        gl->textureWidths[i] = 0;
        gl->textureHeights[i] = 0;
        gl->textureLoaded[i] = false;
    }

    // Create 1x1 white pixel texture for primitive drawing (rectangles, lines, etc.)
    glGenTextures(1, &gl->whiteTexture);
    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);
    uint8_t whitePixel[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); //I believe the old way this was done was wrong
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gl->quadCount = 0;
    gl->currentTextureId = 0;

    // Create FBO (texture will be allocated/resized in beginFrame)
    glGenFramebuffers(1, &gl->fbo);
    gl->fboTexture = 0;
    gl->fboWidth = 0;
    gl->fboHeight = 0;

    // Save original counts so we know which slots are from data.win vs dynamic
    gl->originalTexturePageCount = gl->textureCount;
    gl->originalTpagCount = dataWin->tpag.count;
    gl->originalSpriteCount = dataWin->sprt.count;

    fprintf(stderr, "GL: Renderer initialized (%u texture pages)\n", gl->textureCount);
}

static void glDestroy(Renderer* renderer) {
    GLRenderer* gl = (GLRenderer*) renderer;

    if (gl->fboTexture != 0) glDeleteTextures(1, &gl->fboTexture);
    glDeleteFramebuffers(1, &gl->fbo);
    glDeleteTextures(1, &gl->whiteTexture);

    glDeleteTextures((GLsizei) gl->textureCount, gl->glTextures);
    glDeleteProgram(gl->shaderProgram);
    glDeleteVertexArrays(1, &gl->vao);
    glDeleteBuffers(1, &gl->vbo);
    glDeleteBuffers(1, &gl->ebo);

    free(gl->glTextures);
    free(gl->textureWidths);
    free(gl->textureHeights);
    free(gl->textureLoaded);
    free(gl->vertexData);
    free(gl);
}

static void glBeginFrame(Renderer* renderer, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH) {
    GLRenderer* gl = (GLRenderer*) renderer;

    gl->quadCount = 0;
    gl->currentTextureId = 0;
    gl->windowW = windowW;
    gl->windowH = windowH;
    gl->gameW = gameW;
    gl->gameH = gameH;

    if (gameW != gl->fboWidth || gameH != gl->fboHeight) {
        GLCommon_resizeMainFBO(&gl->fboTexture, gl->fbo, &gl->fboWidth, &gl->fboHeight, gameW, gameH);
    }

    // Bind FBO and clear
    glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
    glViewport(0, 0, gameW, gameH);
    gl->base.CPortX = 0;
    gl->base.CPortY = 0;
    gl->base.CPortW = gameW;
    gl->base.CPortH = gameH;
}

static void glBeginView(Renderer* renderer, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, float viewAngle) {
    GLRenderer* gl = (GLRenderer*) renderer;

    gl->quadCount = 0;
    gl->currentTextureId = 0;

    // Set viewport and scissor to the port rectangle within the FBO
    // FBO uses game resolution, port coordinates are in game space
    // OpenGL viewport Y is bottom-up, game Y is top-down
    int32_t glPortY = gl->gameH - portY - portH;
    glViewport(portX, glPortY, portW, portH);

    gl->base.CPortX = portX;
    gl->base.CPortY = glPortY;
    gl->base.CPortW = portW;
    gl->base.CPortH = portH;

    glEnable(GL_SCISSOR_TEST);
    glScissor(portX, glPortY, portW, portH);

    // Build orthographic projection (Y-down for GML coordinate system)
    Matrix4f projection;
    Matrix4f_identity(&projection);
    Matrix4f_ortho(&projection, (float) viewX, (float) (viewX + viewW), (float) (viewY + viewH), (float) viewY, -1.0f, 1.0f);

    if (viewAngle != 0.0f) {
        // GML view_angle: rotate camera by this angle (degrees, counter-clockwise)
        // To rotate the camera, we rotate the world in the opposite direction around the view center
        float cx = (float) viewX + (float) viewW / 2.0f;
        float cy = (float) viewY + (float) viewH / 2.0f;
        Matrix4f rot;
        Matrix4f_identity(&rot);
        Matrix4f_translate(&rot, cx, cy, 0.0f);
        float angleRad = viewAngle * (float) M_PI / 180.0f;
        Matrix4f_rotateZ(&rot, -angleRad);
        Matrix4f_translate(&rot, -cx, -cy, 0.0f);
        Matrix4f result;
        Matrix4f_multiply(&result, &projection, &rot);
        projection = result;
    }

    glUseProgram(gl->shaderProgram);
    glUniformMatrix4fv(gl->uProjection, 1, GL_FALSE, projection.m);
    glUniform1i(gl->uTexture, 0);
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(gl->vao);
    renderer->PreviousViewMatrix = projection;

}

static void glEndView(Renderer* renderer) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    glDisable(GL_SCISSOR_TEST);
}

static void glBeginGUI(Renderer* renderer, int32_t guiW, int32_t guiH, int32_t portX, int32_t portY, int32_t portW, int32_t portH) {
    GLRenderer* gl = (GLRenderer*) renderer;

    gl->quadCount = 0;
    gl->currentTextureId = 0;

    int32_t glPortY = gl->gameH - portY - portH;
    glViewport(portX, glPortY, portW, portH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(portX, glPortY, portW, portH);

    Matrix4f projection;
    Matrix4f_identity(&projection);
    Matrix4f_ortho(&projection, 0.0f, (float) guiW, (float) guiH, 0.0f, -1.0f, 1.0f);

    glUseProgram(gl->shaderProgram);
    glUniformMatrix4fv(gl->uProjection, 1, GL_FALSE, projection.m);
    glUniform1i(gl->uTexture, 0);
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(gl->vao);
}

static void glEndGUI(Renderer* renderer) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    glDisable(GL_SCISSOR_TEST);
}

static void glEndFrame(Renderer* renderer) {
    GLRenderer* gl = (GLRenderer*) renderer;
    glBindVertexArray(0);
    GLCommon_letterboxBlit(gl->fbo, gl->fboWidth, gl->fboHeight, gl->gameW, gl->gameH, gl->windowW, gl->windowH);
}

static void glRendererFlush(Renderer* renderer) {
    flushBatch((GLRenderer*) renderer);
}

static void glClearScreen(Renderer* renderer, uint32_t color, float alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // GML draw_clear ignores the active scissor and clears the whole target. Disable scissor for the clear and restore it after.
    GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    if (scissorWasEnabled) glDisable(GL_SCISSOR_TEST);
    glClearColor(r, g, b, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
    if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST);
}

// Lazily decodes and uploads a TXTR page on first access.
// Returns true if the texture is ready, false if it failed to decode.
static bool ensureTextureLoaded(GLRenderer* gl, uint32_t pageId) {
    if (gl->textureLoaded[pageId]) return (gl->textureWidths[pageId] != 0);

    gl->textureLoaded[pageId] = true;

    DataWin* dw = gl->base.dataWin;
    Texture* txtr = &dw->txtr.textures[pageId];

    int w, h;
    bool gm2022_5 = DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0);
    uint8_t* pixels = ImageDecoder_decodeToRgba(txtr->blobData, (size_t) txtr->blobSize, gm2022_5, &w, &h);
    if (pixels == nullptr) {
        fprintf(stderr, "GL: Failed to decode TXTR page %u\n", pageId);
        return false;
    }

    gl->textureWidths[pageId] = w;
    gl->textureHeights[pageId] = h;

    glBindTexture(GL_TEXTURE_2D, gl->glTextures[pageId]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(pixels);
    fprintf(stderr, "GL: Loaded TXTR page %u (%dx%d)\n", pageId, w, h);
    return true;
}

// Resolves a TPAG index to a loaded GL texture. Returns false if drawing should be skipped.
static bool resolveSpriteTexture(GLRenderer* gl, int32_t tpagIndex, TexturePageItem** outTpag, GLuint* outTexId, int32_t* outTexW, int32_t* outTexH) {
    DataWin* dw = gl->base.dataWin;
    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return false;
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return false;
    if (!ensureTextureLoaded(gl, (uint32_t) pageId)) return false;
    *outTpag = tpag;
    *outTexId = gl->glTextures[pageId];
    *outTexW = gl->textureWidths[pageId];
    *outTexH = gl->textureHeights[pageId];
    return true;
}

// Emits a single textured quad into the batch given 4 final screen-space corners (TL, TR, BR, BL), 4 UVs forming a rect (u0,v0)-(u1,v1), and a flat color/alpha.
// Handles texture rebinding and batch flushing.
static void emitTexturedQuad(GLRenderer* gl, GLuint texId, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float u0, float v0, float u1, float v1, float r, float g, float b, float alpha) {
    if (gl->quadCount > 0 && gl->currentTextureId != texId) flushBatch(gl);
    if (gl->quadCount >= MAX_QUADS) flushBatch(gl);
    gl->currentTextureId = texId;

    float* verts = gl->vertexData + gl->quadCount * VERTICES_PER_QUAD * FLOATS_PER_VERTEX;

    // Vertex 0: top-left
    verts[0] = x0; verts[1] = y0; verts[2] = u0; verts[3] = v0;
    verts[4] = r;  verts[5] = g;  verts[6] = b;  verts[7] = alpha;

    // Vertex 1: top-right
    verts[8]  = x1; verts[9]  = y1; verts[10] = u1; verts[11] = v0;
    verts[12] = r;  verts[13] = g;  verts[14] = b;  verts[15] = alpha;

    // Vertex 2: bottom-right
    verts[16] = x2; verts[17] = y2; verts[18] = u1; verts[19] = v1;
    verts[20] = r;  verts[21] = g;  verts[22] = b;  verts[23] = alpha;

    // Vertex 3: bottom-left
    verts[24] = x3; verts[25] = y3; verts[26] = u0; verts[27] = v1;
    verts[28] = r;  verts[29] = g;  verts[30] = b;  verts[31] = alpha;

    gl->quadCount++;
}

static void glDrawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;
    TexturePageItem* tpag;
    GLuint texId;
    int32_t texW, texH;
    if (!resolveSpriteTexture(gl, tpagIndex, &tpag, &texId, &texW, &texH)) return;

    // Compute normalized UVs from TPAG source rect
    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    // Compute local quad corners (relative to origin, with target offset)
    float localX0 = (float) tpag->targetX - originX;
    float localY0 = (float) tpag->targetY - originY;
    float localX1 = localX0 + (float) tpag->sourceWidth;
    float localY1 = localY0 + (float) tpag->sourceHeight;

    // Build 2D transform: T(x,y) * R(-angleDeg) * S(xscale, yscale)
    // GML rotation is counter-clockwise, OpenGL rotation is counter-clockwise, but
    // since we have Y-down, we negate the angle to get the correct visual rotation
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);

    // Transform 4 corners
    float x0, y0, x1, y1, x2, y2, x3, y3;
    Matrix4f_transformPoint(&transform, localX0, localY0, &x0, &y0); // top-left
    Matrix4f_transformPoint(&transform, localX1, localY0, &x1, &y1); // top-right
    Matrix4f_transformPoint(&transform, localX1, localY1, &x2, &y2); // bottom-right
    Matrix4f_transformPoint(&transform, localX0, localY1, &x3, &y3); // bottom-left

    // Convert BGR color to RGB floats
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    emitTexturedQuad(gl, texId, x0, y0, x1, y1, x2, y2, x3, y3, u0, v0, u1, v1, r, g, b, alpha);
}

static void glDrawSpritePart(Renderer* renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, float angleDeg, float pivotX, float pivotY, uint32_t color, float alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    if (!ensureTextureLoaded(gl, (uint32_t) pageId)) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];

    // Compute UVs for the sub-region within the atlas
    float u0 = (float) (tpag->sourceX + srcOffX) / (float) texW;
    float v0 = (float) (tpag->sourceY + srcOffY) / (float) texH;
    float u1 = (float) (tpag->sourceX + srcOffX + srcW) / (float) texW;
    float v1 = (float) (tpag->sourceY + srcOffY + srcH) / (float) texH;

    // Convert BGR color to RGB floats
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Quad corners (no origin offset - draw_sprite_part ignores sprite origin)
    float cx0, cy0, cx1, cy1, cx2, cy2, cx3, cy3;
    if (angleDeg == 0.0f) {
        cx0 = x;                         cy0 = y;
        cx1 = x + (float) srcW * xscale; cy1 = y;
        cx2 = x + (float) srcW * xscale; cy2 = y + (float) srcH * yscale;
        cx3 = x;                         cy3 = y + (float) srcH * yscale;
    } else {
        float angleRad = -angleDeg * ((float) M_PI / 180.0f);
        float cosA = cosf(angleRad);
        float sinA = sinf(angleRad);
        float qx0 = x,                         qy0 = y;
        float qx1 = x + (float) srcW * xscale, qy1 = y;
        float qx2 = x + (float) srcW * xscale, qy2 = y + (float) srcH * yscale;
        float qx3 = x,                         qy3 = y + (float) srcH * yscale;
        float dx, dy;
        dx = qx0 - pivotX; dy = qy0 - pivotY; cx0 = cosA * dx - sinA * dy + pivotX; cy0 = sinA * dx + cosA * dy + pivotY;
        dx = qx1 - pivotX; dy = qy1 - pivotY; cx1 = cosA * dx - sinA * dy + pivotX; cy1 = sinA * dx + cosA * dy + pivotY;
        dx = qx2 - pivotX; dy = qy2 - pivotY; cx2 = cosA * dx - sinA * dy + pivotX; cy2 = sinA * dx + cosA * dy + pivotY;
        dx = qx3 - pivotX; dy = qy3 - pivotY; cx3 = cosA * dx - sinA * dy + pivotX; cy3 = sinA * dx + cosA * dy + pivotY;
    }

    emitTexturedQuad(gl, texId, cx0, cy0, cx1, cy1, cx2, cy2, cx3, cy3, u0, v0, u1, v1, r, g, b, alpha);
}

static void glDrawSpritePos(Renderer* renderer, int32_t tpagIndex, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, float alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;
    TexturePageItem* tpag;
    GLuint texId;
    int32_t texW, texH;
    if (!resolveSpriteTexture(gl, tpagIndex, &tpag, &texId, &texW, &texH)) return;

    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    emitTexturedQuad(gl, texId, x1, y1, x2, y2, x3, y3, x4, y4, u0, v0, u1, v1, 1.0f, 1.0f, 1.0f, alpha);
}

// Emits a single colored quad into the batch using the white pixel texture
static void emitColoredQuad(GLRenderer* gl, float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    // Flush if texture changed or batch full
    if (gl->quadCount > 0 && gl->currentTextureId != gl->whiteTexture) flushBatch(gl);
    if (gl->quadCount >= MAX_QUADS) flushBatch(gl);

    gl->currentTextureId = gl->whiteTexture;

    float* verts = gl->vertexData + gl->quadCount * VERTICES_PER_QUAD * FLOATS_PER_VERTEX;

    // All UVs point to (0.5, 0.5) center of the 1x1 white texture
    // Vertex 0: top-left
    verts[0] = x0; verts[1] = y0; verts[2] = 0.5f; verts[3] = 0.5f;
    verts[4] = r;  verts[5] = g;  verts[6] = b;    verts[7] = a;

    // Vertex 1: top-right
    verts[8]  = x1; verts[9]  = y0; verts[10] = 0.5f; verts[11] = 0.5f;
    verts[12] = r;  verts[13] = g;  verts[14] = b;    verts[15] = a;

    // Vertex 2: bottom-right
    verts[16] = x1; verts[17] = y1; verts[18] = 0.5f; verts[19] = 0.5f;
    verts[20] = r;  verts[21] = g;  verts[22] = b;    verts[23] = a;

    // Vertex 3: bottom-left
    verts[24] = x0; verts[25] = y1; verts[26] = 0.5f; verts[27] = 0.5f;
    verts[28] = r;  verts[29] = g;  verts[30] = b;    verts[31] = a;

    gl->quadCount++;
}

static void glDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, bool outline) {
    GLRenderer* gl = (GLRenderer*) renderer;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    if (outline) {
        // Draw 4 one-pixel-wide edges: top, bottom, left, right
        emitColoredQuad(gl, x1, y1, x2 + 1, y1 + 1, r, g, b, alpha); // top
        emitColoredQuad(gl, x1, y2, x2 + 1, y2 + 1, r, g, b, alpha); // bottom
        emitColoredQuad(gl, x1, y1 + 1, x1 + 1, y2, r, g, b, alpha); // left
        emitColoredQuad(gl, x2, y1 + 1, x2 + 1, y2, r, g, b, alpha); // right
    } else {
        // Filled rectangle: GML adds +1 to width/height for filled rects
        emitColoredQuad(gl, x1, y1, x2 + 1, y2 + 1, r, g, b, alpha);
    }
}

// ===[ Line Drawing ]===

static void glDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color, float alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Compute perpendicular offset for line thickness
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (0.0001f > len) return;

    float halfW = width * 0.5f;
    float px = (-dy / len) * halfW;
    float py = (dx / len) * halfW;

    // Emit quad as 4 vertices forming a rectangle along the line
    if (gl->quadCount > 0 && gl->currentTextureId != gl->whiteTexture) {
        flushBatch(gl);
    }
    if (gl->quadCount >= MAX_QUADS) {
        flushBatch(gl);
    }
    gl->currentTextureId = gl->whiteTexture;

    float* verts = gl->vertexData + gl->quadCount * VERTICES_PER_QUAD * FLOATS_PER_VERTEX;

    // Vertex 0: start + perpendicular
    verts[0] = x1 + px; verts[1] = y1 + py; verts[2] = 0.5f; verts[3] = 0.5f;
    verts[4] = r; verts[5] = g; verts[6] = b; verts[7] = alpha;

    // Vertex 1: start - perpendicular
    verts[8] = x1 - px; verts[9] = y1 - py; verts[10] = 0.5f; verts[11] = 0.5f;
    verts[12] = r; verts[13] = g; verts[14] = b; verts[15] = alpha;

    // Vertex 2: end - perpendicular
    verts[16] = x2 - px; verts[17] = y2 - py; verts[18] = 0.5f; verts[19] = 0.5f;
    verts[20] = r; verts[21] = g; verts[22] = b; verts[23] = alpha;

    // Vertex 3: end + perpendicular
    verts[24] = x2 + px; verts[25] = y2 + py; verts[26] = 0.5f; verts[27] = 0.5f;
    verts[28] = r; verts[29] = g; verts[30] = b; verts[31] = alpha;

    gl->quadCount++;
}

static void glDrawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color1, uint32_t color2, float alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;

    float r1 = (float) BGR_R(color1) / 255.0f;
    float g1 = (float) BGR_G(color1) / 255.0f;
    float b1 = (float) BGR_B(color1) / 255.0f;

    float r2 = (float) BGR_R(color2) / 255.0f;
    float g2 = (float) BGR_G(color2) / 255.0f;
    float b2 = (float) BGR_B(color2) / 255.0f;

    // Compute perpendicular offset for line thickness
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (0.0001f > len) return;

    float halfW = width * 0.5f;
    float px = (-dy / len) * halfW;
    float py = (dx / len) * halfW;

    // Emit quad with per-vertex colors (color1 at start, color2 at end)
    if (gl->quadCount > 0 && gl->currentTextureId != gl->whiteTexture) {
        flushBatch(gl);
    }
    if (gl->quadCount >= MAX_QUADS) {
        flushBatch(gl);
    }
    gl->currentTextureId = gl->whiteTexture;

    float* verts = gl->vertexData + gl->quadCount * VERTICES_PER_QUAD * FLOATS_PER_VERTEX;

    // Vertex 0: start + perpendicular (color1)
    verts[0] = x1 + px; verts[1] = y1 + py; verts[2] = 0.5f; verts[3] = 0.5f;
    verts[4] = r1; verts[5] = g1; verts[6] = b1; verts[7] = alpha;

    // Vertex 1: start - perpendicular (color1)
    verts[8] = x1 - px; verts[9] = y1 - py; verts[10] = 0.5f; verts[11] = 0.5f;
    verts[12] = r1; verts[13] = g1; verts[14] = b1; verts[15] = alpha;

    // Vertex 2: end - perpendicular (color2)
    verts[16] = x2 - px; verts[17] = y2 - py; verts[18] = 0.5f; verts[19] = 0.5f;
    verts[20] = r2; verts[21] = g2; verts[22] = b2; verts[23] = alpha;

    // Vertex 3: end + perpendicular (color2)
    verts[24] = x2 + px; verts[25] = y2 + py; verts[26] = 0.5f; verts[27] = 0.5f;
    verts[28] = r2; verts[29] = g2; verts[30] = b2; verts[31] = alpha;

    gl->quadCount++;
}

static void glDrawRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color1, uint32_t color2, uint32_t color3, uint32_t color4, float alpha, bool outline) {
    GLRenderer* gl = (GLRenderer*) renderer;

    float r1 = (float) BGR_R(color1) / 255.0f;
    float g1 = (float) BGR_G(color1) / 255.0f;
    float b1 = (float) BGR_B(color1) / 255.0f;

    float r2 = (float) BGR_R(color2) / 255.0f;
    float g2 = (float) BGR_G(color2) / 255.0f;
    float b2 = (float) BGR_B(color2) / 255.0f;

    float r3 = (float) BGR_R(color3) / 255.0f;
    float g3 = (float) BGR_G(color3) / 255.0f;
    float b3 = (float) BGR_B(color3) / 255.0f;

    float r4 = (float) BGR_R(color4) / 255.0f;
    float g4 = (float) BGR_G(color4) / 255.0f;
    float b4 = (float) BGR_B(color4) / 255.0f;


    if (gl->quadCount > 0 && gl->currentTextureId != gl->whiteTexture) {
        flushBatch(gl);
    }
    if (gl->quadCount >= MAX_QUADS) {
        flushBatch(gl);
    }
    gl->currentTextureId = gl->whiteTexture;

    if (outline) {
        // Draw 4 one-pixel-wide edges: top, bottom, left, right
        glDrawLineColor(renderer, x1, y1, x2, y1, 1.0, color1, color2, alpha);
        glDrawLineColor(renderer, x2, y1, x2, y2, 1.0, color2, color3, alpha);
        glDrawLineColor(renderer, x2, y2, x1, y2, 1.0, color3, color4, alpha);
        glDrawLineColor(renderer, x1, y2, x1, y1, 1.0, color4, color1, alpha);
    } else {
        // Filled rectangle: GML adds +1 to width/height for filled rects

    float* verts = gl->vertexData + gl->quadCount * VERTICES_PER_QUAD * FLOATS_PER_VERTEX;

    // All UVs point to (0.5, 0.5) center of the 1x1 white texture
    // Vertex 0: top-left
    verts[0] = x1; verts[1] = y1; verts[2] = 0.5f; verts[3] = 0.5f;
    verts[4] = r1;  verts[5] = g1;  verts[6] = b1;    verts[7] = alpha;

    // Vertex 1: top-right
    verts[8]  = x2+1; verts[9]  = y1; verts[10] = 0.5f; verts[11] = 0.5f;
    verts[12] = r2;  verts[13] = g2;  verts[14] = b2;    verts[15] = alpha;

    // Vertex 2: bottom-right
    verts[16] = x2+1; verts[17] = y2+1; verts[18] = 0.5f; verts[19] = 0.5f;
    verts[20] = r3;  verts[21] = g3;  verts[22] = b3;    verts[23] = alpha;

    // Vertex 3: bottom-left
    verts[24] = x1; verts[25] = y2+1; verts[26] = 0.5f; verts[27] = 0.5f;
    verts[28] = r4;  verts[29] = g4;  verts[30] = b4;    verts[31] = alpha;

    gl->quadCount++;

    }
}

static void glDrawTriangle(Renderer *renderer, float x1, float y1, float x2, float y2, float x3, float y3, bool outline)
{
    GLRenderer* gl = (GLRenderer*) renderer;
    if(outline)
    {
        glDrawLine(renderer, x1, y1, x2, y2, 1, renderer->drawColor, 1.0);
        glDrawLine(renderer, x2, y2, x3, y3, 1, renderer->drawColor, 1.0);
        glDrawLine(renderer, x3, y3, x1, y1, 1, renderer->drawColor, 1.0);
    } else {
        float r = (float) BGR_R(renderer->drawColor) / 255.0f;
        float g = (float) BGR_G(renderer->drawColor) / 255.0f;
        float b = (float) BGR_B(renderer->drawColor) / 255.0f;

        flushBatch(gl);

        int i = 0;
        float verts[24] = {
            x1, y1, 0.0f, 0.0f, r, g, b, renderer->drawAlpha,
            x2, y2, 0.0f, 0.0f, r, g, b, renderer->drawAlpha,
            x3, y3, 0.0f, 0.0f, r, g, b, renderer->drawAlpha,
        };

        glBindVertexArray(gl->vao);
        glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, 3 * FLOATS_PER_VERTEX * sizeof(float), verts);

        glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
}

// ===[ Text Drawing ]===

// Resolved font state shared between glDrawText and glDrawTextColor
typedef struct {
    Font* font;
    TexturePageItem* fontTpag; // single TPAG for regular fonts (nullptr for sprite fonts)
    GLuint texId;
    int32_t texW, texH;
    Sprite* spriteFontSprite; // source sprite for sprite fonts (nullptr for regular fonts)
} GlFontState;

// Resolves font texture state
// Returns false if the font can't be drawn
static bool glResolveFontState(GLRenderer* gl, DataWin* dw, Font* font, GlFontState* state) {
    state->font = font;
    state->fontTpag = nullptr;
    state->texId = 0;
    state->texW = 0;
    state->texH = 0;
    state->spriteFontSprite = nullptr;

    if (!font->isSpriteFont) {
        int32_t fontTpagIndex = font->tpagIndex;
        if (0 > fontTpagIndex) return false;

        state->fontTpag = &dw->tpag.items[fontTpagIndex];
        int16_t pageId = state->fontTpag->texturePageId;
        if (0 > pageId || (uint32_t) pageId >= gl->textureCount) return false;
        if (!ensureTextureLoaded(gl, (uint32_t) pageId)) return false;

        state->texId = gl->glTextures[pageId];
        state->texW = gl->textureWidths[pageId];
        state->texH = gl->textureHeights[pageId];
    } else if (font->spriteIndex >= 0 && dw->sprt.count > (uint32_t) font->spriteIndex) {
        state->spriteFontSprite = &dw->sprt.sprites[font->spriteIndex];
    }
    return true;
}

// Resolves UV coordinates, texture ID, and local position for a single glyph
// Returns false if the glyph can't be drawn
static bool glResolveGlyph(GLRenderer* gl, DataWin* dw, GlFontState* state, FontGlyph* glyph, float cursorX, float cursorY, GLuint* outTexId, float* outU0, float* outV0, float* outU1, float* outV1, float* outLocalX0, float* outLocalY0) {
    Font* font = state->font;
    if (font->isSpriteFont && state->spriteFontSprite != nullptr) {
        Sprite* sprite = state->spriteFontSprite;
        int32_t glyphIndex = (int32_t) (glyph - font->glyphs);
        if (0 > glyphIndex ||  glyphIndex >= (int32_t) sprite->textureCount) return false;

        int32_t tpagIdx = sprite->tpagIndices[glyphIndex];
        if (0 > tpagIdx) return false;

        TexturePageItem* glyphTpag = &dw->tpag.items[tpagIdx];
        int16_t pid = glyphTpag->texturePageId;
        if (0 > pid || (uint32_t) pid >= gl->textureCount) return false;
        if (!ensureTextureLoaded(gl, (uint32_t) pid)) return false;

        *outTexId = gl->glTextures[pid];
        int32_t tw = gl->textureWidths[pid];
        int32_t th = gl->textureHeights[pid];

        *outU0 = (float) glyphTpag->sourceX / (float) tw;
        *outV0 = (float) glyphTpag->sourceY / (float) th;
        *outU1 = (float) (glyphTpag->sourceX + glyphTpag->sourceWidth) / (float) tw;
        *outV1 = (float) (glyphTpag->sourceY + glyphTpag->sourceHeight) / (float) th;

        *outLocalX0 = cursorX + (float) glyph->offset;
        *outLocalY0 = cursorY + (float) ((int32_t) glyphTpag->targetY - sprite->originY);
    } else {
        *outTexId = state->texId;
        *outU0 = (float) (state->fontTpag->sourceX + glyph->sourceX) / (float) state->texW;
        *outV0 = (float) (state->fontTpag->sourceY + glyph->sourceY) / (float) state->texH;
        *outU1 = (float) (state->fontTpag->sourceX + glyph->sourceX + glyph->sourceWidth) / (float) state->texW;
        *outV1 = (float) (state->fontTpag->sourceY + glyph->sourceY + glyph->sourceHeight) / (float) state->texH;

        *outLocalX0 = cursorX + glyph->offset;
        *outLocalY0 = cursorY;
    }
    return true;
}

static void glDrawText(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg) {
    GLRenderer* gl = (GLRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dw->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dw->font.fonts[fontIndex];

    GlFontState fontState;
    if (!glResolveFontState(gl, dw, font, &fontState)) return;

    uint32_t color = renderer->drawColor;
    float alpha = renderer->drawAlpha;
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    int32_t textLen = (int32_t) strlen(text);

    // Count lines, treating \r\n and \n\r as single breaks
    int32_t lineCount = TextUtils_countLines(text, textLen);

    // Per-line vertical stride. HTML5 runner's default `linesep` is `max_glyph_height * scaleY`.
    // We apply scaleY via the transform matrix below, so keep the stride in pre-scale (local) coords.
    float lineStride = TextUtils_lineStride(font);

    // Vertical alignment offset
    float totalHeight = (float) lineCount * lineStride;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    // Build transform matrix
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale * font->scaleX, yscale * font->scaleY, angleRad);

    // Iterate through lines. HTML5 subtracts ascenderOffset from the per-line y offset
    // (see yyFont.GR_Text_Draw), shifting glyphs up so the baseline aligns with the drawn y.
    float cursorY = valignOffset - (float) font->ascenderOffset;
    int32_t lineStart = 0;

    for (int32_t lineIdx = 0; lineCount > lineIdx; lineIdx++) {
        // Find end of current line
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(text[lineEnd])) {
            lineEnd++;
        }
        int32_t lineLen = lineEnd - lineStart;

        // Horizontal alignment offset for this line
        float lineWidth = TextUtils_measureLineWidth(font, text + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;

        // Render each glyph in the line - decode each codepoint once and carry it forward as next iteration's ch (also used for kerning)
        int32_t pos = 0;
        uint16_t ch = 0;
        bool hasCh = false;
        if (lineLen > pos) {
            ch = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            hasCh = true;
        }

        while (hasCh) {
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);

            uint16_t nextCh = 0;
            bool hasNext = lineLen > pos;
            if (hasNext) nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);

            if (glyph != nullptr) {
                bool drewSuccessfully = false;
                if (glyph->sourceWidth != 0 && glyph->sourceHeight != 0) {
                    float u0, v0, u1, v1;
                    float localX0, localY0;
                    GLuint glyphTexId;

                    if (glResolveGlyph(gl, dw, &fontState, glyph, cursorX, cursorY, &glyphTexId, &u0, &v0, &u1, &v1, &localX0, &localY0)) {
                        // Flush if texture changed or batch full
                        if (gl->quadCount > 0 && gl->currentTextureId != glyphTexId) flushBatch(gl);
                        if (gl->quadCount >= MAX_QUADS) flushBatch(gl);
                        gl->currentTextureId = glyphTexId;

                        float localX1 = localX0 + (float) glyph->sourceWidth;
                        float localY1 = localY0 + (float) glyph->sourceHeight;

                        // Transform corners
                        float px0, py0, px1, py1, px2, py2, px3, py3;
                        Matrix4f_transformPoint(&transform, localX0, localY0, &px0, &py0);
                        Matrix4f_transformPoint(&transform, localX1, localY0, &px1, &py1);
                        Matrix4f_transformPoint(&transform, localX1, localY1, &px2, &py2);
                        Matrix4f_transformPoint(&transform, localX0, localY1, &px3, &py3);

                        // Write 4 vertices
                        float* verts = gl->vertexData + gl->quadCount * VERTICES_PER_QUAD * FLOATS_PER_VERTEX;

                        verts[0] = px0; verts[1] = py0; verts[2] = u0; verts[3] = v0;
                        verts[4] = r;   verts[5] = g;   verts[6] = b;  verts[7] = alpha;

                        verts[8]  = px1; verts[9]  = py1; verts[10] = u1; verts[11] = v0;
                        verts[12] = r;   verts[13] = g;   verts[14] = b;  verts[15] = alpha;

                        verts[16] = px2; verts[17] = py2; verts[18] = u1; verts[19] = v1;
                        verts[20] = r;   verts[21] = g;   verts[22] = b;  verts[23] = alpha;

                        verts[24] = px3; verts[25] = py3; verts[26] = u0; verts[27] = v1;
                        verts[28] = r;   verts[29] = g;   verts[30] = b;  verts[31] = alpha;

                        gl->quadCount++;
                        drewSuccessfully = true;
                    }
                }

                cursorX += glyph->shift;
                if (drewSuccessfully && hasNext) {
                    cursorX += TextUtils_getKerningOffset(glyph, nextCh);
                }
            }

            ch = nextCh;
            hasCh = hasNext;
        }

        cursorY += lineStride;
        // Skip past the newline, treating \r\n and \n\r as single breaks
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(text, lineEnd, textLen);
        } else {
            lineStart = lineEnd;
        }
    }
}

static void glDrawTextColor(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, int32_t _c1, int32_t _c2, int32_t _c3, int32_t _c4, float alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dw->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dw->font.fonts[fontIndex];

    GlFontState fontState;
    if (!glResolveFontState(gl, dw, font, &fontState)) return;

    int32_t textLen = (int32_t) strlen(text);
    if(textLen == 0) return;

    // Count lines, treating \r\n and \n\r as single breaks
    int32_t lineCount = TextUtils_countLines(text, textLen);

    float lineStride = TextUtils_lineStride(font);

    // Vertical alignment offset
    float totalHeight = (float) lineCount * lineStride;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    // Build transform matrix
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale * font->scaleX, yscale * font->scaleY, angleRad);

    // Iterate through lines. HTML5 subtracts ascenderOffset from per-line y offset.
    float cursorY = valignOffset - (float) font->ascenderOffset;
    int32_t lineStart = 0;

    for (int32_t lineIdx = 0; lineCount > lineIdx; lineIdx++) {
        // Find end of current line
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(text[lineEnd])) {
            lineEnd++;
        }
        int32_t lineLen = lineEnd - lineStart;

        // Horizontal alignment offset for this line
        float lineWidth = TextUtils_measureLineWidth(font, text + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;
        // Pixel-position cursor for the gradient
        float gradientX = 0.0f;

        // Render each glyph in the line - decode each codepoint once and carry it forward as next iteration's ch (also used for kerning)
        int32_t pos = 0;
        uint16_t ch = 0;
        bool hasCh = false;
        if (lineLen > pos) {
            ch = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            hasCh = true;
        }

        while (hasCh) {
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);

            uint16_t nextCh = 0;
            bool hasNext = lineLen > pos;
            if (hasNext) nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);

            if (glyph != nullptr) {
                float advance = (float) glyph->shift;
                float leftFrac  = (lineWidth > 0.0f) ? (gradientX / lineWidth) : 0.0f;
                float rightFrac = (lineWidth > 0.0f) ? ((gradientX + advance) / lineWidth) : 1.0f;
                int32_t c1 = Color_lerp(_c1, _c2, leftFrac);
                int32_t c2 = Color_lerp(_c1, _c2, rightFrac);
                int32_t c3 = Color_lerp(_c4, _c3, rightFrac);
                int32_t c4 = Color_lerp(_c4, _c3, leftFrac);

                bool drewSuccessfully = false;
                if (glyph->sourceWidth != 0 && glyph->sourceHeight != 0) {
                    float u0, v0, u1, v1;
                    float localX0, localY0;
                    GLuint glyphTexId;

                    if (glResolveGlyph(gl, dw, &fontState, glyph, cursorX, cursorY, &glyphTexId, &u0, &v0, &u1, &v1, &localX0, &localY0)) {
                        // Flush if texture changed or batch full
                        if (gl->quadCount > 0 && gl->currentTextureId != glyphTexId) flushBatch(gl);
                        if (gl->quadCount >= MAX_QUADS) flushBatch(gl);
                        gl->currentTextureId = glyphTexId;

                        float localX1 = localX0 + (float) glyph->sourceWidth;
                        float localY1 = localY0 + (float) glyph->sourceHeight;

                        // Transform corners
                        float px0, py0, px1, py1, px2, py2, px3, py3;
                        Matrix4f_transformPoint(&transform, localX0, localY0, &px0, &py0);
                        Matrix4f_transformPoint(&transform, localX1, localY0, &px1, &py1);
                        Matrix4f_transformPoint(&transform, localX1, localY1, &px2, &py2);
                        Matrix4f_transformPoint(&transform, localX0, localY1, &px3, &py3);

                        // Write 4 vertices
                        float* verts = gl->vertexData + gl->quadCount * VERTICES_PER_QUAD * FLOATS_PER_VERTEX;

                        // top left
                        verts[0] = px0; verts[1] = py0; verts[2] = u0; verts[3] = v0;
                        verts[4] = ((float) BGR_R(c1) / 255.0f); verts[5] = ((float) BGR_G(c1) / 255.0f); verts[6] = ((float) BGR_B(c1) / 255.0f); verts[7] = alpha;

                        // top right
                        verts[8]  = px1; verts[9]  = py1; verts[10] = u1; verts[11] = v0;
                        verts[12] = ((float) BGR_R(c2) / 255.0f); verts[13] = ((float) BGR_G(c2) / 255.0f); verts[14] = ((float) BGR_B(c2) / 255.0f); verts[15] = alpha;

                        // bottom right
                        verts[16] = px2; verts[17] = py2; verts[18] = u1; verts[19] = v1;
                        verts[20] = ((float) BGR_R(c3) / 255.0f); verts[21] = ((float) BGR_G(c3) / 255.0f); verts[22] = ((float) BGR_B(c3) / 255.0f); verts[23] = alpha;

                        // bottom left
                        verts[24] = px3; verts[25] = py3; verts[26] = u0; verts[27] = v1;
                        verts[28] = ((float) BGR_R(c4) / 255.0f); verts[29] = ((float) BGR_G(c4) / 255.0f); verts[30] = ((float) BGR_B(c4) / 255.0f); verts[31] = alpha;

                        gl->quadCount++;
                        drewSuccessfully = true;
                    }
                }

                cursorX += glyph->shift;
                gradientX   += glyph->shift;
                if (drewSuccessfully && hasNext) {
                    float kern = TextUtils_getKerningOffset(glyph, nextCh);
                    cursorX += kern;
                    gradientX   += kern;
                }
            }

            ch = nextCh;
            hasCh = hasNext;
        }

        cursorY += lineStride;
        // Skip past the newline, treating \r\n and \n\r as single breaks
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(text, lineEnd, textLen);
        } else {
            lineStart = lineEnd;
        }
    }
}

// ===[ Dynamic Sprite Creation/Deletion ]===

// Finds a free dynamic texture page slot (glTextures[i] == 0), or appends a new one.
static uint32_t findOrAllocTexturePageSlot(GLRenderer* gl) {
    // Scan dynamic range for a reusable slot
    for (uint32_t i = gl->originalTexturePageCount; gl->textureCount > i; i++) {
        if (gl->glTextures[i] == 0) return i;
    }
    // No free slot found, grow the arrays
    uint32_t newPageId = gl->textureCount;
    gl->textureCount++;
    gl->glTextures = safeRealloc(gl->glTextures, gl->textureCount * sizeof(GLuint));
    gl->textureWidths = safeRealloc(gl->textureWidths, gl->textureCount * sizeof(int32_t));
    gl->textureHeights = safeRealloc(gl->textureHeights, gl->textureCount * sizeof(int32_t));
    gl->textureLoaded = safeRealloc(gl->textureLoaded, gl->textureCount * sizeof(bool));
    gl->glTextures[newPageId] = 0;
    gl->textureWidths[newPageId] = 0;
    gl->textureHeights[newPageId] = 0;
    gl->textureLoaded[newPageId] = false;
    return newPageId;
}

// Finds a free dynamic TPAG slot (texturePageId == -1), or appends a new one.
static uint32_t findOrAllocTpagSlot(DataWin* dw, uint32_t originalTpagCount) {
    for (uint32_t i = originalTpagCount; dw->tpag.count > i; i++) {
        if (dw->tpag.items[i].texturePageId == -1) return i;
    }
    uint32_t newIndex = dw->tpag.count;
    dw->tpag.count++;
    dw->tpag.items = safeRealloc(dw->tpag.items, dw->tpag.count * sizeof(TexturePageItem));
    memset(&dw->tpag.items[newIndex], 0, sizeof(TexturePageItem));
    dw->tpag.items[newIndex].texturePageId = -1;
    return newIndex;
}









static int32_t glCreateSurface(Renderer* renderer, int32_t width, int32_t height) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    uint32_t surfaceIndex = GLCommon_findOrAllocateSurfaceSlot(&gl->surfaces, &gl->surfaceTexture, &gl->surfaceWidth, &gl->surfaceHeight, &gl->surfaceCount);

    glGenFramebuffers(1, &gl->surfaces[surfaceIndex]);

    glGenTextures(1, &gl->surfaceTexture[surfaceIndex]);
    glBindTexture(GL_TEXTURE_2D, gl->surfaceTexture[surfaceIndex]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceIndex]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->surfaceTexture[surfaceIndex], 0);
    fprintf(stderr, "GL: Created surface %u with size (%dx%d)\n", surfaceIndex, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);

    //gl->surfaceTexture[surfaceIndex] = 0;
    gl->surfaceWidth[surfaceIndex] = width;
    gl->surfaceHeight[surfaceIndex] = height;

    //glGenTextures
    return surfaceIndex;
}


/*
glSurfaceFree
    gl->surfaceTexture[newSurfaceIndex] = 0;
    gl->surfaceWidth[newSurfaceIndex] = 0;
    gl->surfaceHeight[newSurfaceIndex] = 0;
*/


static void glSurfaceFree(Renderer* renderer, int32_t surfaceID) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    if (surfaceID == APPLICATION_SURFACE_ID) return;
    if (gl->surfaceTexture[surfaceID] != 0) glDeleteTextures(1, &gl->surfaceTexture[surfaceID]);
    if (gl->surfaces[surfaceID] != 0) glDeleteFramebuffers(1, &gl->surfaces[surfaceID]);




    //glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
    gl->surfaces[surfaceID] = 0;
    gl->surfaceTexture[surfaceID] = 0;
    gl->surfaceWidth[surfaceID] = 0;
    gl->surfaceHeight[surfaceID] = 0;
    fprintf(stderr, "GL: Freed Surface %u\n", surfaceID);
}




//glSurfaceResize

static void glSurfaceResize(Renderer* renderer, int32_t surfaceID, int32_t width, int32_t height) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    if (surfaceID != -1) {
        if (gl->surfaceWidth[surfaceID] == width && gl->surfaceHeight[surfaceID] == height) return;

        if (gl->surfaceTexture[surfaceID] != 0) glDeleteTextures(1, &gl->surfaceTexture[surfaceID]);

        glGenTextures(1, &gl->surfaceTexture[surfaceID]);
        glBindTexture(GL_TEXTURE_2D, gl->surfaceTexture[surfaceID]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceID]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->surfaceTexture[surfaceID], 0);

        fprintf(stderr, "GL: Resized Surface %u Size (%dx%d)\n", surfaceID, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
        gl->surfaceWidth[surfaceID] = width;
        gl->surfaceHeight[surfaceID] = height;
    }

/*
    glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->fboTexture, 0);
*/

}


static bool glSurfaceExists(Renderer* renderer, int32_t surfaceId) {
    GLRenderer* gl = (GLRenderer*) renderer;


    if (surfaceId > -1) {
        if (surfaceId < gl->surfaceCount)
        {
            if (gl->surfaces[surfaceId] != 0) {

                return true;
            }
        }
    }

    return false;
}

static bool glSurfaceGetPixels(Renderer* renderer, int32_t surfaceId, uint8_t* outRGBA) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    return GLCommon_surfaceGetPixels(gl->surfaces, gl->surfaceWidth, gl->surfaceHeight, gl->surfaceCount, surfaceId, outRGBA);
}

static bool glSetRenderTarget(Renderer* renderer, int32_t surfaceId) {
    GLRenderer* gl = (GLRenderer*) renderer;

    if (surfaceId != APPLICATION_SURFACE_ID) {
        if (surfaceId < gl->surfaceCount)
        {
            if (gl->surfaces[surfaceId] != 0) {
                glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceId]);
                // Build orthographic projection (Y-down for GML coordinate system)
                Matrix4f projection;
                Matrix4f_identity(&projection);
                Matrix4f_ortho(&projection, (float) 0.0, (float) gl->surfaceWidth[surfaceId], gl->surfaceHeight[surfaceId], (float) 0.0, -1.0f, 1.0f);

                glUseProgram(gl->shaderProgram);
                glUniformMatrix4fv(gl->uProjection, 1, GL_FALSE, projection.m);
                glUniform1i(gl->uTexture, 0);
                glViewport(0,0,gl->surfaceWidth[surfaceId],gl->surfaceHeight[surfaceId]);
                glDisable(GL_SCISSOR_TEST);


                return true;
            }
        }
    }

    if (surfaceId == APPLICATION_SURFACE_ID) {
        glUniformMatrix4fv(gl->uProjection, 1, GL_FALSE, renderer->PreviousViewMatrix.m);
        glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
        //glViewport(0, 0, gl->fboWidth, gl->fboHeight);
        glViewport(gl->base.CPortX, gl->base.CPortY, gl->base.CPortW, gl->base.CPortH);

        glEnable(GL_SCISSOR_TEST);
        return true;
    }

    return false;
}

static void glSurfaceCopy(Renderer* renderer, int32_t destSurfaceID, int32_t destX, int32_t destY, int32_t srcSurfaceID, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, bool part) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    GLCommon_surfaceBlit(gl->fbo, gl->fboWidth, gl->fboHeight, gl->surfaces, gl->surfaceWidth, gl->surfaceHeight, gl->surfaceCount, destSurfaceID, destX, destY, srcSurfaceID, srcX, srcY, srcW, srcH, part);
}

static float glGetSurfaceWidth(Renderer* renderer, int32_t surfaceId) {
    GLRenderer* gl = (GLRenderer*) renderer;

    flushBatch(gl);

    //fprintf(stderr, "Get Surface Width %u\n", surfaceId);
    if (surfaceId > -1) {
        if (surfaceId < gl->surfaceCount)
        {
            if (gl->surfaces[surfaceId] != 0) {

                return (float) gl->surfaceWidth[surfaceId];
            }
        }
    }

    return 0.0;
}

static float glGetSurfaceHeight(Renderer* renderer, int32_t surfaceId) {
    GLRenderer* gl = (GLRenderer*) renderer;

    flushBatch(gl);

    if (surfaceId > -1) {
        if (surfaceId < gl->surfaceCount)
        {
            if (gl->surfaces[surfaceId] != 0) {
                return (float) gl->surfaceHeight[surfaceId];
            }
        }
    }

    return 0.0;
}

static void glDrawSurface(Renderer* renderer, int32_t surfaceID, float x, float y, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {

    GLRenderer* gl = (GLRenderer*) renderer;

    GLuint texId;
    int32_t texW = 0;
    int32_t texH = 0;

    if (surfaceID != -1) {
        if (0 > surfaceID || gl->surfaceCount <= (uint32_t) surfaceID) return;
        texId = gl->surfaceTexture[surfaceID];
        texW = gl->surfaceWidth[surfaceID];
        texH = gl->surfaceHeight[surfaceID];
    } else {
        texId = gl->fboTexture;
        texW = gl->fboWidth;
        texH = gl->fboHeight;
    }

    // Flush previous batch with the OLD texture before switching, so pending sprite quads aren't redrawn with the surface's pixels.
    if (gl->quadCount > 0 && gl->currentTextureId != texId) flushBatch(gl);
    if (gl->quadCount >= MAX_QUADS) flushBatch(gl);
    gl->currentTextureId = texId;

    float u0 = (float) 0.0;
    float v0 = (float) 1.0;
    float u1 = (float) 1.0;
    float v1 = (float) 0.0;


    // Compute local quad corners (relative to origin, with target offset)
    float localX0 = (float) 0.0;
    float localY0 = (float) 0.0;
    float localX1 = (float) texW;
    float localY1 = (float) texH;

    // Build 2D transform: T(x,y) * R(-angleDeg) * S(xscale, yscale)
    // GML rotation is counter-clockwise, OpenGL rotation is counter-clockwise, but
    // since we have Y-down, we negate the angle to get the correct visual rotation
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);

    // Transform 4 corners
    float x0, y0, x1, y1, x2, y2, x3, y3;
    Matrix4f_transformPoint(&transform, localX0, localY0, &x0, &y0); // top-left
    Matrix4f_transformPoint(&transform, localX1, localY0, &x1, &y1); // top-right
    Matrix4f_transformPoint(&transform, localX1, localY1, &x2, &y2); // bottom-right
    Matrix4f_transformPoint(&transform, localX0, localY1, &x3, &y3); // bottom-left

    // Convert BGR color to RGB floats
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Write 4 vertices into batch buffer
    float* verts = gl->vertexData + gl->quadCount * VERTICES_PER_QUAD * FLOATS_PER_VERTEX;

    // Vertex 0: top-left
    verts[0] = x0; verts[1] = y0; verts[2] = u0; verts[3] = v0;
    verts[4] = r;  verts[5] = g;  verts[6] = b;  verts[7] = alpha;

    // Vertex 1: top-right
    verts[8]  = x1; verts[9]  = y1; verts[10] = u1; verts[11] = v0;
    verts[12] = r;  verts[13] = g;  verts[14] = b;  verts[15] = alpha;

    // Vertex 2: bottom-right
    verts[16] = x2; verts[17] = y2; verts[18] = u1; verts[19] = v1;
    verts[20] = r;  verts[21] = g;  verts[22] = b;  verts[23] = alpha;

    // Vertex 3: bottom-left
    verts[24] = x3; verts[25] = y3; verts[26] = u0; verts[27] = v1;
    verts[28] = r;  verts[29] = g;  verts[30] = b;  verts[31] = alpha;

    gl->quadCount++;
}




static void glDrawSurfacePart(Renderer* renderer, int32_t surfaceID, int32_t x, int32_t y, int32_t left, int32_t top, int32_t width, int32_t height, float xscale, float yscale, uint32_t color, float alpha) {

    GLRenderer* gl = (GLRenderer*) renderer;

    GLuint texId;
    int32_t texW = 0;
    int32_t texH = 0;

    if (surfaceID != -1) {
        if (0 > surfaceID || gl->surfaceCount <= (uint32_t) surfaceID) return;
        texId = gl->surfaceTexture[surfaceID];
        texW = gl->surfaceWidth[surfaceID];
        texH = gl->surfaceHeight[surfaceID];
    } else {
        texId = gl->fboTexture;
        texW = gl->fboWidth;
        texH = gl->fboHeight;
    }

    // Flush previous batch with the OLD texture before switching, so pending sprite quads aren't redrawn with the surface's pixels.
    if (gl->quadCount > 0 && gl->currentTextureId != texId) flushBatch(gl);
    if (gl->quadCount >= MAX_QUADS) flushBatch(gl);
    gl->currentTextureId = texId;

    float u0 = (float) left / (float) texW;
    float v0 = (float) (float) texH - (top / (float) texH);
    float u1 = (float) (left + width) / (float) texW;
    float v1 = (float) (float) texH - ((top + height) / (float) texH);

    // Compute local quad corners (relative to origin, with target offset)
    float localX0 = 0;
    float localY0 = 0;
    float localX1 = (float) width;
    float localY1 = (float) height;
    //float alpha = 1.0;
    // Build 2D transform: T(x,y) * R(-angleDeg) * S(xscale, yscale)
    // GML rotation is counter-clockwise, OpenGL rotation is counter-clockwise, but
    // since we have Y-down, we negate the angle to get the correct visual rotation

    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, 0.0);

    // Transform 4 corners
    float x0, y0, x1, y1, x2, y2, x3, y3;
    Matrix4f_transformPoint(&transform, localX0, localY0, &x0, &y0); // top-left
    Matrix4f_transformPoint(&transform, localX1, localY0, &x1, &y1); // top-right
    Matrix4f_transformPoint(&transform, localX1, localY1, &x2, &y2); // bottom-right
    Matrix4f_transformPoint(&transform, localX0, localY1, &x3, &y3); // bottom-left

    // Convert BGR color to RGB floats
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;

    // Write 4 vertices into batch buffer
    float* verts = gl->vertexData + gl->quadCount * VERTICES_PER_QUAD * FLOATS_PER_VERTEX;

    // Vertex 0: top-left
    verts[0] = x0; verts[1] = y0; verts[2] = u0; verts[3] = v0;
    verts[4] = r;  verts[5] = g;  verts[6] = b;  verts[7] = alpha;

    // Vertex 1: top-right
    verts[8]  = x1; verts[9]  = y1; verts[10] = u1; verts[11] = v0;
    verts[12] = r;  verts[13] = g;  verts[14] = b;  verts[15] = alpha;

    // Vertex 2: bottom-right
    verts[16] = x2; verts[17] = y2; verts[18] = u1; verts[19] = v1;
    verts[20] = r;  verts[21] = g;  verts[22] = b;  verts[23] = alpha;

    // Vertex 3: bottom-left
    verts[24] = x3; verts[25] = y3; verts[26] = u0; verts[27] = v1;
    verts[28] = r;  verts[29] = g;  verts[30] = b;  verts[31] = alpha;

    gl->quadCount++;

}

static void glDrawSurfaceStretched(Renderer* renderer, int32_t surfaceID, float x, float y, float width, float height) {
    GLRenderer* gl = (GLRenderer*) renderer;
    GLuint texId;

    if (surfaceID != -1) {
        if (0 > surfaceID || gl->surfaceCount <= (uint32_t) surfaceID) return;
        texId = gl->surfaceTexture[surfaceID];
    } else {
        texId = gl->fboTexture;
    }

    // Flush previous batch with the OLD texture before switching, so pending sprite quads aren't redrawn with the surface's pixels.
    if (gl->quadCount > 0 && gl->currentTextureId != texId) flushBatch(gl);
    if (gl->quadCount >= MAX_QUADS) flushBatch(gl);
    gl->currentTextureId = texId;

    float u0 = (float) 0.0;
    float v0 = (float) 1.0;
    float u1 = (float) 1.0;
    float v1 = (float) 0.0;

    // Compute local quad corners (relative to origin, with target offset)
    float x0 = (float) x;
    float y0 = (float) y;
    float x1 = x0 + (float) width;
    float y1 = y0 + (float) height;
    float alpha = 1.0;
    // Build 2D transform: T(x,y) * R(-angleDeg) * S(xscale, yscale)
    // GML rotation is counter-clockwise, OpenGL rotation is counter-clockwise, but
    // since we have Y-down, we negate the angle to get the correct visual rotation
    // Convert BGR color to RGB floats
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;

    // Write 4 vertices into batch buffer
    float* verts = gl->vertexData + gl->quadCount * VERTICES_PER_QUAD * FLOATS_PER_VERTEX;

    // Vertex 0: top-left
    verts[0] = x0; verts[1] = y0; verts[2] = u0; verts[3] = v0;
    verts[4] = r;  verts[5] = g;  verts[6] = b;  verts[7] = alpha;

    // Vertex 1: top-right
    verts[8]  = x1; verts[9]  = y0; verts[10] = u1; verts[11] = v0;
    verts[12] = r;  verts[13] = g;  verts[14] = b;  verts[15] = alpha;

    // Vertex 2: bottom-right
    verts[16] = x1; verts[17] = y1; verts[18] = u1; verts[19] = v1;
    verts[20] = r;  verts[21] = g;  verts[22] = b;  verts[23] = alpha;

    // Vertex 3: bottom-left
    verts[24] = x0; verts[25] = y1; verts[26] = u0; verts[27] = v1;
    verts[28] = r;  verts[29] = g;  verts[30] = b;  verts[31] = alpha;

    gl->quadCount++;
}

static int32_t glCreateSpriteFromSurface(Renderer* renderer, int32_t surfaceID, int32_t x, int32_t y, int32_t w, int32_t h, bool removeback, bool smooth, int32_t xorig, int32_t yorig) {
    GLRenderer* gl = (GLRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 >= w || 0 >= h) return -1;

    // Flush any pending draws before reading pixels
    flushBatch(gl);

    // OpenGL Y is bottom-up, GML Y is top-down, so flip the Y coordinate
    int32_t glY = gl->fboHeight - y - h;

    // Read pixels from the FBO (application_surface)
    if (surfaceID == APPLICATION_SURFACE_ID) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gl->fbo);
    } else {
        if (surfaceID < gl->surfaceCount)
        {
            if (gl->surfaces[surfaceID] != 0) {
                    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceID]);
                    glY = gl->surfaceHeight[surfaceID] - y - h;
            }
        }
    }

    uint8_t* pixels = safeMalloc((size_t) w * (size_t) h * 4);
    if (pixels == nullptr) return -1;


    glReadPixels(x, glY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // Flip vertically (OpenGL reads bottom-to-top)
    size_t rowBytes = (size_t) w * 4;
    uint8_t* rowTemp = safeMalloc(rowBytes);
    repeat(h / 2, row) {
        uint8_t* top = pixels + row * rowBytes;
        uint8_t* bot = pixels + (h - 1 - row) * rowBytes;
        memcpy(rowTemp, top, rowBytes);
        memcpy(top, bot, rowBytes);
        memcpy(bot, rowTemp, rowBytes);
    }
    free(rowTemp);

    // Create a new GL texture from the captured pixels
    GLuint newTexId;
    glGenTextures(1, &newTexId);
    glBindTexture(GL_TEXTURE_2D, newTexId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(pixels);

    // Find or allocate slots for texture page, TPAG, and sprite
    uint32_t pageId = findOrAllocTexturePageSlot(gl);
    gl->glTextures[pageId] = newTexId;
    gl->textureWidths[pageId] = w;
    gl->textureHeights[pageId] = h;
    gl->textureLoaded[pageId] = true;

    uint32_t tpagIndex = findOrAllocTpagSlot(dw, gl->originalTpagCount);
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    tpag->sourceX = 0;
    tpag->sourceY = 0;
    tpag->sourceWidth = (uint16_t) w;
    tpag->sourceHeight = (uint16_t) h;
    tpag->targetX = 0;
    tpag->targetY = 0;
    tpag->targetWidth = (uint16_t) w;
    tpag->targetHeight = (uint16_t) h;
    tpag->boundingWidth = (uint16_t) w;
    tpag->boundingHeight = (uint16_t) h;
    tpag->texturePageId = (int16_t) pageId;

    uint32_t spriteIndex = DataWin_allocSpriteSlot(dw, gl->originalSpriteCount);
    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    // name was set by DataWin_allocSpriteSlot ("__newsprite<N>"); don't overwrite it here
    sprite->width = (uint32_t) w;
    sprite->height = (uint32_t) h;
    sprite->originX = xorig;
    sprite->originY = yorig;
    sprite->textureCount = 1;
    sprite->tpagIndices = safeMalloc(sizeof(int32_t));
    sprite->tpagIndices[0] = (int32_t) tpagIndex;
    sprite->maskCount = 0;
    sprite->masks = nullptr;

    fprintf(stderr, "GL: Created dynamic sprite %u (%dx%d) from surface %d at (%d,%d)\n", spriteIndex, w, h, surfaceID, x, y);
    return (int32_t) spriteIndex;
}

static void glDeleteSprite(Renderer* renderer, int32_t spriteIndex) {
    GLRenderer* gl = (GLRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > spriteIndex || dw->sprt.count <= (uint32_t) spriteIndex) return;

    // Refuse to delete original data.win sprites
    if (gl->originalSpriteCount > (uint32_t) spriteIndex) {
        fprintf(stderr, "GL: Cannot delete data.win sprite %d\n", spriteIndex);
        return;
    }

    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    if (sprite->textureCount == 0) return; // already deleted

    // Clean up GL texture and TPAG entries owned by this sprite.
    // Slots with index >= originalTpagCount are dynamically allocated and ours to free.
    repeat(sprite->textureCount, i) {
        int32_t tpagIdx = sprite->tpagIndices[i];
        if (tpagIdx >= 0 && (uint32_t) tpagIdx >= gl->originalTpagCount) {
            TexturePageItem* tpag = &dw->tpag.items[tpagIdx];
            int16_t pageId = tpag->texturePageId;
            if (pageId >= 0 && gl->textureCount > (uint32_t) pageId) {
                glDeleteTextures(1, &gl->glTextures[pageId]);
                gl->glTextures[pageId] = 0;
            }
            // Mark TPAG slot as free for reuse
            tpag->texturePageId = -1;
        }
    }

    // Clear the sprite entry so it won't be drawn and can be reused. Preserve `name` across the memset: the slot is still in sprt.count and must keep a valid string for asset_get_index / name lookups.
    free(sprite->tpagIndices);
    const char* keepName = sprite->name;
    memset(sprite, 0, sizeof(Sprite));
    sprite->name = keepName;

    fprintf(stderr, "GL: Deleted sprite %d\n", spriteIndex);
}

static void glGpuSetBlendMode(Renderer* renderer, int32_t mode) {
    flushBatch((GLRenderer*) renderer);
    glBlendEquation(GLCommon_blendModeToEquation(mode));
    glBlendFunc(GLCommon_blendModeToSFactor(mode), GLCommon_blendModeToDFactor(mode));
}

static void glGpuSetBlendModeExt(Renderer* renderer, int32_t sfactor, int32_t dfactor) {
    flushBatch((GLRenderer*) renderer);
    glBlendFunc(GLCommon_blendFactorToGL(sfactor), GLCommon_blendFactorToGL(dfactor));
}

static void glGpuSetBlendEnable(Renderer* renderer, bool enable) {
    flushBatch((GLRenderer*)renderer);
    enable ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
}

static bool glGpuGetBlendEnable(Renderer* renderer) {

    return glIsEnabled(GL_BLEND);
}

static void glGpuSetAlphaTestEnable(Renderer* renderer, bool enable) {
    GLRenderer* gl = (GLRenderer*) renderer;
    if (gl->alphaTestEnable == enable) return;
    flushBatch(gl);
    gl->alphaTestEnable = enable;
    glUseProgram(gl->shaderProgram);
    glUniform1f(gl->uAlphaTestRef, enable ? gl->alphaTestRef : -1.0f);
}

static void glGpuSetAlphaTestRef(Renderer* renderer, uint8_t ref) {
    GLRenderer* gl = (GLRenderer*) renderer;
    float refF = ref / 255.0f;
    if (gl->alphaTestRef == refF) return;
    flushBatch(gl);
    gl->alphaTestRef = refF;
    if (gl->alphaTestEnable) {
        glUseProgram(gl->shaderProgram);
        glUniform1f(gl->uAlphaTestRef, refF);
    }
}

static void glGpuSetColorWriteEnable(Renderer* renderer, bool red, bool green, bool blue, bool alpha) {
    flushBatch((GLRenderer*)renderer);
    glColorMask(red, green, blue, alpha);
}

static void glGpuSetFog(Renderer* renderer, bool enable, uint32_t color) {
    GLRenderer* gl = (GLRenderer*) renderer;
    if (gl->fogEnable == enable && gl->fogColor == color) return;
    flushBatch(gl);
    gl->fogEnable = enable;
    gl->fogColor = color;
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;
    glUseProgram(gl->shaderProgram);
    glUniform4f(gl->uFogColor, r, g, b, enable ? 1.0f : 0.0f);
}

// ===[ Vtable ]===

static RendererVtable glVtable = {
    .init = glInit,
    .destroy = glDestroy,
    .beginFrame = glBeginFrame,
    .endFrame = glEndFrame,
    .beginView = glBeginView,
    .endView = glEndView,
    .beginGUI = glBeginGUI,
    .endGUI = glEndGUI,
    .drawSprite = glDrawSprite,
    .drawSpritePos = glDrawSpritePos,
    .drawSpritePart = glDrawSpritePart,
    .drawRectangle = glDrawRectangle,
    .drawRectangleColor = glDrawRectangleColor,
    .drawLine = glDrawLine,
    .drawLineColor = glDrawLineColor,
    .drawTriangle = glDrawTriangle,
    .drawText = glDrawText,
    .drawTextColor = glDrawTextColor,
    .flush = glRendererFlush,
    .clearScreen = glClearScreen,
    .createSpriteFromSurface = glCreateSpriteFromSurface,
    .deleteSprite = glDeleteSprite,
    .gpuSetBlendMode = glGpuSetBlendMode,
    .gpuSetBlendModeExt = glGpuSetBlendModeExt,
    .gpuSetBlendEnable = glGpuSetBlendEnable,
    .gpuSetAlphaTestEnable = glGpuSetAlphaTestEnable,
    .gpuSetAlphaTestRef = glGpuSetAlphaTestRef,
    .gpuSetColorWriteEnable = glGpuSetColorWriteEnable,
    .gpuSetFog = glGpuSetFog,
    .gpuGetBlendEnable = glGpuGetBlendEnable,
    .drawTile = nullptr,
    .createSurface = glCreateSurface,
    .surfaceExists = glSurfaceExists,
    .setRenderTarget = glSetRenderTarget,
    .surfaceCopy = glSurfaceCopy,
    .surfaceGetPixels = glSurfaceGetPixels,
    .getSurfaceWidth = glGetSurfaceWidth,
    .getSurfaceHeight = glGetSurfaceHeight,
    .drawSurface = glDrawSurface,
    .drawSurfacePart = glDrawSurfacePart,
    .drawSurfaceStretched = glDrawSurfaceStretched,
    .surfaceResize = glSurfaceResize,
    .surfaceFree = glSurfaceFree,

};

// ===[ Public API ]===

Renderer* GLRenderer_create(void) {
    GLRenderer* gl = safeCalloc(1, sizeof(GLRenderer));
    gl->base.vtable = &glVtable;
    gl->base.drawColor = 0xFFFFFF; // white (BGR)
    gl->base.drawAlpha = 1.0f;
    gl->base.drawFont = -1;
    gl->base.drawHalign = 0;
    gl->base.drawValign = 0;
    gl->base.circlePrecision = 24;
    return (Renderer*) gl;
}
