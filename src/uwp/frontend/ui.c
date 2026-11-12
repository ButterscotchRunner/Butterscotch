#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <stdio.h>
#include <stddef.h>
#include "stb_image.h"
#include "log.h"
#include "data_win.h"

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include "nuklear.h"
#include "ui.h"
#include "controller_glyphs.h"

#define UI_GLSL_HEADER "#version 300 es\nprecision highp float;\n"

typedef struct {
    float position[2];
    float uv[2];
    nk_byte color[4];
} UiVertex;

static struct nk_context ctx;
static struct nk_font_atlas atlas;
static struct nk_font* body, *heading;
static struct nk_buffer commands, vertices, indices;
static struct nk_draw_null_texture nullTexture;
static GLuint program, vao, vbo, ebo, fontTexture, glyphTexture;
static GLint projection;
static bool initialized;
static char loadingTitle[1024];

static bool loadGlyphs(void) {
    char* base = SDL_GetBasePath();
    if (!base) return false;
    char path[1024];
    snprintf(path, sizeof(path), "%sAssets/ControllerGlyphs.png", base);
    SDL_free(base);
    size_t size;
    void* data = SDL_LoadFile(path, &size);
    if (!data) return false;
    int width, height, channels;
    unsigned char* pixels = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
    SDL_free(data);
    if (!pixels) {
        SDL_SetError("Cannot decode controller glyphs: %s", stbi_failure_reason());
        return false;
    }
    if (width != UWP_GLYPH_ATLAS_WIDTH || height != UWP_GLYPH_ATLAS_HEIGHT) {
        stbi_image_free(pixels);
        SDL_SetError("Controller glyph atlas dimensions do not match its metadata");
        return false;
    }
    glGenTextures(1, &glyphTexture);
    glBindTexture(GL_TEXTURE_2D, glyphTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    return true;
}

static struct nk_image glyphImage(int glyph) {
    int x = glyph % UWP_GLYPH_COLUMNS * UWP_GLYPH_STRIDE + UWP_GLYPH_PADDING;
    int y = glyph / UWP_GLYPH_COLUMNS * UWP_GLYPH_STRIDE + UWP_GLYPH_PADDING;
    return nk_subimage_id(glyphTexture, UWP_GLYPH_ATLAS_WIDTH, UWP_GLYPH_ATLAS_HEIGHT, nk_rect((float)x, (float)y, UWP_GLYPH_SIZE, UWP_GLYPH_SIZE));
}

static void glyphAt(int glyph, float x, float y) {
    nk_layout_space_push(&ctx, nk_rect(x, y, 32, 32));
    nk_image(&ctx, glyphImage(glyph));
}

static void hintAt(int glyph, const char* label, float x, float y, float width) {
    glyphAt(glyph, x, y);
    nk_layout_space_push(&ctx, nk_rect(x + 40, y, width - 40, 32));
    nk_label_colored(&ctx, label, NK_TEXT_LEFT, nk_rgb(178, 178, 178));
}

static GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (!shader) {
        SDL_SetError("Could not create the selector shader");
        return 0;
    }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        logError("Nuklear shader: %s\n", log);
        SDL_SetError("Could not compile the selector shader");
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool uwpUiInit(void) {
    if (initialized) return true;
    GLuint vs = compileShader(GL_VERTEX_SHADER, UI_GLSL_HEADER
        "layout(location = 0) in vec2 position;\n"
        "layout(location = 1) in vec2 uv;\n"
        "layout(location = 2) in vec4 color;\n"
        "uniform mat4 projection;\n"
        "out vec2 texcoord;\n"
        "out vec4 tint;\n"
        "void main() {\n"
        "    texcoord = uv;\n"
        "    tint = color;\n"
        "    gl_Position = projection * vec4(position, 0, 1);\n"
        "}\n");
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, UI_GLSL_HEADER
        "in vec2 texcoord;\n"
        "in vec4 tint;\n"
        "uniform sampler2D atlas;\n"
        "out vec4 pixel;\n"
        "void main() {\n"
        "    pixel = tint * texture(atlas, texcoord);\n"
        "}\n");
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    program = glCreateProgram();
    if (!program) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        SDL_SetError("Could not create the selector shader program");
        return false;
    }
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        logError("Nuklear shader link: %s\n", log);
        SDL_SetError("Could not link the selector shader");
        glDeleteProgram(program);
        program = 0;
        return false;
    }
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "atlas"), 0);
    projection = glGetUniformLocation(program, "projection");
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void *)offsetof(UiVertex, position));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void *)offsetof(UiVertex, uv));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UiVertex), (void *)offsetof(UiVertex, color));

    nk_font_atlas_init_default(&atlas);
    nk_font_atlas_begin(&atlas);
    char* base = SDL_GetBasePath();
    if (base) {
        char path[1024];
        snprintf(path, sizeof(path), "%sAssets/Roboto-Regular.ttf", base);
        body = nk_font_atlas_add_from_file(&atlas, path, 24, NULL);
        heading = nk_font_atlas_add_from_file(&atlas, path, 36, NULL);
        SDL_free(base);
    }
    if (!body) body = nk_font_atlas_add_default(&atlas, 24, NULL);
    if (!heading) heading = nk_font_atlas_add_default(&atlas, 36, NULL);
    if (!body || !heading) {
        SDL_SetError("Could not load selector fonts");
        uwpUiShutdown();
        return false;
    }
    int width, height;
    const void* pixels = nk_font_atlas_bake(&atlas, &width, &height, NK_FONT_ATLAS_RGBA32);
    if (!pixels) {
        SDL_SetError("Could not build the selector font atlas");
        uwpUiShutdown();
        return false;
    }
    glGenTextures(1, &fontTexture);
    glBindTexture(GL_TEXTURE_2D, fontTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    nk_font_atlas_end(&atlas, nk_handle_id(fontTexture), &nullTexture);
    if (!nk_init_default(&ctx, &body->handle)) {
        SDL_SetError("Could not initialize Nuklear");
        uwpUiShutdown();
        return false;
    }
    nk_buffer_init_default(&commands);
    nk_buffer_init_default(&vertices);
    nk_buffer_init_default(&indices);

    struct nk_color colors[NK_COLOR_COUNT];
    for (int i = 0; i < NK_COLOR_COUNT; i++) colors[i] = nk_rgb(48, 48, 48);
    colors[NK_COLOR_TEXT] = nk_rgb(243, 243, 243);
    colors[NK_COLOR_WINDOW] = nk_rgb(28, 28, 28);
    colors[NK_COLOR_BORDER] = nk_rgb(72, 72, 72);
    colors[NK_COLOR_BUTTON] = nk_rgb(48, 48, 48);
    colors[NK_COLOR_BUTTON_HOVER] = nk_rgb(66, 66, 66);
    colors[NK_COLOR_BUTTON_ACTIVE] = nk_rgb(135, 78, 27);
    colors[NK_COLOR_SELECT] = nk_rgb(37, 37, 37);
    colors[NK_COLOR_SELECT_ACTIVE] = nk_rgb(113, 68, 31);
    colors[NK_COLOR_SLIDER] = nk_rgb(19, 19, 19);
    colors[NK_COLOR_SLIDER_CURSOR] = nk_rgb(222, 161, 82);
    colors[NK_COLOR_SLIDER_CURSOR_HOVER] = colors[NK_COLOR_SLIDER_CURSOR];
    colors[NK_COLOR_SLIDER_CURSOR_ACTIVE] = colors[NK_COLOR_SLIDER_CURSOR];
    nk_style_from_table(&ctx, colors);
    ctx.style.window.padding = nk_vec2(40, 32);
    ctx.style.window.group_padding = nk_vec2(12, 10);
    ctx.style.window.spacing = nk_vec2(8, 8);
    ctx.style.selectable.rounding = 8;
    ctx.style.selectable.padding = nk_vec2(20, 8);
    ctx.style.selectable.text_normal_active = nk_rgb(255, 213, 154);
    ctx.style.button.rounding = 8;
    ctx.style.button.border = 0;
    if (!loadGlyphs()) {
        uwpUiShutdown();
        return false;
    }
    initialized = true;
    return true;
}

