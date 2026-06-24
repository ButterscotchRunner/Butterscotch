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
#include "math_compat.h"

#include "stb_image.h"
#include "stb_ds.h"
#include "utils.h"
#include "image_decoder.h"
#include "gl_common.h"

// ===[ Constants ]===
#define MAX_QUADS 4096
#define FLOATS_PER_VERTEX 8  // x, y, u, v, r, g, b, a
#define VERTICES_PER_TRIANGLE 3
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

static const char* defaultVertexShaderSource =
    GLSL_VERSION_DIRECTIVE
    GLSL_VERTEX_PRECISION
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec4 aColor;\n"
    "layout(location = 2) in vec2 aTexCoord;\n"
    "uniform mat4 uProjection;\n"
    "out vec2 vTexCoord;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);\n"
    "    vTexCoord = aTexCoord;\n"
    "    vColor = aColor;\n"
    "}\n";

static const char* defaultFragmentShaderSource =
    GLSL_VERSION_DIRECTIVE
    GLSL_FRAGMENT_PRECISION
    "in vec2 vTexCoord;\n"
    "in vec4 vColor;\n"
    "uniform sampler2D uTexture;\n"
    "uniform float uAlphaTestRef;\n"
    "uniform bool uAlphaTestEnabled;\n"
    "uniform vec4 uFogColor;\n" // rgb = fog color, a = enable flag (0 or 1)
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 c = texture(uTexture, vTexCoord) * vColor;\n"
    "   if (uAlphaTestEnabled)"
    "   {"
    "       if (uAlphaTestRef >= c.a) discard;\n"
    "   }"
    "    c.rgb = mix(c.rgb, uFogColor.rgb, uFogColor.a);\n"
    "    fragColor = c;\n"
    "}\n";


// ===[ Shader Compilation ]===

static GLuint compileShader(GLenum type, const char* source, bool* ok) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        fprintf(stderr, "GL: Shader compilation failed: %s\n", infoLog);
        *ok = false;
        return 0;
    }
    *ok = true;
    return shader;
}

static GLuint linkProgram(const char* name, uint32_t vertexAttributeCount, const char** vertexAttributes, GLuint vertShader, GLuint fragShader, bool *success2) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);

    repeat(vertexAttributeCount, i) {
        glBindAttribLocation(program, i, vertexAttributes[i]);
    }

    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        fprintf(stderr, "GL: Shader %s linking failed: %s\n", name, infoLog);
        *success2 = false;
    } else {
        *success2 = true;
        fprintf(stderr, "GL: Shader %s succesfully linked!\n", name);
    }
    return program;
}

GLShaderUniform* findShaderUniformByName(GMLShader* shader, const char* name) {
    repeat(shader->uniformCount, b) {
        if (strcmp(shader->uniforms[b].name, name) == 0) {
            return &shader->uniforms[b];
        }
    }

    return nullptr;
}

// ===[ Batch Flush ]===

static void flushBatch(GLRenderer* gl) {
    if (gl->batchCount == 0) return;

    if (gl->base.currentShader != -1) {
        GMLShader* shader = &gl->gmlShaders[gl->base.currentShader];

        GLShaderUniform* uniform = findShaderUniformByName(shader, "gm_BaseTexture");
        if (uniform != nullptr)
            glActiveTexture(GL_TEXTURE0 + uniform->samplerSlot);
        glBindTexture(GL_TEXTURE_2D, gl->currentTextureId);
    } else {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gl->currentTextureId);
    }

    int32_t singleVertexCount = 0;
    if (gl->batchType == BATCHTYPE_QUAD) {
        singleVertexCount = VERTICES_PER_QUAD;
    } else if (gl->batchType == BATCHTYPE_TRIANGLE) {
        singleVertexCount = VERTICES_PER_TRIANGLE;
    } else {
        abort();
    }

    int32_t vertexCount = gl->batchCount * singleVertexCount;
    int32_t indexCount = gl->batchCount * INDICES_PER_QUAD;

    // Bind the VAO so the EBO binding it carries is what glDrawElements uses.
    // Without this, glDrawElements would treat the nullptr indices arg as a literal pointer to client memory and SEGV inside the driver during async upload.
    glBindVertexArray(gl->vao);

    glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexCount * FLOATS_PER_VERTEX * sizeof(float), gl->vertexData);

    if (gl->batchType == BATCHTYPE_QUAD) {
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    } else if (gl->batchType == BATCHTYPE_TRIANGLE) {
        glDrawArrays(GL_TRIANGLES, 0, gl->batchCount * VERTICES_PER_TRIANGLE);
    } else {
        abort();
    }

    gl->batchCount = 0;
}

static void flushIfNeededAndSetActiveState(GLRenderer* gl, BatchType batchType, GLuint textureId) {
    if (gl->batchCount != 0) {
        // TODO: This should be changed down the road from MAX_QUADS to MAX_WHATEVER_BATCH_TYPE_ARE_WE_USING
        if (gl->batchType != batchType || gl->currentTextureId != textureId || gl->batchCount == MAX_QUADS) {
            flushBatch(gl);
        }
    }

    gl->batchType = batchType;
    gl->currentTextureId = textureId;
}

// ===[ Vtable Implementations ]===

static bool compileProgram(GMLShader* gmlShader, const char* name, const char* vertexShaderSource, const char* fragmentShaderSource, uint32_t vertexAttributeCount, const char** vertexAttributes) {
    fprintf(stderr, "GL: Compiling %s vertex shader\n", name);
    bool vertexShaderOK = false;
    bool fragmentShaderOK = false;
    GLuint vertShaderT = compileShader(GL_VERTEX_SHADER, vertexShaderSource, &vertexShaderOK);
    if (!vertexShaderOK) {
        fprintf(stderr, "GL: Failed to compile %s vertex shader!\n", name);
        return false;
    }
    fprintf(stderr, "GL: Compiling %s fragment shader\n", name);
    GLuint fragShaderT = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource, &fragmentShaderOK);
    if (!fragmentShaderOK) {
        fprintf(stderr, "GL: Failed to compile %s fragment shader!\n", name);
        return false;
    }

    bool success;
    GLuint shaderId = linkProgram(name, vertexAttributeCount, vertexAttributes, vertShaderT, fragShaderT, &success);
    glDeleteShader(vertShaderT);
    glDeleteShader(fragShaderT);
    //Texture Set Stage BS has to be done bruh :(
    int32_t samplerIndex = 0;
    GLint uniformCount;
    glGetProgramiv(shaderId, GL_ACTIVE_UNIFORMS, &uniformCount);

    gmlShader->uniformCount = uniformCount;
    gmlShader->uniforms = (GLShaderUniform *)safeCalloc(uniformCount, sizeof(GLShaderUniform));

    // We can only get the length of a specific uniform in OpenGL 4.3+...
    GLint maxUniformNameLength = 0;
    glGetProgramiv(shaderId, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxUniformNameLength);

    repeat(uniformCount, b) {
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;

        char* uniformName = (char *)safeMalloc(maxUniformNameLength + 1);

        glGetActiveUniform(shaderId, b, maxUniformNameLength, &length, &size, &type, uniformName);

        gmlShader->uniforms[b].location = glGetUniformLocation(shaderId, uniformName);
        gmlShader->uniforms[b].name = uniformName;
        gmlShader->uniforms[b].type = type;

        if (type == GL_SAMPLER_2D) {
            glUseProgram(shaderId);
            glUniform1i(gmlShader->uniforms[b].location, samplerIndex);
            gmlShader->uniforms[b].samplerSlot = samplerIndex;
            samplerIndex += 1;
        }
    }

    gmlShader->shaderId = shaderId;
    gmlShader->compiled = true;
    return true;
}

