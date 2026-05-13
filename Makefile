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
INCLUDES := -I. -Isrc -Ivendor/stb/ds -Isrc/image -Ivendor/stb/image -Ivendor/stb/vorbis -Ivendor/md5 -Ivendor/glad/include

HEADERS := $(wildcard src/*.h) $(shell find vendor -name '*.h')
SRCS := $(wildcard src/*.c) $(wildcard src/image/*.c) vendor/md5/md5.c vendor/glad/src/glad.c

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

ifndef DISABLE_BC16
DEFINES += -DENABLE_BC16
endif

ifndef DISABLE_BC17
DEFINES += -DENABLE_BC17
endif

ifndef DISABLE_LEGACY_GL
ifndef DISABLE_MODERN_GL
INCLUDES += -Isrc/gl_common -Isrc/gl
SRCS += $(wildcard src/gl_common/*.c)
HEADERS += $(wildcard src/gl_common/*.h)
endif
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
DEFINES += -DENABLE_MODERN_GL
SRCS += $(wildcard src/gl/*.c)
HEADERS += $(wildcard src/gl/*.h)
endif

ifdef DISABLE_BC16
ifdef DISABLE_BC17
$(error must enable at least 1 bytecode version)
endif
endif

ifdef DISABLE_MODERN_GL
ifdef DISABLE_LEGACY_GL
$(error must enable at least 1 OpenGL renderer)
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

PLATFORM := glfw
ifeq ($(PLATFORM),glfw)
SRCS += $(wildcard src/glfw/*.c)
HEADERS += $(wildcard src/glfw/*.h)
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
$(error invalid platform)
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

all: build/butterscotch

build/butterscotch: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) $(EXTRALIBS) -o $@

build/%.c.o: %.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build