void uwpUiShutdown(void) {
    // Also release resources when initialization stops before the UI is ready.
    if (!program) return;
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &fontTexture);
    glDeleteTextures(1, &glyphTexture);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    nk_buffer_free(&commands);
    nk_buffer_free(&vertices);
    nk_buffer_free(&indices);
    nk_free(&ctx);
    if (atlas.permanent.alloc) nk_font_atlas_clear(&atlas);
    SDL_zero(commands);
    SDL_zero(vertices);
    SDL_zero(indices);
    program = vao = vbo = ebo = fontTexture = glyphTexture = 0;
    body = heading = NULL;
    loadingTitle[0] = '\0';
    initialized = false;
}

void uwpUiBegin(void) {
    nk_input_begin(&ctx);
    nk_input_end(&ctx);
}

void uwpUiSelector(const char* const* games, int count, int selected, const char* status) {
    if (nk_begin(&ctx, "Game library", nk_rect(0, 0, UWP_UI_WIDTH, UWP_UI_HEIGHT), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {
        nk_layout_space_begin(&ctx, NK_STATIC, 656, 16);
        nk_layout_space_push(&ctx, nk_rect(0, 0, 980, 44));
        nk_style_push_font(&ctx, &heading->handle);
        nk_label(&ctx, "Butterscotch", NK_TEXT_LEFT);
        nk_style_pop_font(&ctx);
        char text[64];
        snprintf(text, sizeof(text), "%d %s", count, count == 1 ? "game" : "games");
        nk_layout_space_push(&ctx, nk_rect(1040, 8, 160, 32));
        nk_label_colored(&ctx, text, NK_TEXT_RIGHT, nk_rgb(178, 178, 178));
        nk_layout_space_push(&ctx, nk_rect(0, 56, 1200, 28));
        nk_label_colored(&ctx, "Game library     /butterscotch/<game>/data.win", NK_TEXT_LEFT, nk_rgb(178, 178, 178));
        nk_layout_space_push(&ctx, nk_rect(0, 104, 1200, 368));
        if (nk_group_begin(&ctx, "Games", NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {
            int first = selected / 7 * 7;
            for (int i = first; i < count && i < first + 7; i++) {
                nk_layout_row_dynamic(&ctx, 42, 1);
                nk_select_label(&ctx, games[i], NK_TEXT_LEFT, i == selected);
            }
            nk_group_end(&ctx);
        }
        nk_layout_space_push(&ctx, nk_rect(0, 490, 1200, 58));
        nk_label_wrap(&ctx, status);
        const char* buttons[] = {"Play", "Refresh", "Options", "Quit"};
        const int glyphs[] = {UWP_GLYPH_A, UWP_GLYPH_X, UWP_GLYPH_MENU, UWP_GLYPH_B};
        for (int i = 0; i < (int)SDL_arraysize(buttons); i++) {
            nk_layout_space_push(&ctx, nk_rect(i * 304.0f, 568, 288, 48));
            nk_button_image_label(&ctx, glyphImage(glyphs[i]), buttons[i], NK_TEXT_CENTERED);
        }
        glyphAt(UWP_GLYPH_D_PAD, 0, 624);
        hintAt(UWP_GLYPH_LEFT_STICK, "Select", 40, 624, 200);
        hintAt(UWP_GLYPH_MENU, "Launch options", 288, 624, 300);
        nk_layout_space_end(&ctx);
    }
    nk_end(&ctx);
}

static bool beginDialog(const char* backdrop, const char* title, struct nk_rect bounds) {
    nk_style_push_style_item(&ctx, &ctx.style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 184)));
    nk_begin(&ctx, backdrop, nk_rect(0, 0, UWP_UI_WIDTH, UWP_UI_HEIGHT), NK_WINDOW_NO_INPUT | NK_WINDOW_NO_SCROLLBAR);
    nk_end(&ctx);
    nk_style_pop_style_item(&ctx);
    nk_style_push_style_item(&ctx, &ctx.style.window.fixed_background, nk_style_item_color(nk_rgb(36, 36, 36)));
    nk_style_push_vec2(&ctx, &ctx.style.window.padding, nk_vec2(32, 24));
    nk_style_push_float(&ctx, &ctx.style.window.rounding, 12);
    return nk_begin(&ctx, title, bounds, NK_WINDOW_NO_INPUT | NK_WINDOW_NO_SCROLLBAR) != 0;
}

static void endDialog(void) {
    nk_end(&ctx);
    nk_style_pop_float(&ctx);
    nk_style_pop_vec2(&ctx);
    nk_style_pop_style_item(&ctx);
}

void uwpUiLaunchOptions(const char* game, const UwpLaunchOptions* options, int selected) {
    if (beginDialog("Options backdrop", "Launch options", nk_rect(192, 120, 896, 480))) {
        nk_style_push_font(&ctx, &heading->handle);
        nk_layout_row_dynamic(&ctx, 44, 1);
        nk_label(&ctx, "Launch options", NK_TEXT_LEFT);
        nk_style_pop_font(&ctx);
        nk_layout_row_dynamic(&ctx, 30, 1);
        nk_label_colored(&ctx, game, NK_TEXT_LEFT, nk_rgb(255, 205, 139));
        char rows[UWP_OPTION_COUNT][64];
        snprintf(rows[UWP_OPTION_DEBUG_STATS], sizeof(rows[0]), "Debug stats: %s", options->debugStats ? "On" : "Off");
        snprintf(rows[UWP_OPTION_LAZY_TEXTURES], sizeof(rows[0]), "Lazy textures: %s", options->lazyTextures ? "On" : "Off");
        snprintf(rows[UWP_OPTION_16BPP_TEXTURES], sizeof(rows[0]), "16bpp textures: %s", options->texture16bpp ? "On" : "Off");
        for (int i = 0; i < UWP_OPTION_COUNT; i++) {
            nk_layout_row_dynamic(&ctx, 42, 1);
            nk_select_label(&ctx, rows[i], NK_TEXT_LEFT, i == selected);
        }
        nk_layout_space_begin(&ctx, NK_STATIC, 80, 12);
        hintAt(UWP_GLYPH_D_PAD, "Select / change", 0, 0, 240);
        hintAt(UWP_GLYPH_A, "Toggle", 264, 0, 180);
        hintAt(UWP_GLYPH_X, "Defaults", 0, 44, 220);
        glyphAt(UWP_GLYPH_B, 264, 44);
        hintAt(UWP_GLYPH_MENU, "Back", 304, 44, 180);
        nk_layout_space_end(&ctx);
    }
    endDialog();
}

static void drawLoadingBar(uint64_t current, uint64_t total) {
    struct nk_rect bar;
    if (!nk_widget(&bar, &ctx) || bar.w <= 0 || bar.h <= 0) return;
    struct nk_command_buffer* canvas = nk_window_get_canvas(&ctx);
    float rounding = NK_MIN(7.0f, NK_MIN(bar.w, bar.h) * 0.5f);
    nk_fill_rect(canvas, bar, rounding, nk_rgb(19, 19, 19));

    struct nk_rect fill = nk_rect(bar.x + 2, bar.y + 2, bar.w - 4, bar.h - 4);
    if (fill.w <= 0 || fill.h <= 0) return;
    if (total) {
        fill.w *= (float)((double)NK_MIN(current, total) / (double)total);
    } else {
        float width = NK_MIN(100.0f, fill.w);
        float offset = (SDL_GetTicks() % 2400) / 1200.0f;
        if (offset > 1) offset = 2 - offset;
        fill.x += offset * (fill.w - width);
        fill.w = width;
    }
    // Tiny progress values must not produce corners wider than the fill itself.
    rounding = NK_MIN(7.0f, NK_MIN(fill.w, fill.h) * 0.5f);
    nk_fill_rect(canvas, fill, rounding, nk_rgb(222, 161, 82));
}

void uwpUiLoading(const char* game, const char* stage, uint64_t current, uint64_t total) {
    if (beginDialog("Loading backdrop", "Loading game", nk_rect(224, 236, 832, 248))) {
        nk_style_push_font(&ctx, &heading->handle);
        nk_layout_row_dynamic(&ctx, 44, 1);
        nk_label(&ctx, "Loading game", NK_TEXT_LEFT);
        nk_style_pop_font(&ctx);
        nk_layout_row_dynamic(&ctx, 30, 1);
        nk_label_colored(&ctx, game, NK_TEXT_LEFT, nk_rgb(255, 205, 139));
        nk_layout_row_dynamic(&ctx, 32, 1);
        nk_label(&ctx, stage, NK_TEXT_LEFT);
        nk_layout_row_dynamic(&ctx, 18, 1);
        drawLoadingBar(current, total);
        char text[96];
        if (total) {
            if (current > total) current = total;
            snprintf(text, sizeof(text), "%llu%%     %llu / %llu", (unsigned long long)(100 * current / total), (unsigned long long)current, (unsigned long long)total);
        } else {
            snprintf(text, sizeof(text), "Please wait...");
        }
        nk_layout_row_dynamic(&ctx, 28, 1);
        nk_label_colored(&ctx, text, NK_TEXT_LEFT, nk_rgb(178, 178, 178));
    }
    endDialog();
}

void uwpUiRender(int width, int height) {
    const struct nk_draw_vertex_layout_element layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(UiVertex, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(UiVertex, uv)},
        {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(UiVertex, color)},
        {NK_VERTEX_LAYOUT_END}
    };
    struct nk_convert_config config = {0};
    config.vertex_layout = layout;
    config.vertex_size = sizeof(UiVertex);
    config.vertex_alignment = _Alignof(UiVertex);
    config.tex_null = nullTexture;
    config.circle_segment_count = 22;
    config.curve_segment_count = 22;
    config.arc_segment_count = 22;
    config.global_alpha = 1;
    config.shape_AA = NK_ANTI_ALIASING_ON;
    config.line_AA = NK_ANTI_ALIASING_ON;
    nk_buffer_clear(&commands);
    nk_buffer_clear(&vertices);
    nk_buffer_clear(&indices);
    if (nk_convert(&ctx, &commands, &vertices, &indices, &config) != NK_CONVERT_SUCCESS) {
        logError("Nuklear: could not convert UI geometry\n");
        nk_clear(&ctx);
        return;
    }
    const GLfloat matrix[16] = {2.0f / UWP_UI_WIDTH, 0, 0, 0, 0, -2.0f / UWP_UI_HEIGHT, 0, 0, 0, 0, -1, 0, -1, 1, 0, 1};
    glViewport(0, 0, width, height);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glUseProgram(program);
    glUniformMatrix4fv(projection, 1, GL_FALSE, matrix);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ARRAY_BUFFER, vertices.allocated, nk_buffer_memory_const(&vertices), GL_STREAM_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.allocated, nk_buffer_memory_const(&indices), GL_STREAM_DRAW);
    const struct nk_draw_command* cmd;
    uintptr_t offset = 0;
    nk_draw_foreach(cmd, &ctx, &commands) {
        if (!cmd->elem_count) continue;
        glBindTexture(GL_TEXTURE_2D, cmd->texture.id);
        glScissor((GLint)(cmd->clip_rect.x * width / UWP_UI_WIDTH), (GLint)((UWP_UI_HEIGHT - cmd->clip_rect.y - cmd->clip_rect.h) * height / UWP_UI_HEIGHT), (GLsizei)(cmd->clip_rect.w * width / UWP_UI_WIDTH), (GLsizei)(cmd->clip_rect.h * height / UWP_UI_HEIGHT));
        glDrawElements(GL_TRIANGLES, cmd->elem_count, GL_UNSIGNED_SHORT, (void *)offset);
        offset += cmd->elem_count * sizeof(nk_draw_index);
    }
    glDisable(GL_SCISSOR_TEST);
    nk_clear(&ctx);
}