static void glInit(Renderer* renderer, DataWin* dataWin) {
    GLRenderer* gl = (GLRenderer*) renderer;
    renderer->dataWin = dataWin;

    GMLShader* defaultShader = (GMLShader *)safeCalloc(1, sizeof(GMLShader));
    bool success = compileProgram(defaultShader, "default", defaultVertexShaderSource, defaultFragmentShaderSource, 0, nullptr);
    if (!success) {
        fprintf(stderr, "GL: Failed to compile default shaders! Bailing...");
        abort();
    }

    gl->defaultShaderProgram = defaultShader;

    gl->gmlShaders = (GMLShader *)safeCalloc(dataWin->shdr.count, sizeof(GMLShader));
    fprintf(stderr, "GL: %u Shaders Found\n", dataWin->shdr.count);

    repeat(dataWin->shdr.count, i) {
        Shader* shdr = &dataWin->shdr.shaders[i];
        GMLShader* gmlShader = &gl->gmlShaders[i];

        if (!shdr->present) {
            gl->gmlShaderCount++;
            fprintf(stderr, "GL: Skipping shader %d because it isn't present!\n", i);
            continue;
        }

        fprintf(stderr, "GL: Compiling %s Vertex Shader\n", shdr->name);
        compileProgram(
            gmlShader,
            shdr->name,
#ifdef ENABLE_GLES
            shdr->glslES_Vertex,
            shdr->glslES_Fragment,
#else
            shdr->glsl_Vertex,
            shdr->glsl_Fragment,
#endif
            shdr->vertexAttributeCount,
            shdr->vertexAttributes
        );

        gl->gmlShaderCount++;
    }

    GLShaderUniform* uAlphaTestRef = findShaderUniformByName(gl->defaultShaderProgram, "uAlphaTestRef");
    GLShaderUniform* uFogColor = findShaderUniformByName(gl->defaultShaderProgram, "uFogColor");

    gl->alphaTestEnable = false;
    gl->alphaTestRef = 0.0f;
    gl->colorWriteR = true;
    gl->colorWriteG = true;
    gl->colorWriteB = true;
    gl->colorWriteA = true;
    gl->fogEnable = false;
    gl->fogColor = 0;
    glUseProgram(gl->defaultShaderProgram->shaderId);
    glUniform1f(uAlphaTestRef->location, -1.0f);
    glUniform4f(uFogColor->location, 0.0f, 0.0f, 0.0f, 0.0f);

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
    uint32_t* indices = (uint32_t *)safeMalloc(eboSize);
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
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*) (4 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*) (2 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // Allocate CPU-side vertex buffer
    gl->vertexData = (float *)safeMalloc(MAX_QUADS * VERTICES_PER_QUAD * FLOATS_PER_VERTEX * sizeof(float));

    // Prepare texture slots for lazy loading (PNG decode deferred to first use)
    gl->textureCount = dataWin->txtr.count;
    gl->glTextures = (GLuint *)safeMalloc(gl->textureCount * sizeof(GLuint));
    gl->textureWidths = (int32_t *)safeMalloc(gl->textureCount * sizeof(int32_t));
    gl->textureHeights = (int32_t *)safeMalloc(gl->textureCount * sizeof(int32_t));
    gl->textureLoaded = (bool *)safeMalloc(gl->textureCount * sizeof(bool));

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

    gl->batchCount = 0;
    gl->currentTextureId = 0;

    // Save original counts so we know which slots are from data.win vs dynamic
    gl->originalTexturePageCount = gl->textureCount;
    gl->originalTpagCount = dataWin->tpag.count;
    gl->originalSpriteCount = dataWin->sprt.count;

    fprintf(stderr, "GL: Renderer initialized (%u texture pages)\n", gl->textureCount);
}

static void glGpuSetShader(Renderer* renderer, int32_t shaderIndex) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    GMLShader* gmlShader = &gl->gmlShaders[shaderIndex];

    glUseProgram(gmlShader->shaderId);
    //Gotta set those built-ins! they ain't gonna set themselves
    GLShaderUniform* gmMatricesUniform = findShaderUniformByName(gmlShader, "gm_Matrices[0]");
    GLShaderUniform* gmFogColourUniform = findShaderUniformByName(gmlShader, "gm_FogColour");

    //Lights are for another time

    GLShaderUniform* gmAlphaTestEnabledUniform = findShaderUniformByName(gmlShader, "gm_AlphaTestEnabled");
    GLShaderUniform* gmAlphaRefValue = findShaderUniformByName(gmlShader, "gm_AlphaRefValue");

    if (gmMatricesUniform != nullptr) {
        glUniformMatrix4fv(gmMatricesUniform->location, 5, GL_FALSE, renderer->gmlMatrices[0].m);
    }
    if (gmFogColourUniform != nullptr) {
        glUniform1i(gmFogColourUniform->location, gl->fogColor);
    }
    if (gmAlphaTestEnabledUniform != nullptr) {
        glUniform1i(gmAlphaTestEnabledUniform->location, gl->alphaTestEnable);
    }
    if (gmAlphaRefValue != nullptr) {
        glUniform1f(gmAlphaRefValue->location, gl->alphaTestRef);
    }

    renderer->currentShader = shaderIndex;
}

static void glShaderSettingsRefresh(Renderer* renderer) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    if (renderer->currentShader != -1) {
        glGpuSetShader(renderer, (int32_t) renderer->currentShader);
    } else {
        float fogR = (float) BGR_R(gl->fogColor) / 255.0f;
        float fogG = (float) BGR_G(gl->fogColor) / 255.0f;
        float fogB = (float) BGR_B(gl->fogColor) / 255.0f;

        glUseProgram(gl->defaultShaderProgram->shaderId);

        GLShaderUniform* uProjection = findShaderUniformByName(gl->defaultShaderProgram, "uProjection");
        GLShaderUniform* uFogColor = findShaderUniformByName(gl->defaultShaderProgram, "uFogColor");
        GLShaderUniform* uAlphaTestRef = findShaderUniformByName(gl->defaultShaderProgram, "uAlphaTestRef");
        GLShaderUniform* uAlphaTestEnabled = findShaderUniformByName(gl->defaultShaderProgram, "uAlphaTestEnabled");
        GLShaderUniform* uTexture = findShaderUniformByName(gl->defaultShaderProgram, "uTexture");

        glUniformMatrix4fv(uProjection->location, 1, GL_FALSE, renderer->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION].m);
        glUniform4f(uFogColor->location, fogR, fogG, fogB, gl->fogEnable ? 1.0f : 0.0f);
        glUniform1f(uAlphaTestRef->location, gl->alphaTestRef);
        glUniform1i(uAlphaTestEnabled->location, gl->alphaTestEnable);
        glUniform1i(uTexture->location, 1);
    }
}

static void glGpuResetShader(Renderer* renderer) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    glUseProgram(gl->defaultShaderProgram->shaderId);
    renderer->currentShader = -1;
    glShaderSettingsRefresh(renderer);
}

static void freeShader(GMLShader* shader) {
    if (shader->compiled)
        glDeleteProgram(shader->shaderId);

    repeat(shader->uniformCount, i) {
        free(shader->uniforms[i].name);
    }
    free(shader->uniforms);
}

static void glDestroy(Renderer* renderer) {
    GLRenderer* gl = (GLRenderer*) renderer;

    glDeleteTextures(1, &gl->whiteTexture);

    repeat(gl->gmlShaderCount, i) {
        freeShader(&gl->gmlShaders[i]);
    }

    free(gl->gmlShaders);

    repeat(gl->surfaceCount, i) {
        if (gl->surfaceTexture[i] != 0) glDeleteTextures(1, &gl->surfaceTexture[i]);
        if (gl->surfaces[i] != 0) glDeleteFramebuffers(1, &gl->surfaces[i]);
    }
    free(gl->surfaces);
    free(gl->surfaceTexture);
    free(gl->surfaceWidth);
    free(gl->surfaceHeight);

    freeShader(gl->defaultShaderProgram);
    free(gl->defaultShaderProgram);
    glDeleteTextures((GLsizei) gl->textureCount, gl->glTextures);
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

    gl->batchCount = 0;
    gl->currentTextureId = 0;
    gl->windowW = windowW;
    gl->windowH = windowH;
    gl->gameW = gameW;
    gl->gameH = gameH;

    // Bind the application_surface
    int32_t appId = gl->base.runner->applicationSurfaceId;
    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[appId]);
    glViewport(0, 0, gameW, gameH);
    gl->base.CPortX = 0;
    gl->base.CPortY = 0;
    gl->base.CPortW = gameW;
    gl->base.CPortH = gameH;
}

static void glBeginView(Renderer* renderer, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, float viewAngle) {
    GLRenderer* gl = (GLRenderer*) renderer;

    gl->batchCount = 0;
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

    // World -> clip transform for this view.
    Matrix4f projection;
    Matrix4f_viewProjection(&projection, (float) viewX, (float) viewY, (float) viewW, (float) viewH, viewAngle);
    Matrix4f_flipClipY(&projection);

    renderer->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION] = projection;
    glShaderSettingsRefresh(renderer);
    glActiveTexture(GL_TEXTURE1);

    glBindVertexArray(gl->vao);
    renderer->previousViewMatrix = projection;

}

static void glEndView(Renderer* renderer) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    glDisable(GL_SCISSOR_TEST);
}

// camera_apply: swap the active world->clip projection on the current target without touching its viewport.
static void glApplyProjection(Renderer* renderer, const Matrix4f* worldToClip) {
    GLRenderer* gl = (GLRenderer*) renderer;
    // Flush first so pending quads draw under the projection they were issued with.
    flushBatch(gl);
    Matrix4f projection = *worldToClip;
    Matrix4f_flipClipY(&projection);
    renderer->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION] = projection;
    glShaderSettingsRefresh(renderer);
    renderer->previousViewMatrix = projection;
}

