#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "libretro.h"
#include <glsm/glsmsym.h>

#include "data_win.h"
#include "vm.h"
#include "runner.h"
#include "runner_keyboard.h"
#include "runner_mouse.h"
#include "runner_gamepad.h"
#ifdef ENABLE_GLES
#include "gl/gl_renderer.h"
#else
#include "sw_renderer.h"
#endif
#include "ma_audio_system.h"
#include "overlay_file_system.h"
#include "desktop/platformdefs.h"
#include "utils.h"

static retro_log_printf_t          log_cb;
static retro_video_refresh_t       video_cb;
static retro_input_poll_t          input_poll_cb;
static retro_input_state_t         input_state_cb;
static retro_audio_sample_batch_t  audio_batch_cb;
static retro_environment_t         environ_cb;

static Runner*            g_runner   = nullptr;
static Renderer*          g_renderer = nullptr;
static OverlayFileSystem* g_overlayFs = nullptr;
static VMContext*         g_vm       = nullptr;
static DataWin*           g_dataWin  = nullptr;
static Gen8*              g_gen8     = nullptr;

static int32_t  fbWidth  = 640;
static int32_t  fbHeight = 480;
static double   lastFrameStartTime = 0.0;

enum GraphicsAPI gfx = SOFTWARE;
InputRecording*  globalInputRecording = nullptr;

static bool use_hw_renderer  = false;
static bool hw_context_valid = false;

static uint32_t* nextFb  = nullptr;
static int       nextFbW = 0;
static int       nextFbH = 0;

void Runner_setNextFrame(uint32_t* framebuffer, int width, int height)
{
  nextFb  = framebuffer;
  nextFbW = width;
  nextFbH = height;
}

bool platformInit(int32_t reqW, int32_t reqH, const char* title, bool headless)
{
  (void)title; (void)headless;
  fbWidth  = reqW;
  fbHeight = reqH;
  return true;
}

static bool always_true(void) { return true; }

void platformInitFunctions(Runner* runner)
{
  g_runner = runner;
  runner->windowHasFocus = always_true;
}

void platformExit(void) {}

void platformSwapBuffers(void) {}

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
void *platformGetProcAddress(const char *name)
{
  glsm_ctx_proc_address_t proc;
  proc.addr = NULL;
  if (!glsm_ctl(GLSM_CTL_PROC_ADDRESS_GET, &proc))
    return NULL;
  return (void*)proc.addr(name);
}
#endif

double platformGetTime(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

bool platformHandleEvents(void) { return false; }

void platformGetMousePos(double* xPos, double* yPos)
{
  if (xPos) *xPos = 0;
  if (yPos) *yPos = 0;
}

bool platformGetWindowSize(int32_t* outW, int32_t* outH)
{
  if (outW) *outW = fbWidth;
  if (outH) *outH = fbHeight;
  return true;
}

bool platformGetScaledWindowSize(int32_t* outW, int32_t* outH)
{
  return platformGetWindowSize(outW, outH);
}

void platformSetWindowSize(int32_t width, int32_t height)
{
  if (width <= 0 || height <= 0) return;
  fbWidth  = width;
  fbHeight = height;
}

void platformSetWindowTitle(const char* title) { (void)title; }

void platformSleepUntil(uint64_t time) { (void)time; }

static void context_reset(void)
{
  log_cb(RETRO_LOG_INFO, "butterscotch: GL context reset\n");


#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
  static bool first_init = true;
  printf("context_reset.\n");
  glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL);

  if (first_init)
  {
    glsm_ctl(GLSM_CTL_STATE_SETUP, NULL);
    first_init = false;
  }
#endif

#ifdef ENABLE_GLES
  g_renderer = GLRenderer_create();
  GLRenderer* gl = (GLRenderer*)g_renderer;
  gl->hostFramebuffer = glsm_get_current_framebuffer();
#endif

  MaAudioSystem* maAudio = MaAudioSystem_create(g_dataWin);
  AudioSystem* audio = (AudioSystem*)maAudio;

  g_runner = Runner_create(g_dataWin, g_vm,
                            g_renderer,
                            (FileSystem *)g_overlayFs, audio);
  g_runner->osType          = OS_WINDOWS;
  g_runner->setWindowSize   = platformSetWindowSize;
  g_runner->getWindowSize   = platformGetWindowSize;
  g_runner->setWindowTitle  = platformSetWindowTitle;
  platformInitFunctions(g_runner);

  Runner_initFirstRoom(g_runner);
  lastFrameStartTime = platformGetTime();

  if (!g_renderer)
  {
    use_hw_renderer = false;
  }
  else
  {
    hw_context_valid = true;
    log_cb(RETRO_LOG_INFO, "butterscotch: GLES3 renderer ready\n");
  }

  if (g_runner)
    g_runner->renderer = g_renderer;

}

