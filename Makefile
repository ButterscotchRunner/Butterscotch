# SPOOKY!! THIS MAKEFILE IS MADE BY AI WITH EDITS FROM ME
# WILL/MAY REMAKE LATER!!! (when i actually learn makefiles smh)

.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules

#---------------------------------------------------------------------------------
TARGET      := ButterscotchDS
BUILD       := build_nds
SOURCES     := src src/debug_font src/image src/video src/nds \
               vendor/bzip2 vendor/miniz vendor/md5 vendor/sha1 vendor/base64
INCLUDES    := . src src/image src/debug_font src/video src/nds \
               vendor/stb/ds vendor/stb/image vendor/stb/vorbis \
               vendor/md5 vendor/sha1 vendor/base64 vendor/bzip2 vendor/miniz
DATA        :=
NITRODATA   := nitrofs

GAME_TITLE     := Butterscotch DS
GAME_SUBTITLE1 := GML runner for DS
GAME_SUBTITLE2 := built with devkitARM

#---------------------------------------------------------------------------------
# code generation
#---------------------------------------------------------------------------------
ARCH     := -march=armv5te -mtune=arm946e-s -mthumb

DEFINES  := -DARM9 -D__NDS__ \
            -DBUTTERSCOTCH_COMMIT_DATE=\"unknown\" \
            -DBUTTERSCOTCH_COMMIT_HASH=\"unknown\" \
            -DENABLE_WAD14 -DENABLE_WAD16 -DENABLE_WAD17 \
            -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_STDIO \
            -DBUTTERSCOTCH_VIDEO_NULL \
            -DUSE_NDS
# Profiler/tracing/stub-log defines are left off (they're opt-out in the main Makefile)

CFLAGS   := -g -Wall -O2 -std=gnu11 -ffunction-sections -fdata-sections $(ARCH)
CFLAGS   += $(INCLUDE) $(DEFINES)
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=ds_arm9.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(notdir $*.map)

LIBS     := -lfilesystem -lfat -lnds9 -lm
LIBDIRS  := $(LIBNDS)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH  := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                 $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES := $(addsuffix .o,$(BINFILES)) \
                 $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

# -I instead of -iquote: the repo uses <angle> includes for vendor headers
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifneq ($(strip $(NITRODATA)),)
	export NITRO_FILES := $(CURDIR)/$(NITRODATA)
endif

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).nds : $(OUTPUT).elf
$(OUTPUT).elf : $(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------