static void glBeginGUI(Renderer* renderer, int32_t guiW, int32_t guiH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, int32_t targetSurfaceId) {
    GLRenderer* gl = (GLRenderer*) renderer;

    gl->batchCount = 0;
    gl->currentTextureId = 0;

    if (targetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER) {
        glBindFramebuffer(GL_FRAMEBUFFER, gl->hostFramebuffer);
        glViewport(0, 0, portW, portH);
        glScissor(0, 0, portW, portH);
    } else {
        require(targetSurfaceId >= 0 && (uint32_t) targetSurfaceId < gl->surfaceCount);
        require(gl->surfaces[targetSurfaceId] != 0);
        glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[targetSurfaceId]);
        int32_t glPortY = gl->gameH - portY - portH;
        glViewport(portX, glPortY, portW, portH);
        glScissor(portX, glPortY, portW, portH);
    }

    glEnable(GL_SCISSOR_TEST);

    Matrix4f projection;
    Matrix4f_guiProjection(&projection, (float) guiW, (float) guiH, (float) portW, (float) portH);

    renderer->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION] = projection;
    glShaderSettingsRefresh(renderer);
    glActiveTexture(GL_TEXTURE1);

    glBindVertexArray(gl->vao);
}

static void glSetGuiProjection(Renderer* renderer, int32_t guiW, int32_t guiH, int32_t portW, int32_t portH, bool renderingToUserSurface) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    Matrix4f projection;
    Matrix4f_guiProjection(&projection, (float) guiW, (float) guiH, (float) portW, (float) portH);
    // GL surfaces are stored bottom-up and draw_surface samples them with vertical flip.
    // Flip the projection when we are rendering to a user surface so it comes back upright.
    if (renderingToUserSurface) Matrix4f_flipClipY(&projection);
    renderer->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION] = projection;
    glShaderSettingsRefresh(renderer);
}

static void glEndGUI(Renderer* renderer) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    glDisable(GL_SCISSOR_TEST);
}

static void glEndFrameInit(Renderer* renderer) {
    GLRenderer* gl = (GLRenderer*) renderer;
    glBindVertexArray(0);

    if (renderer->runner->usingAppSurface && !renderer->runner->appSurfaceAutoDraw) {
        glBindFramebuffer(GL_FRAMEBUFFER, gl->hostFramebuffer);
        return;
    }
    int32_t appId = gl->base.runner->applicationSurfaceId;
    GLCommon_beginLetterboxBlit(gl->surfaces[appId], gl->hostFramebuffer);
}

static void glEndFrameEnd(Renderer* renderer) {
    GLRenderer* gl = (GLRenderer*) renderer;

    if (renderer->runner->usingAppSurface && !renderer->runner->appSurfaceAutoDraw) {
        return;
    }
    int32_t appId = gl->base.runner->applicationSurfaceId;
    GLCommon_beginLetterboxBlit(gl->surfaces[appId], gl->hostFramebuffer);
    GLCommon_endLetterboxBlit(gl->surfaceWidth[appId], gl->surfaceHeight[appId], gl->gameW, gl->gameH, gl->windowW, gl->windowH, gl->hostFramebuffer);
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
bool GLRenderer_ensureTextureLoaded(GLRenderer* gl, uint32_t pageId) {
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

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
    if (!GLRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return false;
    *outTpag = tpag;
    *outTexId = gl->glTextures[pageId];
    *outTexW = gl->textureWidths[pageId];
    *outTexH = gl->textureHeights[pageId];
    return true;
}

// Emits a single textured quad into the batch given 4 final screen-space corners (TL, TR, BR, BL), 4 UVs forming a rect (u0,v0)-(u1,v1), and a flat color/alpha.
// Handles texture rebinding and batch flushing.
static void emitTexturedQuad(
    GLRenderer* gl,
    GLuint texId,
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    float x3,
    float y3,
    float u0,
    float v0,
    float u1,
    float v1,
    float r0,
    float g0,
    float b0,
    float r1,
    float g1,
    float b1,
    float r2,
    float g2,
    float b2,
    float r3,
    float g3,
    float b3,
    float alpha
 ) {
    flushIfNeededAndSetActiveState(gl, BATCHTYPE_QUAD, texId);

    float* verts = gl->vertexData + gl->batchCount * VERTICES_PER_QUAD * FLOATS_PER_VERTEX;

    // Vertex 0: top-left
    verts[0] = x0; verts[1] = y0; verts[2] = u0; verts[3] = v0;
    verts[4] = r0;  verts[5] = g0;  verts[6] = b0;  verts[7] = alpha;

    // Vertex 1: top-right
    verts[8]  = x1; verts[9]  = y1; verts[10] = u1; verts[11] = v0;
    verts[12] = r1;  verts[13] = g1;  verts[14] = b1;  verts[15] = alpha;

    // Vertex 2: bottom-right
    verts[16] = x2; verts[17] = y2; verts[18] = u1; verts[19] = v1;
    verts[20] = r2;  verts[21] = g2;  verts[22] = b2;  verts[23] = alpha;

    // Vertex 3: bottom-left
    verts[24] = x3; verts[25] = y3; verts[26] = u0; verts[27] = v1;
    verts[28] = r3;  verts[29] = g3;  verts[30] = b3;  verts[31] = alpha;

    gl->batchCount++;
}

static void drawMultiColoredTextureWithTransform(
    GLRenderer* renderer,
    GLuint textureId,
    Matrix4f transform,
    // Locals = the coordinate in relation to the sprite "frame" itself, includes originX/originY and trimmed transparency
    float localX0,
    float localY0,
    float localX1,
    float localY1,
    float u0,
    float v0,
    float u1,
    float v1,
    float r0,
    float g0,
    float b0,
    float r1,
    float g1,
    float b1,
    float r2,
    float g2,
    float b2,
    float r3,
    float g3,
    float b3,
    float alpha
) {
    // Transform 4 corners
    float x0, y0, x1, y1, x2, y2, x3, y3;
    Matrix4f_transformPoint(&transform, localX0, localY0, &x0, &y0); // top-left
    Matrix4f_transformPoint(&transform, localX1, localY0, &x1, &y1); // top-right
    Matrix4f_transformPoint(&transform, localX1, localY1, &x2, &y2); // bottom-right
    Matrix4f_transformPoint(&transform, localX0, localY1, &x3, &y3); // bottom-left

    emitTexturedQuad(
        renderer,
        textureId,
        x0,
        y0,
        x1,
        y1,
        x2,
        y2,
        x3,
        y3,
        u0,
        v0,
        u1,
        v1,
        r0,
        g0,
        b0,
        r1,
        g1,
        b1,
        r2,
        g2,
        b2,
        r3,
        g3,
        b3,
        alpha
    );
}

static void drawTextureWithTransform(
    GLRenderer* renderer,
    GLuint textureId,
    Matrix4f transform,
    // Locals = the coordinate in relation to the sprite "frame" itself, includes originX/originY and trimmed transparency
    float localX0,
    float localY0,
    float localX1,
    float localY1,
    float u0,
    float v0,
    float u1,
    float v1,
    uint32_t color,
    float alpha
) {
    // Convert BGR color to RGB floats
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    drawMultiColoredTextureWithTransform(
        renderer,
        textureId,
        transform,
        localX0,
        localY0,
        localX1,
        localY1,
        u0,
        v0,
        u1,
        v1,
        r,
        g,
        b,
        r,
        g,
        b,
        r,
        g,
        b,
        r,
        g,
        b,
        alpha
    );
}

static void drawTexture(
    GLRenderer* renderer,
    GLuint textureId,
    float x,
    float y,
    // Locals = the coordinate in relation to the sprite "frame" itself, includes originX/originY and trimmed transparency
    float localX0,
    float localY0,
    float localX1,
    float localY1,
    float u0,
    float v0,
    float u1,
    float v1,
    float xscale,
    float yscale,
    float angleDeg,
    uint32_t color,
    float alpha
) {
    // Build 2D transform: T(x,y) * R(-angleDeg) * S(xscale, yscale)
    // GML rotation is counter-clockwise, OpenGL rotation is counter-clockwise, but
    // since we have Y-down, we negate the angle to get the correct visual rotation
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);

    drawTextureWithTransform(
        renderer,
        textureId,
        transform,
        localX0,
        localY0,
        localX1,
        localY1,
        u0,
        v0,
        u1,
        v1,
        color,
        alpha
    );
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

    // Use targetWidth/Height (draw size in bounding rect), not sourceWidth/Height (texture sample size).
    // They differ when the texture was auto-downscaled by GMS to fit a texture page.
    float localX0 = (float) tpag->targetX - originX;
    float localY0 = (float) tpag->targetY - originY;
    float localX1 = localX0 + (float) tpag->targetWidth;
    float localY1 = localY0 + (float) tpag->targetHeight;

    drawTexture(
        gl,
        texId,
        x,
        y,
        localX0,
        localY0,
        localX1,
        localY1,
        u0,
        v0,
        u1,
        v1,
        xscale,
        yscale,
        angleDeg,
        color,
        alpha
    );
}

static void drawTiled(
    GLRenderer* gl,
    GLuint texId,
    float gridX,
    float gridY,
    float tileW,
    float tileH,
    bool tileX,
    bool tileY,
    float roomW,
    float roomH,
    float quadOffsetX0,
    float quadW,
    float quadOffsetY0,
    float quadH,
    float u0,
    float v0,
    float u1,
    float v1,
    uint32_t color,
    float alpha
) {
    if (0 >= tileW || 0 >= tileH) return;

    float startX, endX, startY, endY;
    if (tileX) {
        startX = fmodf(gridX, tileW);
        if (startX > 0) startX -= tileW;
        endX = roomW;
    } else {
        startX = gridX;
        endX = startX + tileW;
    }
    if (tileY) {
        startY = fmodf(gridY, tileH);
        if (startY > 0) startY -= tileH;
        endY = roomH;
    } else {
        startY = gridY;
        endY = startY + tileH;
    }

    // Optimization for 2D affine projects: Clip the tiled extent to the world-space AABB of what the active projection can see.
    const Matrix4f* projection = &gl->base.gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION];
    Matrix4f invProjection;
    if (Matrix4f_isAffine2D(projection) && Matrix4f_inverse(&invProjection, projection)) {
        // The borders of the screen
        static const float ndcCorners[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}};
        float visMinX = 0.0f, visMinY = 0.0f, visMaxX = 0.0f, visMaxY = 0.0f;
        // For each point, get the borders of it in world-space.
        repeat(4, i) {
            float wx, wy;
            Matrix4f_transformPoint(&invProjection, ndcCorners[i][0], ndcCorners[i][1], &wx, &wy);
            if (i == 0 || wx < visMinX) visMinX = wx;
            if (i == 0 || wx > visMaxX) visMaxX = wx;
            if (i == 0 || wy < visMinY) visMinY = wy;
            if (i == 0 || wy > visMaxY) visMaxY = wy;
        }
        if (tileX) {
            // Snap start forward by whole tiles so the tile containing visMinX is kept
            if (visMinX > startX) startX += floorf((visMinX - startX) / tileW) * tileW;
            if (visMaxX < endX) endX = visMaxX;
        }
        if (tileY) {
            if (visMinY > startY) startY += floorf((visMinY - startY) / tileH) * tileH;
            if (visMaxY < endY) endY = visMaxY;
        }
    }
    if (startX >= endX || startY >= endY) return;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Integer tile counts avoid FP-comparison drift; the inner break handles overshoot at the boundary
    int32_t tilesX = (int32_t) ((endX - startX) / tileW) + 1;
    int32_t tilesY = (int32_t) ((endY - startY) / tileH) + 1;
    if (0 >= tilesX || 0 >= tilesY) return;

    repeat(tilesY, iy) {
        float dy = startY + (float) iy * tileH;
        if (dy >= endY) break;
        float vy0 = dy + quadOffsetY0;
        float vy1 = vy0 + quadH;
        repeat(tilesX, ix) {
            float dx = startX + (float) ix * tileW;
            if (dx >= endX) break;
            float vx0 = dx + quadOffsetX0;
            float vx1 = vx0 + quadW;
            emitTexturedQuad(gl, texId, vx0, vy0, vx1, vy0, vx1, vy1, vx0, vy1, u0, v0, u1, v1, r, g, b, r, g, b, r, g, b, r, g, b, alpha);
        }
    }
}