void uwpLoadingProgress(const char* title, const char* stage, uint64_t current, uint64_t total) {
    if (!initialized) return;
    if (title) SDL_strlcpy(loadingTitle, title, sizeof(loadingTitle));
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    if (!window) return;
    static Uint32 lastFrame;
    static char lastStage[128];
    Uint32 now = SDL_GetTicks();
    if (!strcmp(lastStage, stage) && current != total && now - lastFrame < 33) return;
    snprintf(lastStage, sizeof(lastStage), "%s", stage);
    lastFrame = now;
    SDL_PumpEvents();

    // Loading also runs during game_change, while the renderer owns the GL state.
    GLint oldProgram, oldVao, oldVbo, texture, sampler, activeTexture, drawFbo, readFbo;
    GLint viewport[4], scissor[4], srcRgb, dstRgb, srcAlpha, dstAlpha, equationRgb, equationAlpha;
    GLfloat clearColor[4];
    GLboolean colorMask[4], enabled[5];
    const GLenum caps[] = {GL_BLEND, GL_DEPTH_TEST, GL_CULL_FACE, GL_SCISSOR_TEST, GL_STENCIL_TEST};
    int capCount = sizeof(caps) / sizeof(caps[0]);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldVbo);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &equationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equationAlpha);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    for (int i = 0; i < capCount; i++) enabled[i] = glIsEnabled(caps[i]);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (int i = 1; i < capCount; i++) glDisable(caps[i]);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    int width, height;
    SDL_GL_GetDrawableSize(window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(28 / 255.0f, 28 / 255.0f, 28 / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    uwpUiBegin();
    uwpUiLoading(loadingTitle, stage, current, total);
    uwpUiRender(width, height);
    SDL_GL_SwapWindow(window);

    glUseProgram(glIsProgram(oldProgram) ? oldProgram : 0);
    glBindVertexArray(oldVao);
    glBindBuffer(GL_ARRAY_BUFFER, oldVbo);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindSampler(0, sampler);
    glActiveTexture(activeTexture);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
    glBlendEquationSeparate(equationRgb, equationAlpha);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    for (int i = 0; i < capCount; i++) {
        if (enabled[i]) glEnable(caps[i]);
        else glDisable(caps[i]);
    }
}

void uwpDataProgress(const char* chunk, int index, int total, struct DataWin* dataWin, void* userData) {
    (void)userData;
    char stage[64];
    snprintf(stage, sizeof(stage), "Loading chunk %.4s...", chunk);
    uwpLoadingProgress(dataWin->gen8.displayName, stage, index, total);
}