static void context_destroy(void)
{
  log_cb(RETRO_LOG_INFO, "butterscotch: GL context destroyed\n");
  hw_context_valid = false;

  if (g_renderer)
  {
    g_renderer->vtable->destroy(g_renderer);
    g_renderer = NULL;
    if (g_runner)
      g_runner->renderer = NULL;
  }
}

static int retroKeyToGml(unsigned key)
{
  if (key >= RETROK_a && key <= RETROK_z) return key - 32;
  if (key >= RETROK_0 && key <= RETROK_9) return key;
  switch (key)
  {
    case RETROK_ESCAPE:    return VK_ESCAPE;
    case RETROK_RETURN:    return VK_ENTER;
    case RETROK_TAB:       return VK_TAB;
    case RETROK_BACKSPACE: return VK_BACKSPACE;
    case RETROK_SPACE:     return VK_SPACE;
    case RETROK_LSHIFT:
    case RETROK_RSHIFT:    return VK_SHIFT;
    case RETROK_LCTRL:
    case RETROK_RCTRL:     return VK_CONTROL;
    case RETROK_LALT:
    case RETROK_RALT:      return VK_ALT;
    case RETROK_UP:        return VK_UP;
    case RETROK_DOWN:      return VK_DOWN;
    case RETROK_LEFT:      return VK_LEFT;
    case RETROK_RIGHT:     return VK_RIGHT;
    case RETROK_F1:        return VK_F1;
    case RETROK_F2:        return VK_F2;
    case RETROK_F3:        return VK_F3;
    case RETROK_F4:        return VK_F4;
    case RETROK_F5:        return VK_F5;
    case RETROK_F6:        return VK_F6;
    case RETROK_F7:        return VK_F7;
    case RETROK_F8:        return VK_F8;
    case RETROK_F9:        return VK_F9;
    case RETROK_F10:       return VK_F10;
    case RETROK_F11:       return VK_F11;
    case RETROK_F12:       return VK_F12;
    case RETROK_INSERT:    return VK_INSERT;
    case RETROK_DELETE:    return VK_DELETE;
    case RETROK_HOME:      return VK_HOME;
    case RETROK_END:       return VK_END;
    case RETROK_PAGEUP:    return VK_PAGEUP;
    case RETROK_PAGEDOWN:  return VK_PAGEDOWN;
    default:               return -1;
  }
}

static void keyboard_cb(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers)
{
  (void)key_modifiers;
  if (!g_runner) return;
  int gml = retroKeyToGml(keycode);
  if (gml < 0) return;
  if (down)
    RunnerKeyboard_onKeyDown(g_runner->keyboard, gml);
  else
    RunnerKeyboard_onKeyUp(g_runner->keyboard, gml);
  if (down && character != 0)
    RunnerKeyboard_onCharacter(g_runner->keyboard, character);
}

static int     prev_mouse_buttons[3] = {0};
static int16_t prev_mouse_x = 0;
static int16_t prev_mouse_y = 0;