static void glDrawSpriteTiled(Renderer* renderer, int32_t tpagIndex, float originX, float originY, float x, float y, float xscale, float yscale, bool tileX, bool tileY, float roomW, float roomH, uint32_t color, float alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;
    TexturePageItem* tpag;
    GLuint texId;
    int32_t texW, texH;
    if (!resolveSpriteTexture(gl, tpagIndex, &tpag, &texId, &texW, &texH)) return;

    float axScale = fabsf(xscale);
    float ayScale = fabsf(yscale);
    float tileW = (float) tpag->boundingWidth * axScale;
    float tileH = (float) tpag->boundingHeight * ayScale;

    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    // Use targetWidth/Height (draw size in bounding rect), not sourceWidth/Height (texture sample size).
    // They differ when the texture was auto-downscaled by GMS to fit a texture page.
    float localX0 = (float) tpag->targetX - originX;
    float localY0 = (float) tpag->targetY - originY;
    // Per-tile quad origin = grid cell (dx) + originX*axScale (cancels the grid anchor) + xscale*localX0
    float quadOffX0 = originX * axScale + xscale * localX0;
    float quadOffY0 = originY * ayScale + yscale * localY0;
    float quadW = xscale * (float) tpag->targetWidth;
    float quadH = yscale * (float) tpag->targetHeight;

    drawTiled(
        gl,
        texId,
        x - originX * axScale,
        y - originY * ayScale,
        tileW,
        tileH,
        tileX,
        tileY,
        roomW,
        roomH,
        quadOffX0,
        quadW,
        quadOffY0,
        quadH,
        u0,
        v0,
        u1,
        v1,
        color,
        alpha
    );
}

