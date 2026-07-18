# Makefile build
# meant to be extremely portable to weird unix-like systems

CC := cc
PKG_CONFIG := pkg-config

empty :=
space := $(empty) $(empty)

ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)

-include compat/config.mk

ifndef DISABLE_MMD
DEPFLAGS = -MMD -MP -MF $(@:.$(OBJ_EXT)=.d)
endif

# trigger configure re-run if $(CC) changes
_dummy := $(shell \
	printf '$(CC)' > compat/tmp/cc-new; \
	cmp -s compat/tmp/cc-new compat/tmp/cc || \
	mv compat/tmp/cc-new compat/tmp/cc; \
	rm -f compat/tmp/cc-new \
)

endif

DEFINES += $(DEFINE)ENABLE_VM_GML_PROFILER \
		   $(DEFINE)ENABLE_VM_OPCODE_PROFILER \
		   $(DEFINE)ENABLE_VM_STUB_LOGS \
		   $(DEFINE)ENABLE_VM_TRACING
INCLUDES += $(INCLUDE). \
		    $(INCLUDE)src \
		    $(INCLUDE)vendor/stb/ds \
		    $(INCLUDE)src/image \
		    $(INCLUDE)vendor/stb/image \
		    $(INCLUDE)vendor/stb/vorbis \
		    $(INCLUDE)vendor/md5 \
		    $(INCLUDE)vendor/sha1 \
		    $(INCLUDE)vendor/base64 \
		    $(INCLUDE)vendor/bzip2