// RetroArch button ID → GamepadSlot::buttonDown index
static const int retro_id_to_gp_idx[] = {
  [RETRO_DEVICE_ID_JOYPAD_B]      = 0,  // GP_FACE1
  [RETRO_DEVICE_ID_JOYPAD_A]      = 1,  // GP_FACE2
  [RETRO_DEVICE_ID_JOYPAD_X]      = 2,  // GP_FACE3
  [RETRO_DEVICE_ID_JOYPAD_Y]      = 3,  // GP_FACE4
  [RETRO_DEVICE_ID_JOYPAD_L]      = 4,  // GP_SHOULDERL
  [RETRO_DEVICE_ID_JOYPAD_R]      = 5,  // GP_SHOULDERR
  [RETRO_DEVICE_ID_JOYPAD_L2]     = 6,  // GP_SHOULDERLB
  [RETRO_DEVICE_ID_JOYPAD_R2]     = 7,  // GP_SHOULDERRB
  [RETRO_DEVICE_ID_JOYPAD_SELECT] = 8,  // GP_SELECT
  [RETRO_DEVICE_ID_JOYPAD_START]  = 9,  // GP_START
  [RETRO_DEVICE_ID_JOYPAD_L3]     = 10, // GP_STICKL
  [RETRO_DEVICE_ID_JOYPAD_R3]     = 11, // GP_STICKR
  [RETRO_DEVICE_ID_JOYPAD_UP]     = 12, // GP_PADU
  [RETRO_DEVICE_ID_JOYPAD_DOWN]   = 13, // GP_PADD
  [RETRO_DEVICE_ID_JOYPAD_LEFT]   = 14, // GP_PADL
  [RETRO_DEVICE_ID_JOYPAD_RIGHT]  = 15, // GP_PADR
};
static bool prev_gp_down[GP_BUTTON_COUNT] = {0};

static void pump_mouse(void)
{
  // Absolute pointer (touch / mouse emulation)
  int16_t px = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
  int16_t py = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
  if (px != prev_mouse_x || py != prev_mouse_y)
  {
    prev_mouse_x = px;
    prev_mouse_y = py;
  }

  // Mouse buttons via pointer pressed
  int ptr_btn = 0;
  while (input_state_cb(0, RETRO_DEVICE_POINTER, ptr_btn, RETRO_DEVICE_ID_POINTER_PRESSED))
    ptr_btn++;

  // Mouse buttons via RETRO_DEVICE_MOUSE
  int16_t mb_left   = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
  int16_t mb_right  = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT);
  int16_t mb_middle = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE);
  int pressed = mb_left ? 1 : (mb_right ? 2 : (mb_middle ? 3 : ptr_btn));

  if (pressed != prev_mouse_buttons[0])
  {
    prev_mouse_buttons[0] = pressed;
    if (pressed)
      RunnerMouse_onButtonDown(g_runner->mouse, pressed);
    else
      RunnerMouse_onButtonUp(g_runner->mouse, prev_mouse_buttons[0] ? prev_mouse_buttons[0] : 1);
  }
}

static void pump_gamepad(void)
{
  GamepadSlot* slot = &g_runner->gamepads->slots[0];
  GamepadSlot prev;
  memcpy(&prev, slot, sizeof(prev));

  slot->connected = true;
  g_runner->gamepads->connectedCount++;
  slot->deadzone = 0.15f;
  slot->triggerThreshold = 0.5f;
  strcpy(slot->description, "RetroPad");
  strcpy(slot->guid, "retroarch");

  // Buttons
  for (int i = 0; i < 16; i++)
  {
    int idx = retro_id_to_gp_idx[i];
    bool down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, i);
    slot->buttonDown[idx] = down;
    slot->buttonValue[idx] = down ? 1.0f : 0.0f;
  }

  // Analog sticks
  slot->axisValue[0] = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X) / 32767.0f;
  slot->axisValue[1] = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y) / 32767.0f;
  slot->axisValue[2] = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X) / 32767.0f;
  slot->axisValue[3] = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y) / 32767.0f;

  // Pressed / Released from diff with previous
  for (int i = 0; i < GP_BUTTON_COUNT; i++)
  {
    slot->buttonPressed[i]  = slot->buttonDown[i] && !prev_gp_down[i];
    slot->buttonReleased[i] = !slot->buttonDown[i] && prev_gp_down[i];
    prev_gp_down[i] = slot->buttonDown[i];
  }
}