static void glDrawSpritePart(Renderer* renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, float angleDeg, float pivotX, float pivotY, uint32_t color, float alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    if (!GLRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return;

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

    emitTexturedQuad(gl, texId, cx0, cy0, cx1, cy1, cx2, cy2, cx3, cy3, u0, v0, u1, v1, r, g, b, r, g, b, r, g, b, r, g, b, alpha);
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

    emitTexturedQuad(gl, texId, x1, y1, x2, y2, x3, y3, x4, y4, u0, v0, u1, v1, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, alpha);
}

// Emits a single colored quad into the batch using the white pixel texture
static void emitMultiColoredQuad(
    GLRenderer* gl,
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    float x3,
    float y3,
    float r0,
    float g0,
    float b0,
    float r1,
    float g1,
    float b1,
    float r2,
    float g2,
    float b2,
    float r3,
    float g3,
    float b3,
    float a
) {
    emitTexturedQuad(
        gl,
        gl->whiteTexture,
        x0,
        y0,
        x1,
        y1,
        x2,
        y2,
        x3,
        y3,
        // Points to the middle of the whiteTexture
        0.5f,
        0.5f,
        0.5f,
        0.5f,
        r0,
        g0,
        b0,
        r1,
        g1,
        b1,
        r2,
        g2,
        b2,
        r3,
        g3,
        b3,
        a
    );
}

static void emitColoredQuad(
    GLRenderer* gl,
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    float x3,
    float y3,
    float r,
    float g,
    float b,
    float a
) {
    emitTexturedQuad(
        gl,
        gl->whiteTexture,
        x0,
        y0,
        x1,
        y1,
        x2,
        y2,
        x3,
        y3,
        // Points to the middle of the whiteTexture
        0.5f,
        0.5f,
        0.5f,
        0.5f,
        r,
        g,
        b,
        r,
        g,
        b,
        r,
        g,
        b,
        r,
        g,
        b,
        a
    );
}

// Helper method that emits a colored rectangle
static void emitMultiColoredRectangle(
    GLRenderer* gl,
    float x0,
    float y0,
    float x1,
    float y1,
    float r0,
    float g0,
    float b0,
    float r1,
    float g1,
    float b1,
    float r2,
    float g2,
    float b2,
    float r3,
    float g3,
    float b3,
    float a
) {
    emitMultiColoredQuad(
        gl,
        // top-left
        x0,
        y0,
        // top-right
        x1,
        y0,
        // bottom-right
        x1,
        y1,
        // bottom-left
        x0,
        y1,
        r0,
        g0,
        b0,
        r1,
        g1,
        b1,
        r2,
        g2,
        b2,
        r3,
        g3,
        b3,
        a
    );
}

// Helper method that emits a colored rectangle
static void emitColoredRectangle(
    GLRenderer* gl,
    float x0,
    float y0,
    float x1,
    float y1,
    float r,
    float g,
    float b,
    float a
) {
    emitMultiColoredRectangle(gl, x0, y0, x1, y1, r, g, b, r, g, b, r, g, b, r, g, b, a);
}

static void glDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, bool outline) {
    GLRenderer* gl = (GLRenderer*) renderer;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    if (outline) {
        // Draw 4 one-pixel-wide edges: top, bottom, left, right
        emitColoredRectangle(gl, x1, y1, x2 + 1, y1 + 1, r, g, b, alpha); // top
        emitColoredRectangle(gl, x1, y2, x2 + 1, y2 + 1, r, g, b, alpha); // bottom
        emitColoredRectangle(gl, x1, y1 + 1, x1 + 1, y2, r, g, b, alpha); // left
        emitColoredRectangle(gl, x2, y1 + 1, x2 + 1, y2, r, g, b, alpha); // right
    } else {
        // Filled rectangle: GML adds +1 to width/height for filled rects
        emitColoredRectangle(gl, x1, y1, x2 + 1,y2 + 1, r, g, b, alpha);
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

    emitColoredQuad(gl, x1 + px, y1 + py, x1 - px, y1 - py, x2 - px, y2 - py, x2 + px, y2 + py, r, g, b, alpha);
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

    emitMultiColoredQuad(
        gl,
        x1 + px,
        y1 + py,
        x1 - px,
        y1 - py,
        x2 - px,
        y2 - py,
        x2 + px,
        y2 + py,
        r1,
        g1,
        b1,
        r1,
        g1,
        b1,
        r2,
        g2,
        b2,
        r2,
        g2,
        b2,
        alpha
    );
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

    if (outline) {
        // Draw 4 one-pixel-wide edges: top, bottom, left, right
        glDrawLineColor(renderer, x1, y1, x2, y1, 1.0, color1, color2, alpha);
        glDrawLineColor(renderer, x2, y1, x2, y2, 1.0, color2, color3, alpha);
        glDrawLineColor(renderer, x2, y2, x1, y2, 1.0, color3, color4, alpha);
        glDrawLineColor(renderer, x1, y2, x1, y1, 1.0, color4, color1, alpha);
    } else {
        // Filled rectangle: GML adds +1 to width/height for filled rects
        emitMultiColoredRectangle(
            gl,
            x1,
            y1,
            x2 + 1,
            y2 + 1,
            r1,
            g1,
            b1,
            r2,
            g2,
            b2,
            r3,
            g3,
            b3,
            r4,
            g4,
            b4,
            alpha
        );
    }
}

static void glDrawTriangle(Renderer *renderer, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color1, uint32_t color2, uint32_t color3, float alpha, bool outline) {
    GLRenderer* gl = (GLRenderer*) renderer;
    if (outline) {
        glDrawLineColor(renderer, x1, y1, x2, y2, 1, color1, color2, alpha);
        glDrawLineColor(renderer, x2, y2, x3, y3, 1, color2, color3, alpha);
        glDrawLineColor(renderer, x3, y3, x1, y1, 1, color3, color1, alpha);
    } else {
        flushIfNeededAndSetActiveState(gl, BATCHTYPE_TRIANGLE, gl->whiteTexture);

        // Woo, pointers!
        // This gets the vertex data for the new triangle batch
        float* verts = gl->vertexData + gl->batchCount * VERTICES_PER_TRIANGLE * FLOATS_PER_VERTEX;

        verts[0] = x1;
        verts[1] = y1;
        verts[2] = 0.0f;
        verts[3] = 0.0f;
        verts[4] = (float) BGR_R(color1) / 255.0f;
        verts[5] = (float) BGR_G(color1) / 255.0f;
        verts[6] = (float) BGR_B(color1) / 255.0f;
        verts[7] = alpha;

        verts[8]  = x2;
        verts[9]  = y2;
        verts[10] = 0.0f;
        verts[11] = 0.0f;
        verts[12] = (float) BGR_R(color2) / 255.0f;
        verts[13] = (float) BGR_G(color2) / 255.0f;
        verts[14] = (float) BGR_B(color2) / 255.0f;
        verts[15] = alpha;

        verts[16] = x3;
        verts[17] = y3;
        verts[18] = 0.0f;
        verts[19] = 0.0f;
        verts[20] = (float) BGR_R(color3) / 255.0f;
        verts[21] = (float) BGR_G(color3) / 255.0f;
        verts[22] = (float) BGR_B(color3) / 255.0f;
        verts[23] = alpha;

        gl->batchCount++;
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
        if (!GLRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return false;

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
        if (!GLRenderer_ensureTextureLoaded(gl, (uint32_t) pid)) return false;

        *outTexId = gl->glTextures[pid];
        int32_t tw = gl->textureWidths[pid];
        int32_t th = gl->textureHeights[pid];

        *outU0 = (float) glyphTpag->sourceX / (float) tw;
        *outV0 = (float) glyphTpag->sourceY / (float) th;
        *outU1 = (float) (glyphTpag->sourceX + glyphTpag->sourceWidth) / (float) tw;
        *outV1 = (float) (glyphTpag->sourceY + glyphTpag->sourceHeight) / (float) th;

        // Sprite-font glyphs sit at the cell offset. GM 2023.2+ subtracts the sprite origin, pre-2023.2 it cancels.
        // (See GameMaker-HTML5's commit a7c5b909209d5a28602fedfe2031965386a99921)
        *outLocalX0 = cursorX + (float) glyph->offset;
        *outLocalY0 = cursorY + (float) (int32_t) glyphTpag->targetY - (float) font->spriteOriginYAdjust;
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

static void drawText(
    Renderer* renderer,
    const char* text,
    float x,
    float y,
    float xscale,
    float yscale,
    float angleDeg,
    float lineSeparation,
    uint32_t _c1,
    uint32_t _c2,
    uint32_t _c3,
    uint32_t _c4,
    float alpha
) {
    GLRenderer* gl = (GLRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dw->font.count <= (uint32_t) fontIndex)
        return;

    Font* font = &dw->font.fonts[fontIndex];

    GlFontState fontState;
    if (!glResolveFontState(gl, dw, font, &fontState))
        return;

    int32_t textLen = (int32_t) strlen(text);
    if (textLen == 0)
        return;

    // Count lines, treating \r\n and \n\r as single breaks
    int32_t lineCount = TextUtils_countLines(text, textLen);

    float lineStride = (0.0f > lineSeparation) ? TextUtils_lineStride(font) : (lineSeparation / (font->scaleY != 0.0f ? font->scaleY : 1.0f));

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

    int32_t c1 = _c1;
    int32_t c2 = _c2;
    int32_t c3 = _c3;
    int32_t c4 = _c4;
    bool needsLerpingOnTheFly = c1 != c2 || c2 != c3 || c3 != c4;

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
                float leftFrac = (lineWidth > 0.0f) ? (gradientX / lineWidth) : 0.0f;
                float rightFrac = (lineWidth > 0.0f) ? ((gradientX + advance) / lineWidth) : 1.0f;
                if (needsLerpingOnTheFly) {
                    c1 = Color_lerp(_c1, _c2, leftFrac);
                    c2 = Color_lerp(_c1, _c2, rightFrac);
                    c3 = Color_lerp(_c4, _c3, rightFrac);
                    c4 = Color_lerp(_c4, _c3, leftFrac);
                }

                float r0 = (float) BGR_R(c1) / 255.0f;
                float g0 = (float) BGR_G(c1) / 255.0f;
                float b0 = (float) BGR_B(c1) / 255.0f;

                float r1 = (float) BGR_R(c2) / 255.0f;
                float g1 = (float) BGR_G(c2) / 255.0f;
                float b1 = (float) BGR_B(c2) / 255.0f;

                float r2 = (float) BGR_R(c3) / 255.0f;
                float g2 = (float) BGR_G(c3) / 255.0f;
                float b2 = (float) BGR_B(c3) / 255.0f;

                float r3 = (float) BGR_R(c4) / 255.0f;
                float g3 = (float) BGR_G(c4) / 255.0f;
                float b3 = (float) BGR_B(c4) / 255.0f;

                bool drewSuccessfully = false;
                if (glyph->sourceWidth != 0 && glyph->sourceHeight != 0) {
                    float u0, v0, u1, v1;
                    float localX0, localY0;
                    GLuint glyphTexId;

                    if (glResolveGlyph(gl, dw, &fontState, glyph, cursorX, cursorY, &glyphTexId, &u0, &v0, &u1, &v1, &localX0, &localY0)) {
                        float localX1 = localX0 + (float) glyph->sourceWidth;
                        float localY1 = localY0 + (float) glyph->sourceHeight;

                        drawMultiColoredTextureWithTransform(
                            gl,
                            glyphTexId,
                            transform,
                            localX0,
                            localY0,
                            localX1,
                            localY1,
                            u0,
                            v0,
                            u1,
                            v1,
                            r0,
                            g0,
                            b0,
                            r1,
                            g1,
                            b1,
                            r2,
                            g2,
                            b2,
                            r3,
                            g3,
                            b3,
                            alpha
                        );
                        drewSuccessfully = true;
                    }
                }

                cursorX += glyph->shift;
                gradientX += glyph->shift;
                if (drewSuccessfully && hasNext) {
                    float kern = TextUtils_getKerningOffset(glyph, nextCh);
                    cursorX += kern;
                    gradientX += kern;
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

static void glDrawText(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, float lineSeparation) {
    drawText(
        renderer,
        text,
        x,
        y,
        xscale,
        yscale,
        angleDeg,
        lineSeparation,
        renderer->drawColor,
        renderer->drawColor,
        renderer->drawColor,
        renderer->drawColor,
        renderer->drawAlpha
    );
}

static void glDrawTextColor(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, int32_t _c1, int32_t _c2, int32_t _c3, int32_t _c4, float alpha, float lineSeparation) {
    drawText(
        renderer,
        text,
        x,
        y,
        xscale,
        yscale,
        angleDeg,
        lineSeparation,
        _c1,
        _c2,
        _c3,
        _c4,
        alpha
    );
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
    gl->glTextures = (GLuint *)safeRealloc(gl->glTextures, gl->textureCount * sizeof(GLuint));
    gl->textureWidths = (int32_t *)safeRealloc(gl->textureWidths, gl->textureCount * sizeof(int32_t));
    gl->textureHeights = (int32_t *)safeRealloc(gl->textureHeights, gl->textureCount * sizeof(int32_t));
    gl->textureLoaded = (bool *)safeRealloc(gl->textureLoaded, gl->textureCount * sizeof(bool));
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
    dw->tpag.items = (TexturePageItem *)safeRealloc(dw->tpag.items, dw->tpag.count * sizeof(TexturePageItem));
    memset(&dw->tpag.items[newIndex], 0, sizeof(TexturePageItem));
    dw->tpag.items[newIndex].texturePageId = -1;
    return newIndex;
}

static int32_t glCreateSurface(Renderer* renderer, int32_t width, int32_t height) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);

    // Save the current FBO binding so creating a surface doesn't change the active render target.
    GLint prevBinding = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevBinding);

    uint32_t surfaceIndex = GLCommon_findOrAllocateSurfaceSlot(&gl->surfaces, &gl->surfaceTexture, &gl->surfaceWidth, &gl->surfaceHeight, &gl->surfaceCount);

    glGenFramebuffers(1, &gl->surfaces[surfaceIndex]);

    glGenTextures(1, &gl->surfaceTexture[surfaceIndex]);
    glBindTexture(GL_TEXTURE_2D, gl->surfaceTexture[surfaceIndex]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceIndex]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->surfaceTexture[surfaceIndex], 0);

    gl->surfaceWidth[surfaceIndex] = width;
    gl->surfaceHeight[surfaceIndex] = height;

    fprintf(stderr, "GL: Created surface %u with size (%dx%d)\n", surfaceIndex, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) prevBinding);

    return (int32_t) surfaceIndex;
}

static int32_t glEnsureApplicationSurface(Renderer* renderer, int32_t width, int32_t height) {
    GLRenderer* gl = (GLRenderer*) renderer;
    int32_t id = renderer->runner->applicationSurfaceId;

    bool needsCreate = (id < 0) || ((uint32_t) id >= gl->surfaceCount) || (gl->surfaces[id] == 0);
    if (needsCreate) {
        id = glCreateSurface(renderer, width, height);
        // Publish immediately so anything that re-queries the runner during this frame sees the new ID.
        renderer->runner->applicationSurfaceId = id;
        return id;
    }

    if (gl->surfaceWidth[id] != width || gl->surfaceHeight[id] != height) {
        renderer->vtable->surfaceResize(renderer, id, width, height);
    }
    return id;
}

static void glSurfaceFree(Renderer* renderer, int32_t surfaceID) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);

    if (0 > surfaceID || (uint32_t) surfaceID >= gl->surfaceCount) return;

    // Freeing the application_surface is a no-op from GML; the runner manages its lifecycle via application_surface_enable.
    if (surfaceID == renderer->runner->applicationSurfaceId) return;

    if (gl->surfaceTexture[surfaceID] != 0) glDeleteTextures(1, &gl->surfaceTexture[surfaceID]);
    if (gl->surfaces[surfaceID] != 0) glDeleteFramebuffers(1, &gl->surfaces[surfaceID]);
    gl->surfaces[surfaceID] = 0;
    gl->surfaceTexture[surfaceID] = 0;
    gl->surfaceWidth[surfaceID] = 0;
    gl->surfaceHeight[surfaceID] = 0;
    fprintf(stderr, "GL: Freed Surface %u\n", surfaceID);
}