HEADERS := $(wildcard src/*.h) $(shell find vendor -name '*.h')
SRCS := $(wildcard src/*.c) $(wildcard src/image/*.c) $(wildcard vendor/bzip2/*.c) vendor/md5/md5.c vendor/sha1/sha1.c vendor/base64/base64.c

DESKTOP_BACKEND := glfw3
AUDIO_BACKEND := miniaudio

ifdef BUTTERSCOTCH_COMMIT_DATE
DEFINES += $(DEFINE)BUTTERSCOTCH_COMMIT_DATE=\"$(BUTTERSCOTCH_COMMIT_DATE)\"
else
DEFINES += $(DEFINE)BUTTERSCOTCH_COMMIT_DATE=\"unknown\"
endif
ifdef BUTTERSCOTCH_COMMIT_HASH
DEFINES += $(DEFINE)BUTTERSCOTCH_COMMIT_HASH=\"$(BUTTERSCOTCH_COMMIT_HASH)\"
else
DEFINES += $(DEFINE)BUTTERSCOTCH_COMMIT_HASH=\"unknown\"
endif

ifndef DISABLE_WAD14
DEFINES += $(DEFINE)ENABLE_WAD14
endif

ifndef DISABLE_WAD16
DEFINES += $(DEFINE)ENABLE_WAD16
endif

ifndef DISABLE_WAD17
DEFINES += $(DEFINE)ENABLE_WAD17
endif

# TODO: add support for non-desktop backends
SRCS += $(wildcard src/desktop/*.c) src/desktop/backends/$(DESKTOP_BACKEND).c
ifeq ($(OS),Windows)
PKG_CONFIG_FLAGS := --static
endif
INCLUDES += $(INCLUDE)src/desktop
ifeq ($(DESKTOP_BACKEND),glfw3)
GLFW3_LIBS += $(shell $(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --libs glfw3)
LIBS += $(GLFW3_LIBS)
DEFINES += $(DEFINE)USE_GLFW3
ENABLE_GLAD := 1
endif
ifeq ($(DESKTOP_BACKEND),glfw2)
GLFW2_LIBS += $(shell $(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --libs libglfw)
LIBS += $(GLFW2_LIBS)
DEFINES += $(DEFINE)USE_GLFW2
ENABLE_GLAD := 1
endif
ifeq ($(DESKTOP_BACKEND),sdl1)
SDL1_LIBS += $(shell $(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --libs sdl)
LIBS += $(SDL1_LIBS)
DEFINES += $(DEFINE)USE_SDL1
endif
ifeq ($(DESKTOP_BACKEND),sdl2)
SDL2_LIBS += $(shell $(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --libs sdl2)
LIBS += $(SDL2_LIBS)
DEFINES += $(DEFINE)USE_SDL2
endif
ifeq ($(DESKTOP_BACKEND),sdl3)
SDL3_LIBS += $(shell $(PKG_CONFIG) $(PKG_CONFIG_FLAGS) --libs sdl3)
LIBS += $(SDL3_LIBS)
DEFINES += $(DEFINE)USE_SDL3
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
INCLUDES += $(INCLUDE)src/gl_common $(INCLUDE)src/gl
HEADERS += $(wildcard src/gl_common/*.h)
ENABLE_GLAD := 1
endif

ifndef DISABLE_LEGACY_GL
DEFINES += $(DEFINE)ENABLE_LEGACY_GL
SRCS += $(wildcard src/gl_legacy/*.c)
INCLUDES += $(INCLUDE)src/gl_legacy
HEADERS += $(wildcard src/gl_legacy/*.h) $(wildcard src/gl/*.h)
endif

ifndef DISABLE_MODERN_GL
DEFINES += $(DEFINE)ENABLE_MODERN_GL
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

ifeq ($(AUDIO_BACKEND),miniaudio)
INCLUDES += $(INCLUDE)src/audio/miniaudio $(INCLUDE)vendor/miniaudio
DEFINES += $(DEFINE)USE_MINIAUDIO
SRCS += $(wildcard src/audio/miniaudio/*.c)
HEADERS += $(wildcard src/audio/miniaudio/*.h)
ifneq ($(OS),Windows)
LIBS += -pthread
endif
endif
ifeq ($(AUDIO_BACKEND),openal)
INCLUDES += $(INCLUDE)src/audio/openal
DEFINES += $(DEFINE)USE_OPENAL
SRCS += $(wildcard src/audio/openal/*.c)
HEADERS += $(wildcard src/audio/openal/*.h)
ifeq ($(OS),Darwin)
LIBS += -framework OpenAL
else
LIBS += -lopenal
endif
endif

ifdef ENABLE_GLAD
SRCS += vendor/glad/src/glad.c
INCLUDES += $(INCLUDE)vendor/glad/include
endif

ifeq ($(OS),Windows)
ifndef MSVC
LIBS += -static
LIBS += -lwinmm
else
LIBS += winmm.lib
DEFINES += $(DEFINE)_CRT_SECURE_NO_WARNINGS
endif
DEFINES += $(DEFINE)WIN32_LEAN_AND_MEAN
else
ifeq ($(OS),Darwin)
LIBS += -lobjc
else
LIBS += -lm
endif
endif

ifndef VERBOSE
V := @
endif

OBJS := $(addprefix build/,$(SRCS:.c=.c.$(OBJ_EXT)))

all: build/butterscotch

-include $(OBJS:.$(OBJ_EXT)=.d)

ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)

compat/config.mk: compat/configure.sh compat/tmp/cc
	@CC="$(CC)" $(SHELL) compat/configure.sh

endif

build/butterscotch: $(OBJS)
	@{ [ -z "$(NO_COLOR)" ] && [ -t 1 ]; } && printf " \033[1;34mLD\033[0m butterscotch\n" || printf " LD butterscotch\n"
	$(V)$(_CC) $(LDFLAGS) $(OBJS) $(LIBS) $(EXTRALIBS) $(OUTPUT_EXE)$@
	@[ -f $@.exe ] && chmod +x $@.exe || true

build/%.c.$(OBJ_EXT): %.c compat/config.mk $(if $(DISABLE_MMD),$(HEADERS))
	@mkdir -p $(dir $@)
	@{ [ -z "$(NO_COLOR)" ] && [ -t 1 ]; } && printf " \033[1;32mCC\033[0m $<\n" || printf " CC $<\n"
	$(V)$(_CC) $(DEFINES) $(INCLUDES) $(CFLAGS) $(DEPFLAGS) $(COMPILE_OBJ) $< $(OUTPUT_OBJ)$@

clean:
	rm -rf build

distclean: clean
	rm -f compat/config.mk compat/tmp/cc
