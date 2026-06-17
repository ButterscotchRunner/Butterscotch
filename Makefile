# Makefile build
# meant to be extremely portable to weird unix-like systems

CC := cc

CFLAGS := -O2 -DNDEBUG

OS := $(shell uname -s)
ifneq ($(filter Windows_NT MINGW% MSYS% CYGWIN%,$(OS)),)
OS := Windows
endif

DEFINES := -DENABLE_VM_GML_PROFILER \
		   -DENABLE_VM_OPCODE_PROFILER \
		   -DENABLE_VM_STUB_LOGS \
		   -DENABLE_VM_TRACING
INCLUDES := -I. -Isrc -Ivendor/stb/ds -Isrc/image -Ivendor/stb/image -Ivendor/stb/vorbis -Ivendor/md5 -Ivendor/sha1 -Ivendor/base64 -Ivendor/bzip2

HEADERS := $(wildcard src/*.h) $(shell find vendor -name '*.h')
SRCS := $(wildcard src/*.c) $(wildcard src/image/*.c) $(wildcard vendor/bzip2/*.c) vendor/md5/md5.c vendor/sha1/sha1.c vendor/base64/base64.c

DESKTOP_BACKEND := glfw3
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

ifndef DISABLE_WAD14
DEFINES += -DENABLE_WAD14
endif

ifndef DISABLE_WAD16
DEFINES += -DENABLE_WAD16
endif

ifndef DISABLE_WAD17
DEFINES += -DENABLE_WAD17
endif

# TODO: add support for non-desktop backends
SRCS += $(wildcard src/desktop/*.c) $(wildcard src/desktop/backends/$(DESKTOP_BACKEND).c)
ifeq ($(OS),Windows)
PKG_CONFIG_FLAGS := --static
endif
INCLUDES += -Isrc/desktop
ifeq ($(DESKTOP_BACKEND),glfw3)
GLFW3_LIBS += $(shell pkg-config $(PKG_CONFIG_FLAGS) --libs glfw3)
LIBS += $(GLFW3_LIBS)
DEFINES += -DUSE_GLFW3
ENABLE_GLAD := 1
endif
ifeq ($(DESKTOP_BACKEND),glfw2)
GLFW2_LIBS += $(shell pkg-config $(PKG_CONFIG_FLAGS) --libs libglfw)
LIBS += $(GLFW2_LIBS)
DEFINES += -DUSE_GLFW2
ENABLE_GLAD := 1
endif
ifeq ($(DESKTOP_BACKEND),sdl1)
SDL1_LIBS += $(shell pkg-config $(PKG_CONFIG_FLAGS) --libs sdl)
LIBS += $(SDL1_LIBS)
DEFINES += -DUSE_SDL1
endif
ifeq ($(DESKTOP_BACKEND),sdl2)
SDL2_LIBS += $(shell pkg-config $(PKG_CONFIG_FLAGS) --libs sdl2)
LIBS += $(SDL2_LIBS)
DEFINES += -DUSE_SDL2
endif
ifeq ($(DESKTOP_BACKEND),sdl3)
SDL3_LIBS += $(shell pkg-config $(PKG_CONFIG_FLAGS) --libs sdl3)
LIBS += $(SDL3_LIBS)
DEFINES += -DUSE_SDL3
endif


# GNU make doesn't have a way to do OR in conditionals, stupid language for clowns
ifndef DISABLE_LEGACY_GL
ENABLE_GL := 1
endif
ifndef DISABLE_MODERN_GL
ENABLE_GL := 1
endif

ifdef ENABLE_GL
SRCS += $(wildcard src/gl_common/*.c)
INCLUDES += -Isrc/gl_common -Isrc/gl
HEADERS += $(wildcard src/gl_common/*.h)
ENABLE_GLAD := 1
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

ifdef DISABLE_WAD14
ifdef DISABLE_WAD16
ifdef DISABLE_WAD17
$(error must enable at least 1 bytecode version)
endif
endif
endif

ifdef DISABLE_LEGACY_GL
ifdef DISABLE_MODERN_GL
$(error must enable at least 1 renderer)
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

ifdef ENABLE_GLAD
ifdef ENABLE_GLES
SRCS += vendor/glad-gles/src/glad.c
INCLUDES += -Ivendor/glad-gles/include
else
SRCS += vendor/glad/src/glad.c
INCLUDES += -Ivendor/glad/include
endif
endif

ifeq ($(OS),Windows)
LIBS += -static -lwinmm
else
ifeq ($(OS),Darwin)
LIBS += -lobjc
else
ifneq ($(filter Linux Haiku %BSD Unix,$(OS)),) # OS is 'Linux', 'Haiku', '*BSD', or 'Unix'
LIBS += -lm
else
$(error unknown OS '$(OS)', please manually set the OS variable)
endif
endif
endif

ifndef VERBOSE
V := @
endif

OBJS := $(addprefix build/,$(SRCS:.c=.c.o))

all: build/butterscotch

-include $(OBJS:.o=.d)

ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)

-include compat/config.mk

ifndef DISABLE_MMD
DEPFLAGS = -MMD -MP -MF $(@:.o=.d)
endif

# trigger configure re-run if $(CC) changes
_dummy := $(shell \
	printf '$(CC)' > compat/tmp/cc-new; \
	cmp -s compat/tmp/cc-new compat/tmp/cc || \
	mv compat/tmp/cc-new compat/tmp/cc; \
	rm -f compat/tmp/cc-new \
)

compat/config.mk: compat/configure.sh compat/tmp/cc
	@CC="$(CC)" $(SHELL) compat/configure.sh

endif

build/butterscotch: $(OBJS)
	@[ -z "$(NO_COLOR)" ] && printf " \033[1;34mLD\033[0m butterscotch\n" || printf " LD butterscotch\n"
	$(V)$(CC) $(LDFLAGS) $(OBJS) $(LIBS) $(EXTRALIBS) -o $@

build/%.c.o: %.c compat/config.mk $(if $(DISABLE_MMD),$(HEADERS))
	@mkdir -p $(dir $@)
	@{ [ -z "$(NO_COLOR)" ] && [ -t 1 ]; } && printf " \033[1;32mCC\033[0m $<\n" || printf " CC $<\n"
	$(V)$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

clean:
	rm -rf build

distclean: clean
	rm -f compat/config.mk compat/tmp/cc