static void glSurfaceResize(Renderer* renderer, int32_t surfaceID, int32_t width, int32_t height) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);

    if (0 > surfaceID || (uint32_t) surfaceID >= gl->surfaceCount) return;
    if (gl->surfaces[surfaceID] == 0) return;
    if (gl->surfaceWidth[surfaceID] == width && gl->surfaceHeight[surfaceID] == height) return;

    GLint prevBinding = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevBinding);

    if (gl->surfaceTexture[surfaceID] != 0) glDeleteTextures(1, &gl->surfaceTexture[surfaceID]);

    glGenTextures(1, &gl->surfaceTexture[surfaceID]);
    glBindTexture(GL_TEXTURE_2D, gl->surfaceTexture[surfaceID]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceID]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->surfaceTexture[surfaceID], 0);

    gl->surfaceWidth[surfaceID] = width;
    gl->surfaceHeight[surfaceID] = height;

    fprintf(stderr, "GL: Resized Surface %u Size (%dx%d)\n", surfaceID, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) prevBinding);
}

static bool glSurfaceExists(Renderer* renderer, int32_t surfaceId) {
    GLRenderer* gl = (GLRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return false;
    return gl->surfaces[surfaceId] != 0;
}

static bool glSurfaceGetPixels(Renderer* renderer, int32_t surfaceId, uint8_t* outRGBA) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    return GLCommon_surfaceGetPixels(gl->surfaces, gl->surfaceWidth, gl->surfaceHeight, gl->surfaceCount, surfaceId, outRGBA);
}

static bool glSetRenderTarget(Renderer* renderer, int32_t surfaceId, bool implicitApplicationSurface) {
    GLRenderer* gl = (GLRenderer*) renderer;

    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return false;
    if (gl->surfaces[surfaceId] == 0) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceId]);

    if (surfaceId == renderer->runner->applicationSurfaceId && implicitApplicationSurface) {
        glViewport(gl->base.CPortX, gl->base.CPortY, gl->base.CPortW, gl->base.CPortH);
        glEnable(GL_SCISSOR_TEST);
        renderer->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION] = renderer->previousViewMatrix;
        glShaderSettingsRefresh(renderer);
        return true;
    }

    // Normal surface bind: surface-local ortho covering the whole surface, no scissor.
    Matrix4f projection;
    Matrix4f_identity(&projection);
    Matrix4f_ortho(&projection, 0.0f, (float) gl->surfaceWidth[surfaceId], 0.0f, (float) gl->surfaceHeight[surfaceId], -1.0f, 1.0f);
    glViewport(0, 0, gl->surfaceWidth[surfaceId], gl->surfaceHeight[surfaceId]);
    glDisable(GL_SCISSOR_TEST);
    renderer->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION] = projection;
    glShaderSettingsRefresh(renderer);

    return true;
}

static void glSurfaceCopy(Renderer* renderer, int32_t destSurfaceID, int32_t destX, int32_t destY, int32_t srcSurfaceID, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, bool part) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    GLCommon_surfaceBlit(gl->surfaces, gl->surfaceWidth, gl->surfaceHeight, gl->surfaceCount, destSurfaceID, destX, destY, srcSurfaceID, srcX, srcY, srcW, srcH, part);
}

