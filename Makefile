# Makefile build
# meant to be extremely portable to weird unix-like systems

CC := cc

CFLAGS := -O2 -DNDEBUG
LIBS := -lbz2

OS := $(shell uname -s)

DEFINES := -DBUTTERSCOTCH_COMMIT_DATE=\"unknown\" \
		   -DBUTTERSCOTCH_COMMIT_HASH=\"unknown\" \
		   -DENABLE_VM_GML_PROFILER \
		   -DENABLE_VM_OPCODE_PROFILER \
		   -DENABLE_VM_STUB_LOGS \
		   -DENABLE_VM_TRACING
INCLUDES := -I. -Isrc -Ivendor/stb/ds -Isrc/gl -Isrc/gl_legacy -Isrc/image -Ivendor/stb/image -Ivendor/stb/vorbis -Ivendor/md5 -Ivendor/miniaudio -Ivendor/glad/include

HEADERS := $(wildcard src/*.h) \
		   $(wildcard src/gl/*.h) \
           $(shell find vendor -name '*.h')
SRCS := $(wildcard src/*.c) $(wildcard src/gl/*.c) $(wildcard src/image/*.c) vendor/md5/md5.c vendor/glad/src/glad.c

ifndef DISABLE_BC16
DEFINES += -DENABLE_BC16
endif

ifndef DISABLE_BC17
DEFINES += -DENABLE_BC17
endif

ifdef DISABLE_BC16
ifdef DISABLE_BC17
$(error must enable at least 1 bytecode version)
endif
endif

ifdef ENABLE_GLES
DEFINES += -DENABLE_GLES
else
SRCS += $(wildcard src/gl_legacy/*.c)
HEADERS += $(wildcard src/gl_legacy/*.h)
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