// === libretro entry points ===

unsigned retro_api_version(void)
{
  return RETRO_API_VERSION;
}

void retro_set_environment(retro_environment_t cb)
{
  environ_cb = cb;
  struct retro_log_callback log;
  if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
    log_cb = log.log;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
  video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
  audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
  input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
  input_state_cb = cb;
}

void retro_get_system_info(struct retro_system_info *info)
{
  info->need_fullpath    = true;
  info->valid_extensions = "win|exe|unx|game";
  info->library_version  = "0.1";
  info->library_name     = "butterscotch";
  info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
  info->geometry.base_width   = 640;
  info->geometry.base_height  = 480;
  info->geometry.max_width    = 1920;
  info->geometry.max_height   = 1080;
  info->geometry.aspect_ratio = 0.0f;
  info->timing.fps            = 60.0;
  info->timing.sample_rate    = 44100.0;
}

void retro_init(void)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
  glsm_ctx_params_t params = {0};
  params.context_reset   = context_reset;
  params.context_destroy = context_destroy;
  params.environ_cb      = environ_cb;
  params.stencil         = false;
  params.major           = 3;
  params.minor           = 0;
  params.context_type    = RETRO_HW_CONTEXT_OPENGLES3;

  use_hw_renderer = glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params);
  if (!use_hw_renderer)
      log_cb(RETRO_LOG_WARN,
              "butterscotch: missing opengles 3 support \n");
#endif
  enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_XRGB8888;
  environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixfmt);
}

void retro_deinit(void)
{
}

bool retro_load_game(const struct retro_game_info *game)
{
  if (!game) return false;

  // --- Parse data.win ---
  DataWinParserOptions opts = {0};
  opts.parseGen8  = true;
  opts.parseOptn  = true;
  opts.parseLang  = true;
  opts.parseExtn  = true;
  opts.parseSond  = true;
  opts.parseAgrp  = true;
  opts.parseSprt  = true;
  opts.parseBgnd  = true;
  opts.parsePath  = true;
  opts.parseScpt  = true;
  opts.parseGlob  = true;
  opts.parseShdr  = true;
  opts.parseFont  = true;
  opts.parseTmln  = true;
  opts.parseObjt  = true;
  opts.parseRoom  = true;
  opts.parseTpag  = true;
  opts.parseCode  = true;
  opts.parseVari  = true;
  opts.parseFunc  = true;
  opts.parseStrg  = true;
  opts.parseTxtr  = true;
  opts.parseAudo  = true;
  opts.skipLoadingPreciseMasksForNonPreciseSprites = true;
  opts.loadType   = DATAWINLOADTYPE_LOAD_IN_MEMORY_AHEAD_OF_TIME;

  g_dataWin = DataWin_parse(game->path, opts);
  if (!g_dataWin)
  {
    log_cb(RETRO_LOG_ERROR, "Failed to parse data.win from %s\n", game->path);
    return false;
  }

  g_gen8 = &g_dataWin->gen8;
  log_cb(RETRO_LOG_INFO, "Loaded \"%s\" (%d) [WAD %u / GM %u.%u.%u.%u]\n",
         g_gen8->name, g_gen8->gameID, g_gen8->wadVersion,
         g_dataWin->detectedFormat.major, g_dataWin->detectedFormat.minor,
         g_dataWin->detectedFormat.release, g_dataWin->detectedFormat.build);

  g_vm = VM_create(g_dataWin);

  const char* lastSlash = strrchr(game->path, '/');
  const char* lastBackslash = strrchr(game->path, '\\');
  if (lastBackslash && (!lastSlash || lastBackslash > lastSlash))
    lastSlash = lastBackslash;

  char* dataWinDir;
  if (lastSlash)
  {
    size_t len = (size_t)(lastSlash - game->path + 1);
    dataWinDir = (char*)safeMalloc(len + 1);
    memcpy(dataWinDir, game->path, len);
    dataWinDir[len] = '\0';
  }
  else
  {
    dataWinDir = safeStrdup("./");
  }

  g_overlayFs = OverlayFileSystem_create(dataWinDir, dataWinDir);
  free(dataWinDir);

  fbWidth  = g_gen8->defaultWindowWidth  > 0 ? (int32_t)g_gen8->defaultWindowWidth  : 640;
  fbHeight = g_gen8->defaultWindowHeight > 0 ? (int32_t)g_gen8->defaultWindowHeight : 480;
#if !defined(ENABLE_GLES)
  g_renderer = SWRenderer_create();
  if (!g_renderer)
  {
    log_cb(RETRO_LOG_ERROR, "Failed to create software renderer\n");
    return false;
  }

  MaAudioSystem* maAudio = MaAudioSystem_create(g_dataWin);
  AudioSystem* audio = (AudioSystem*)maAudio;

  g_runner = Runner_create(g_dataWin, g_vm, g_renderer, (FileSystem*)g_overlayFs, audio);
  g_runner->osType = OS_WINDOWS;
  g_runner->setWindowSize   = platformSetWindowSize;
  g_runner->getWindowSize   = platformGetWindowSize;
  g_runner->setWindowTitle  = platformSetWindowTitle;
  platformInitFunctions(g_runner);

  Runner_initFirstRoom(g_runner);

  lastFrameStartTime = platformGetTime();

  }