static float glGetSurfaceWidth(Renderer* renderer, int32_t surfaceId) {
    GLRenderer* gl = (GLRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return 0.0f;
    if (gl->surfaces[surfaceId] == 0) return 0.0f;
    return (float) gl->surfaceWidth[surfaceId];
}

static float glGetSurfaceHeight(Renderer* renderer, int32_t surfaceId) {
    GLRenderer* gl = (GLRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return 0.0f;
    if (gl->surfaces[surfaceId] == 0) return 0.0f;
    return (float) gl->surfaceHeight[surfaceId];
}

static void glDrawSurface(Renderer* renderer, int32_t surfaceID, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;

    if (0 > surfaceID || (uint32_t) surfaceID >= gl->surfaceCount) return;
    if (gl->surfaceTexture[surfaceID] == 0) return;

    GLuint texId = gl->surfaceTexture[surfaceID];
    int32_t texW = gl->surfaceWidth[surfaceID];
    int32_t texH = gl->surfaceHeight[surfaceID];

    if (0 > srcWidth) { srcLeft = 0; srcTop = 0; srcWidth = texW; srcHeight = texH; }

    float u0 = (float) srcLeft / (float) texW;
    float v0 = (float) srcTop / (float) texH;
    float u1 = (float) (srcLeft + srcWidth) / (float) texW;
    float v1 = (float) (srcTop + srcHeight) / (float) texH;

    float localX1 = (float) srcWidth;
    float localY1 = (float) srcHeight;

    drawTexture(
        gl,
        texId,
        x,
        y,
        0.0f,
        0.0f,
        localX1,
        localY1,
        u0,
        v0,
        u1,
        v1,
        xscale,
        yscale,
        angleDeg,
        color,
        alpha
    );
}

static void glDrawSurfaceTiled(Renderer* renderer, int32_t surfaceID, float x, float y, float xscale, float yscale, float roomW, float roomH, uint32_t color, float alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;

    if (0 > surfaceID || (uint32_t) surfaceID >= gl->surfaceCount) return;
    if (gl->surfaceTexture[surfaceID] == 0) return;

    GLuint texId = gl->surfaceTexture[surfaceID];
    int32_t texW = gl->surfaceWidth[surfaceID];
    int32_t texH = gl->surfaceHeight[surfaceID];

    float tileW = (float) texW * fabsf(xscale);
    float tileH = (float) texH * fabsf(yscale);

    drawTiled(
        gl,
        texId,
        x,
        y,
        tileW,
        tileH,
        true,
        true,
        roomW,
        roomH,
        0.0f,
        xscale * (float) texW,
        0.0f,
        yscale * (float) texH,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        color,
        alpha
    );
}

static int32_t glCreateSpriteFromSurface(Renderer* renderer, int32_t surfaceID, int32_t x, int32_t y, int32_t w, int32_t h, bool removeback, bool smooth, int32_t xorig, int32_t yorig) {
    // TODO: implement these
    (void)smooth;
    (void)removeback;
    GLRenderer* gl = (GLRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 >= w || 0 >= h) return -1;
    if (0 > surfaceID || (uint32_t) surfaceID >= gl->surfaceCount) return -1;
    if (gl->surfaces[surfaceID] == 0) return -1;

    // Flush any pending draws before reading pixels
    flushBatch(gl);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, gl->surfaces[surfaceID]);

    uint8_t* pixels = (uint8_t *)safeMalloc((size_t) w * (size_t) h * 4);
    if (pixels == nullptr) return -1;

    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // Create a new GL texture from the captured pixels
    GLuint newTexId;
    glGenTextures(1, &newTexId);
    glBindTexture(GL_TEXTURE_2D, newTexId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

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
    sprite->tpagIndices = (int32_t *)safeMalloc(sizeof(int32_t));
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
    (void)renderer;
    return glIsEnabled(GL_BLEND);
}

static void glGpuSetAlphaTestEnable(Renderer* renderer, bool enable) {
    GLRenderer* gl = (GLRenderer*) renderer;
    if (gl->alphaTestEnable == enable) return;
    flushBatch(gl);
    gl->alphaTestEnable = enable;
    glShaderSettingsRefresh(renderer);
}

static void glGpuSetAlphaTestRef(Renderer* renderer, uint8_t ref) {
    GLRenderer* gl = (GLRenderer*) renderer;
    float refF = ref / 255.0f;
    if (gl->alphaTestRef == refF) return;
    flushBatch(gl);
    gl->alphaTestRef = refF;
    glShaderSettingsRefresh(renderer); 
}

static void glGpuSetColorWriteEnable(Renderer* renderer, bool red, bool green, bool blue, bool alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    gl->colorWriteR = red;
    gl->colorWriteG = green;
    gl->colorWriteB = blue;
    gl->colorWriteA = alpha;
    glColorMask(red, green, blue, alpha);
}

static void glGpuGetColorWriteEnable(Renderer* renderer, bool* red, bool* green, bool* blue, bool* alpha) {
    GLRenderer* gl = (GLRenderer*) renderer;
    *red = gl->colorWriteR;
    *green = gl->colorWriteG;
    *blue = gl->colorWriteB;
    *alpha = gl->colorWriteA;
}

static void glGpuSetFog(Renderer* renderer, bool enable, uint32_t color) {
    GLRenderer* gl = (GLRenderer*) renderer;
    if (gl->fogEnable == enable && gl->fogColor == color) return;
    flushBatch(gl);
    gl->fogEnable = enable;
    gl->fogColor = color;
    glShaderSettingsRefresh(renderer);
}

static int32_t glShaderGetUniform(Renderer* renderer, int32_t shaderIndex, char* uniform) {
    GLRenderer* gl = (GLRenderer*) renderer;
    GMLShader* shader = &gl->gmlShaders[shaderIndex];

    repeat(shader->uniformCount, b) {
        if (strcmp(shader->uniforms[b].name, uniform) == 0) {
            return b;
        }
    }

    fprintf(stderr, "GL: Uniform %s not found for shader %d!\n", uniform, shaderIndex);
    return -1;
}

static int32_t glShaderGetSamplerIndex(Renderer* renderer, int32_t shaderIndex, char* uniform) {
    GLRenderer* gl = (GLRenderer*) renderer;
    GMLShader* shader = &gl->gmlShaders[shaderIndex];

    repeat(shader->uniformCount, b) {
        if (strcmp(shader->uniforms[b].name, uniform) == 0) {
            return shader->uniforms[b].samplerSlot;
        }
    }

    fprintf(stderr, "GL: Sampler Index %s not found for shader %d!\n", uniform, shaderIndex);
    return -1;
}

static void glShaderSetUniformF(Renderer* renderer, int32_t handle, MAYBE_UNUSED int32_t count, float value1, float value2, float value3, float value4) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);

    if (handle != -1 && renderer->currentShader != -1) {
        GMLShader* shader = &gl->gmlShaders[renderer->currentShader];
        GLShaderUniform uniform = shader->uniforms[handle];

        if (uniform.type == GL_FLOAT) {
            glUniform1f(uniform.location, value1);
        } else if (uniform.type == GL_FLOAT_VEC2) {
            glUniform2f(uniform.location, value1, value2);
        } else if (uniform.type == GL_FLOAT_VEC3) {
            glUniform3f(uniform.location, value1, value2, value3);
        } else if (uniform.type == GL_FLOAT_VEC4) {
            glUniform4f(uniform.location, value1, value2, value3, value4);
        }
    }
}

static void glShaderSetUniformFArray(Renderer* renderer, int32_t handle, float* values, uint32_t count) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);

    if (handle != -1 && renderer->currentShader != -1) {
        GMLShader* shader = &gl->gmlShaders[renderer->currentShader];
        GLShaderUniform uniform = shader->uniforms[handle];

        if (uniform.type == GL_FLOAT) glUniform1fv(uniform.location, count, values);
        else if (uniform.type == GL_FLOAT_VEC2) glUniform2fv(uniform.location, count / 2, values);
        else if (uniform.type == GL_FLOAT_VEC3) glUniform3fv(uniform.location, count / 3, values);
        else if (uniform.type == GL_FLOAT_VEC4) glUniform4fv(uniform.location, count / 4, values);
        else if (uniform.type == GL_FLOAT_MAT4) glUniformMatrix4fv(uniform.location, count / 16, GL_FALSE, values);
        else if (uniform.type == GL_FLOAT_MAT3) glUniformMatrix3fv(uniform.location, count / 9, GL_FALSE, values);
        else if (uniform.type == GL_FLOAT_MAT2) glUniformMatrix2fv(uniform.location, count / 4, GL_FALSE, values);
    }
}

static void glShaderSetUniformI(Renderer* renderer, int32_t handle, MAYBE_UNUSED int32_t count, int32_t value1, int32_t value2, int32_t value3, int32_t value4) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);

    if (handle != -1 && renderer->currentShader != -1) {
        GMLShader* shader = &gl->gmlShaders[renderer->currentShader];
        GLShaderUniform uniform = shader->uniforms[handle];

        if (uniform.type == GL_INT) glUniform1i(uniform.location, value1);
        else if (uniform.type == GL_INT_VEC2) glUniform2i(uniform.location, value1, value2);
        else if (uniform.type == GL_INT_VEC3) glUniform3i(uniform.location, value1, value2, value3);
        else if (uniform.type == GL_INT_VEC4) glUniform4i(uniform.location, value1, value2, value3, value4);
    }
}

static uint32_t glSpriteGetTexture(Renderer* renderer, int32_t tpagIndex) {
    GLRenderer* gl = (GLRenderer*) renderer;
    TexturePageItem* tpag;
    GLuint texId;
    int32_t texW, texH;
    if (!resolveSpriteTexture(gl, tpagIndex, &tpag, &texId, &texW, &texH)) return 0;
    return (uint32_t) (tpagIndex + 1);
}

// Decode a texture handle produced by glSpriteGetTexture (sprite/tpag) or glSurfaceGetTexture (surface)
// back into its GL id and pixel size. *outTpag is set to NULL for surface handles (no tpag sub-region).
// Returns false for the 0 ("no texture") handle or an unresolvable one.
static bool glResolveTextureHandle(GLRenderer* gl, uint32_t texHandle, TexturePageItem** outTpag, GLuint* outTexId, int32_t* outTexW, int32_t* outTexH) {
    if (texHandle == 0) return false;
    if (texHandle & GL_SURFACE_TEXTURE_FLAG) {
        uint32_t sid = texHandle & ~GL_SURFACE_TEXTURE_FLAG;
        if (sid >= gl->surfaceCount || gl->surfaceTexture[sid] == 0) return false;
        if (outTpag) *outTpag = nullptr;
        *outTexId = gl->surfaceTexture[sid];
        *outTexW = gl->surfaceWidth[sid];
        *outTexH = gl->surfaceHeight[sid];
        return true;
    }
    return resolveSpriteTexture(gl, (int32_t) texHandle - 1, outTpag, outTexId, outTexW, outTexH);
}

