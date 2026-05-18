# Makefile build
# meant to be extremely portable to weird unix-like systems

CC := cc

CFLAGS := -O2 -DNDEBUG
LIBS := -lbz2

OS := $(shell uname -s)

DEFINES := -DENABLE_VM_GML_PROFILER \
		   -DENABLE_VM_OPCODE_PROFILER \
		   -DENABLE_VM_STUB_LOGS \
		   -DENABLE_VM_TRACING
INCLUDES := -I. -Isrc -Ivendor/stb/ds -Isrc/image -Ivendor/stb/image -Ivendor/stb/vorbis -Ivendor/md5 -Ivendor/base64

HEADERS := $(wildcard src/*.h) $(shell find vendor -name '*.h')
SRCS := $(wildcard src/*.c) $(wildcard src/image/*.c) vendor/md5/md5.c vendor/base64/base64.c

PLATFORM := glfw
AUDIO_BACKEND := miniaudio

ifdef BUTTERSCOTCH_COMMIT_DATE
DEFINES += -DBUTTERSCOTCH_COMMIT_DATE=\"$(BUTTERSCOTCH_COMMIT_DATE)\"
else
DEFINES += -DBUTTERSCOTCH_COMMIT_DATE=\"unknown\"
endif
ifdef BUTTERSCOTCH_COMMIT_HASH
DEFINES += -DBUTTERSCOTCH_COMMIT_HASH=\"$(BUTTERSCOTCH_COMMIT_HASH)\"
else
DEFINES += -DBUTTERSCOTCH_COMMIT_HASH=\"unknown\"
endif

ifndef DISABLE_BC14
DEFINES += -DENABLE_BC14
endif

ifndef DISABLE_BC16
DEFINES += -DENABLE_BC16
endif

ifndef DISABLE_BC17
DEFINES += -DENABLE_BC17
endif

# GNU make doesn't have a way to do OR in conditionals, stupid language for clowns
ifndef DISABLE_LEGACY_GL
ENABLE_GL := 1
endif
ifndef DISABLE_MODERN_GL
ifneq ($(PLATFORM),sdl)
ENABLE_GL := 1
endif
endif

ifdef ENABLE_GL
INCLUDES += -Isrc/gl_common -Isrc/gl -Ivendor/glad/include
SRCS += $(wildcard src/gl_common/*.c) vendor/glad/src/glad.c
HEADERS += $(wildcard src/gl_common/*.h)
endif

ifndef DISABLE_LEGACY_GL
ifndef ENABLE_GLES
DEFINES += -DENABLE_LEGACY_GL
SRCS += $(wildcard src/gl_legacy/*.c)
INCLUDES += -Isrc/gl_legacy
HEADERS += $(wildcard src/gl_legacy/*.h) $(wildcard src/gl/*.h)
endif
endif

ifndef DISABLE_MODERN_GL
ifneq ($(PLATFORM),sdl)
DEFINES += -DENABLE_MODERN_GL
SRCS += $(wildcard src/gl/*.c)
HEADERS += $(wildcard src/gl/*.h)
endif
endif

ifdef DISABLE_BC14
ifdef DISABLE_BC16
ifdef DISABLE_BC17
$(error must enable at least 1 bytecode version)
endif
endif
endif

ifdef DISABLE_LEGACY_GL
ifeq ($(PLATFORM),sdl)
$(error must enable at least 1 renderer)
else
ifdef DISABLE_MODERN_GL
$(error must enable at least 1 renderer)
endif
endif
endif

ifdef ENABLE_GLES
DEFINES += -DENABLE_GLES
endif

ifeq ($(AUDIO_BACKEND),miniaudio)
INCLUDES += -Isrc/audio/miniaudio -Ivendor/miniaudio
DEFINES += -DUSE_MINIAUDIO
SRCS += $(wildcard src/audio/miniaudio/*.c)
HEADERS += $(wildcard src/audio/miniaudio/*.h)
ifneq ($(OS),Windows)
LIBS += -pthread
endif
endif
ifeq ($(AUDIO_BACKEND),openal)
INCLUDES += -Isrc/audio/openal
DEFINES += -DUSE_OPENAL
SRCS += $(wildcard src/audio/openal/*.c)
HEADERS += $(wildcard src/audio/openal/*.h)
ifeq ($(OS),Darwin)
LIBS += -framework OpenAL
else
LIBS += -lopenal
endif
endif

ifeq ($(PLATFORM),glfw)
SRCS += $(wildcard src/glfw/*.c)
HEADERS += $(wildcard src/glfw/*.h)
DEFINES += -DUSE_GLFW
ifdef USE_GLFW2
ifdef ENABLE_GLES
$(error can't enable both GLES and GLFW2 at the same time!)
endif
DEFINES += -DUSE_GLFW2
SRCS := $(filter-out src/glfw/glfw_gamepad.c,$(SRCS))
ifndef GLFW_LIBS
GLFW_LIBS := $(shell pkg-config --libs libglfw)
endif
else
ifndef GLFW_LIBS
GLFW_LIBS := $(shell pkg-config --libs glfw3)
endif
endif
LIBS += $(GLFW_LIBS)
else
ifeq ($(PLATFORM),sdl)
SRCS += $(wildcard src/sdl/*.c)
HEADERS += $(wildcard src/sdl/*.h)
DEFINES += -DUSE_SDL
ifndef SDL_LIBS
SDL_LIBS := $(shell pkg-config --libs sdl)
endif
LIBS += $(SDL_LIBS)
else
$(error invalid platform)
endif
endif

ifeq ($(OS),Windows)
LIBS += -static
else
ifeq ($(OS),Darwin)
LIBS += -lobjc
else
ifneq ($(filter Linux Haiku %BSD Unix,$(OS)),) # OS is 'Linux', 'Haiku', '*BSD', or 'Unix'
ifneq ($(OS),Haiku)
INCLUDES += -I/usr/X11R6/include
LIBS += -L/usr/X11R6/lib -ldl -lrt
endif
LIBS += -lm
else
$(error unknown OS '$(OS)', please manually set the OS variable)
endif
endif
endif

OBJS := $(addprefix build/,$(SRCS:.c=.c.o))

ifndef DISABLE_MMD
DEPFLAGS = -MMD -MP
endif

all: build/butterscotch

-include $(OBJS:.o=.d)

build/butterscotch: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) $(EXTRALIBS) -o $@

build/%.c.o: %.c $(if $(DISABLE_MMD),$(HEADERS))
	@mkdir -p $(dir $@)
	$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

clean:
	rm -rf build