#endif

  struct retro_keyboard_callback kbcb = { .callback = keyboard_cb };
  environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kbcb);

  return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
  (void)game_type; (void)info; (void)num_info;
  return false;
}

void retro_unload_game(void)
{
  if (g_runner)
  {
    g_runner->audioSystem->vtable->destroy(g_runner->audioSystem);
    if (g_renderer) g_renderer->vtable->destroy(g_renderer);
    Runner_free(g_runner);
    g_runner = nullptr;
  }
  if (g_overlayFs)
  {
    OverlayFileSystem_destroy(g_overlayFs);
    g_overlayFs = nullptr;
  }
  if (g_vm)
  {
    VM_free(g_vm);
    g_vm = nullptr;
  }
  if (g_dataWin)
  {
    DataWin_free(g_dataWin);
    g_dataWin = nullptr;
  }
  g_renderer = nullptr;
  g_gen8     = nullptr;
  nextFb     = nullptr;
}

void retro_reset(void)
{
  Runner_reset(g_runner);
}

void retro_run(void)
{
  if (!g_runner) return;

  double now = platformGetTime();
  g_runner->deltaTime = (now - lastFrameStartTime) * 1000000.0;
  lastFrameStartTime = now;

  RunnerKeyboard_beginFrame(g_runner->keyboard);
  RunnerGamepad_beginFrame(g_runner->gamepads);
  RunnerMouse_beginFrame(g_runner->mouse);

  input_poll_cb();
  pump_gamepad();
  pump_mouse();

  Runner_step(g_runner);

  float dt = (float)(g_runner->deltaTime / 1000000.0);
  if (dt < 0.0f) dt = 0.0f;
  if (dt > 0.1f) dt = 0.1f;
  g_runner->audioSystem->vtable->update(g_runner->audioSystem, dt);

  // Read mixed PCM frames from miniaudio engine and push to libretro
  static float  mixFloat[8192 * 2];
  static int16_t mixInt16[8192 * 2];
  ma_engine* engine = &((MaAudioSystem*)g_runner->audioSystem)->engine;
  if (engine)
  {
    ma_uint64 frames = (ma_uint64)(44100.0f * dt);
    if (frames == 0) frames = 1;
    if (frames > 8192) frames = 8192;
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(engine, mixFloat, frames, &read);
    if (read > 0)
    {
      ma_uint64 total = read * 2;
      for (ma_uint64 s = 0; s < total; s++)
      {
        float f = mixFloat[s];
        if (f < -1.0f) f = -1.0f;
        if (f >  1.0f) f =  1.0f;
        mixInt16[s] = (int16_t)(f * 32767.0f);
      }
      audio_batch_cb(mixInt16, (size_t)read);
    }
  }

  int32_t gameW, gameH;
  if (g_runner->appSurfaceEnabled)
  {
    if (g_runner->applicationWidth  <= 0 || g_runner->applicationHeight <= 0)
    {
      g_runner->applicationWidth  = (int32_t)g_gen8->defaultWindowWidth;
      g_runner->applicationHeight = (int32_t)g_gen8->defaultWindowHeight;
    }
    gameW = g_runner->applicationWidth;
    gameH = g_runner->applicationHeight;
  }
  else
  {
    g_runner->applicationWidth  = fbWidth;
    g_runner->applicationHeight = fbHeight;
    gameW = fbWidth;
    gameH = fbHeight;
  }

  g_runner->renderGameW = gameW;
  g_runner->renderGameH = gameH;

  int32_t winW, winH;
  platformGetScaledWindowSize(&winW, &winH);
  int32_t scaledW, scaledH;
  if ((gameW * winH) / gameH < winW)
  {
    scaledW = (gameW * winH) / gameH;
    scaledH = winH;
  }
  else
  {
    scaledW = winW;
    scaledH = (gameH * winW) / gameW;
  }
  g_runner->viewportX = (winW - scaledW) / 2;
  g_runner->viewportY = (winH - scaledH) / 2;
  g_runner->viewportW = scaledW;
  g_runner->viewportH = scaledH;

  int mx = 0, my = 0;
  int16_t rx = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
  int16_t ry = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
  mx = (int)((int64_t)(rx + 0x7fff) * fbWidth / 0xFFFF);
  my = (int)((int64_t)(ry + 0x7fff) * fbHeight / 0xFFFF);
  Runner_updateMousePosition(g_runner, winW, winH, (double)mx, (double)my);


  float displayScaleX, displayScaleY;
  Runner_computeViewDisplayScale(g_runner, gameW, gameH, &displayScaleX, &displayScaleY);

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
  glsm_ctl(GLSM_CTL_STATE_BIND, NULL);

  Runner_drawPre(g_runner, fbWidth, fbHeight);
  Runner_beginFrame(g_runner, gameW, gameH, fbWidth, fbHeight, fbWidth, fbHeight);

  if (g_runner->drawBackgroundColor)
  {
    uint32_t c = g_runner->backgroundColor;
    glClearColor(((c >> 16) & 0xFF) / 255.0f,
                    ((c >>  8) & 0xFF) / 255.0f,
                    ( c        & 0xFF) / 255.0f,
                    1.0f);
  }
  else
  {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  }
  glClear(GL_COLOR_BUFFER_BIT);

  Runner_drawViews(g_runner, gameW, gameH, false);
  g_renderer->vtable->endFrameInit(g_renderer);
  Runner_drawPost(g_runner, fbWidth, fbHeight);
  g_renderer->vtable->endFrameEnd(g_renderer);
  Runner_drawGUI(g_runner, fbWidth, fbHeight, gameW, gameH);

  glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL);

  video_cb(RETRO_HW_FRAME_BUFFER_VALID,
              (unsigned)fbWidth, (unsigned)fbHeight, 0);
#else

  Runner_drawPre(g_runner, fbWidth, fbHeight);
  Runner_beginFrame(g_runner, gameW, gameH, fbWidth, fbHeight, fbWidth, fbHeight);
  if (g_runner->drawBackgroundColor)
    SWRenderer_clearFrameBuffer(g_renderer, g_runner->backgroundColor);
  else
    SWRenderer_clearFrameBuffer(g_renderer, 0);

  Runner_drawViews(g_runner, gameW, gameH, false);
  g_renderer->vtable->endFrameInit(g_renderer);
  Runner_drawPost(g_runner, fbWidth, fbHeight);
  g_renderer->vtable->endFrameEnd(g_renderer);
  Runner_drawGUI(g_runner, fbWidth, fbHeight, gameW, gameH);

  if (nextFb)
    video_cb(nextFb, nextFbW, nextFbH, nextFbW * (int)sizeof(uint32_t));
#endif

  Runner_handlePendingRoomChange(g_runner);
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
  (void)port; (void)device;
}

size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
void *retro_get_memory_data(unsigned id) { (void)id; return nullptr; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
