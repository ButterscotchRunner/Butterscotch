# Makefile build
# meant to be extremely portable to weird unix-like systems

CC := cc

CFLAGS := -O2 -DNDEBUG
LIBS := -lbz2

DEFINES := -DBUTTERSCOTCH_COMMIT_DATE=\"unknown\" \
		   -DBUTTERSCOTCH_COMMIT_HASH=\"unknown\" \
		   -DENABLE_BC16 \
		   -DENABLE_BC17 \
		   -DENABLE_VM_GML_PROFILER \
		   -DENABLE_VM_OPCODE_PROFILER \
		   -DENABLE_VM_STUB_LOGS \
		   -DENABLE_VM_TRACING
INCLUDES := -I. -Isrc -Ivendor/stb/ds -Isrc/gl -Ivendor/stb/image -Ivendor/stb/vorbis -Ivendor/miniaudio -Ivendor/glad/include

HEADERS := $(wildcard src/*.h) \
		   $(wildcard src/gl/*.h) \
           $(shell find vendor -name '*.h')
SRCS := $(wildcard src/*.c) $(wildcard src/gl/*.c) \
		vendor/glad/src/glad.c

PLATFORM := glfw2
ifeq ($(PLATFORM),glfw)
LIBS += -lglfw
SRCS += $(wildcard src/glfw/*.c)
HEADERS += $(wildcard src/glfw/*.h)
else
ifeq ($(PLATFORM),glfw2)
LIBS += -lglfw
SRCS += $(wildcard src/glfw2/*.c)
HEADERS += $(wildcard src/glfw2/*.h)
else
$(error invalid platform)
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