// surface_get_texture: returns a handle that texture_get_texel_*/texture_get_uvs/texture_set_stage resolve.
static uint32_t glSurfaceGetTexture(Renderer* renderer, int32_t surfaceID) {
    GLRenderer* gl = (GLRenderer*) renderer;
    if (surfaceID < 0 || (uint32_t) surfaceID >= gl->surfaceCount) return 0;
    if (gl->surfaceTexture[surfaceID] == 0) return 0;
    return GL_SURFACE_TEXTURE_FLAG | (uint32_t) surfaceID;
}

static void glTextureSetStage(Renderer* renderer, int32_t slot, uint32_t texHandle) {
    GLRenderer* gl = (GLRenderer*) renderer;
    flushBatch(gl);
    if (slot < 0) {
        fprintf(stderr, "GL: Invalid Texture Stage\n");
        return;
    }
    TexturePageItem* tpag;
    GLuint texID = 0;
    int32_t texW, texH;
    glResolveTextureHandle(gl, texHandle, &tpag, &texID, &texW, &texH);
    if (slot == 0) {
        gl->currentTextureId = texID;
    }
    if (slot > MAX_TEXTURE_STAGES) {
        fprintf(stderr, "GL: Texture Stage Higher Than Max\n");
        return;
    }
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, texID);
    glActiveTexture(GL_TEXTURE1);

}

// Look up a texture's pixel size from the renderer's own tables.
MAYBE_UNUSED static bool lookupTextureSize(GLRenderer* gl, uint32_t texID, int32_t* outW, int32_t* outH) {
    repeat(gl->textureCount, i) {
        if (gl->textureLoaded[i] && gl->glTextures[i] == (GLuint) texID) {
            *outW = gl->textureWidths[i];
            *outH = gl->textureHeights[i];
            return true;
        }
    }

    repeat(gl->textureCount, i) {
        if (gl->surfaceTexture[i] == (GLuint) texID) {
            *outW = gl->surfaceWidth[i];
            *outH = gl->surfaceHeight[i];
            return true;
        }
    }

    return false;
}

static float glTextureGetTexelWidth(Renderer* renderer, uint32_t texHandle) {
    GLRenderer* gl = (GLRenderer*) renderer;
    TexturePageItem* tpag;
    GLuint texId;
    int32_t width = 0, height = 0;
    if (!glResolveTextureHandle(gl, texHandle, &tpag, &texId, &width, &height) || 0 >= width) return 1.0f;
    return 1.0f / (float) width;
}

static float glTextureGetTexelHeight(Renderer* renderer, uint32_t texHandle) {
    GLRenderer* gl = (GLRenderer*) renderer;
    TexturePageItem* tpag;
    GLuint texId;
    int32_t width = 0, height = 0;
    if (!glResolveTextureHandle(gl, texHandle, &tpag, &texId, &width, &height) || 0 >= height) return 1.0f;
    return 1.0f / (float) height;
}

static bool glTextureGetUVs(Renderer* renderer, uint32_t texHandle, float* outUVs) {
    GLRenderer* gl = (GLRenderer*) renderer;
    TexturePageItem* tpag;
    GLuint texId;
    int32_t width = 0, height = 0;
    if (!glResolveTextureHandle(gl, texHandle, &tpag, &texId, &width, &height) || 0 >= width || 0 >= height) return false;
    // Surface handles cover the whole texture (no tpag sub-region).
    if (tpag == nullptr) {
        outUVs[0] = 0.0f; outUVs[1] = 0.0f; outUVs[2] = 1.0f; outUVs[3] = 1.0f;
        return true;
    }
    float divW = 1.0f / (float) width;
    float divH = 1.0f / (float) height;
    outUVs[0] = (float) tpag->sourceX * divW;                              // left
    outUVs[1] = (float) tpag->sourceY * divH;                             // top
    outUVs[2] = outUVs[0] + (float) tpag->sourceWidth * divW;            // right
    outUVs[3] = outUVs[1] + (float) tpag->sourceHeight * divH;           // bottom
    return true;
}

static bool glShaderIsCompiled(Renderer* renderer, int32_t shaderID) {
    GLRenderer* gl = (GLRenderer*) renderer;
    DataWin* dw = gl->base.dataWin;
    if (0 > shaderID || (uint32_t) shaderID >= dw->shdr.count) return false;
    return gl->gmlShaders[shaderID].compiled;
}

static bool glShadersSupported(void) {
    return true;
}

// ===[ Vtable ]===

static RendererVtable glVtable;

// ===[ Public API ]===

Renderer* GLRenderer_create(void) {
    GLRenderer* gl = (GLRenderer *)safeCalloc(1, sizeof(GLRenderer));
    gl->base.vtable = &glVtable;
    glVtable.init = glInit;
    glVtable.destroy = glDestroy;
    glVtable.beginFrame = glBeginFrame;
    glVtable.endFrameInit = glEndFrameInit;
    glVtable.endFrameEnd = glEndFrameEnd;
    glVtable.beginView = glBeginView;
    glVtable.endView = glEndView;
    glVtable.applyProjection = glApplyProjection;
    glVtable.beginGUI = glBeginGUI;
    glVtable.setGuiProjection = glSetGuiProjection;
    glVtable.endGUI = glEndGUI;
    glVtable.drawSprite = glDrawSprite;
    glVtable.drawSpritePos = glDrawSpritePos;
    glVtable.drawSpritePart = glDrawSpritePart;
    glVtable.drawRectangle = glDrawRectangle;
    glVtable.drawRectangleColor = glDrawRectangleColor;
    glVtable.drawLine = glDrawLine;
    glVtable.drawLineColor = glDrawLineColor;
    glVtable.drawTriangle = glDrawTriangle;
    glVtable.drawText = glDrawText;
    glVtable.drawTextColor = glDrawTextColor;
    glVtable.flush = glRendererFlush;
    glVtable.clearScreen = glClearScreen;
    glVtable.createSpriteFromSurface = glCreateSpriteFromSurface;
    glVtable.deleteSprite = glDeleteSprite;
    glVtable.gpuSetBlendMode = glGpuSetBlendMode;
    glVtable.gpuSetBlendModeExt = glGpuSetBlendModeExt;
    glVtable.gpuSetBlendEnable = glGpuSetBlendEnable;
    glVtable.gpuSetAlphaTestEnable = glGpuSetAlphaTestEnable;
    glVtable.gpuSetAlphaTestRef = glGpuSetAlphaTestRef;
    glVtable.gpuSetColorWriteEnable = glGpuSetColorWriteEnable;
    glVtable.gpuGetColorWriteEnable = glGpuGetColorWriteEnable;
    glVtable.gpuSetFog = glGpuSetFog;
    glVtable.gpuGetBlendEnable = glGpuGetBlendEnable;
    glVtable.drawTile = nullptr;
    glVtable.drawSpriteTiled = glDrawSpriteTiled;
    glVtable.createSurface = glCreateSurface;
    glVtable.surfaceExists = glSurfaceExists;
    glVtable.setRenderTarget = glSetRenderTarget;
    glVtable.ensureApplicationSurface = glEnsureApplicationSurface;
    glVtable.surfaceCopy = glSurfaceCopy;
    glVtable.surfaceGetPixels = glSurfaceGetPixels;
    glVtable.getSurfaceWidth = glGetSurfaceWidth;
    glVtable.getSurfaceHeight = glGetSurfaceHeight;
    glVtable.drawSurface = glDrawSurface;
    glVtable.drawSurfaceTiled = glDrawSurfaceTiled;
    glVtable.surfaceResize = glSurfaceResize;
    glVtable.surfaceFree = glSurfaceFree;
    glVtable.gpuSetShader = glGpuSetShader,
    glVtable.gpuResetShader = glGpuResetShader,
    glVtable.shaderGetUniform = glShaderGetUniform,
    glVtable.shaderSetUniformF = glShaderSetUniformF,
    glVtable.shaderSetUniformFArray = glShaderSetUniformFArray,
    glVtable.shaderSetUniformI = glShaderSetUniformI,
    glVtable.spriteGetTexture = glSpriteGetTexture,
    glVtable.surfaceGetTexture = glSurfaceGetTexture,
    glVtable.textureGetTexelWidth = glTextureGetTexelWidth,
    glVtable.textureGetTexelHeight = glTextureGetTexelHeight,
    glVtable.textureGetUVs = glTextureGetUVs,
    glVtable.shaderGetSamplerIndex = glShaderGetSamplerIndex,
    glVtable.textureSetStage = glTextureSetStage,
    glVtable.shaderIsCompiled = glShaderIsCompiled,
    glVtable.shadersSupported = glShadersSupported,

    gl->base.drawColor = 0xFFFFFF; // white (BGR)
    gl->base.drawAlpha = 1.0f;
    gl->base.drawFont = -1;
    gl->base.drawHalign = 0;
    gl->base.drawValign = 0;
    gl->base.circlePrecision = 24;
    gl->base.currentShader = -1;
    return (Renderer*) gl;
}
