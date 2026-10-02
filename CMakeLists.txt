cmake_minimum_required(VERSION 3.21)

if(PLATFORM STREQUAL "ps3")
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/ppu.cmake)
endif()

if(PLATFORM STREQUAL "vita")
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
      if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
      else()
        message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
      endif()
    endif()

    set(BACKEND "sdl2")

    elseif(PLATFORM STREQUAL "switch")
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        if(NOT DEFINED ENV{DEVKITPRO})
            message(FATAL_ERROR "Please define DEVKITPRO to point to your SDK path!")
        endif()
        set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake" CACHE PATH "toolchain file")
    endif()

    set(BACKEND "sdl2")
endif()

project(butterscotch C)

if (PLATFORM STREQUAL "vita")
    include("${VITASDK}/share/vita.cmake" REQUIRED)
    set(VITA_APP_NAME "Butterscotch")
    set(VITA_TITLEID  "BUTTRSTCH")
    set(VITA_VERSION  "01.00")
    set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
    if(WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wvla)
    if(WERROR)
        add_compile_options(-Werror)
    endif()
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        # equivalent to -fomit-frame-pointer
        add_compile_options(/Oy)
    else()
        add_compile_options(-ffast-math -fno-finite-math-only)

        # some targets don't support -fomit-frame-pointer IIRC
        include(CheckCCompilerFlag)
        check_c_compiler_flag("-fomit-frame-pointer" HAS_FOMIT_FRAME_POINTER)
        if(HAS_FOMIT_FRAME_POINTER)
            add_compile_options(-fomit-frame-pointer)
        endif()
    endif()
endif()

set(PLATFORM "cli" CACHE STRING "Platform backend")
set(BACKEND "" CACHE STRING "Desktop platform backend")
set(AUDIO_BACKEND "" CACHE STRING "Audio backend")

if(PLATFORM STREQUAL "cli" AND BACKEND STREQUAL "")
    message(FATAL_ERROR "You need to set a BACKEND!")
endif()

# Build metadata
set(BUTTERSCOTCH_COMMIT_HASH "unknown" CACHE STRING "Git commit hash")
set(BUTTERSCOTCH_COMMIT_DATE "unknown" CACHE STRING "Git commit date")
add_compile_definitions(
    BUTTERSCOTCH_COMMIT_HASH="${BUTTERSCOTCH_COMMIT_HASH}"
    BUTTERSCOTCH_COMMIT_DATE="${BUTTERSCOTCH_COMMIT_DATE}"
)

# Audio backend defaults
if(AUDIO_BACKEND STREQUAL "")
    if(PLATFORM STREQUAL "ps3" OR PLATFORM STREQUAL "vita" OR PLATFORM STREQUAL "switch")
        set(AUDIO_BACKEND "openal")
    elseif(PLATFORM STREQUAL "ps2")
        set(AUDIO_BACKEND "ps2")
    else()
        set(AUDIO_BACKEND "miniaudio")
    endif()
endif()

if(AUDIO_BACKEND STREQUAL "openal")
    file(GLOB AUDIO_SOURCES src/audio/openal/*.c)
elseif(AUDIO_BACKEND STREQUAL "miniaudio")
    file(GLOB AUDIO_SOURCES src/audio/miniaudio/*.c)
    if(PLATFORM STREQUAL "vita")
        add_compile_definitions(MA_NO_RUNTIME_LINKING)
    endif()
elseif(AUDIO_BACKEND STREQUAL "web")
    file(GLOB AUDIO_SOURCES src/audio/web/*.c src/audio/miniaudio/*.c)
elseif(AUDIO_BACKEND STREQUAL "ps2")
    file(GLOB AUDIO_SOURCES src/audio/ps2/*.c)
elseif(AUDIO_BACKEND STREQUAL "fmod")
    file(GLOB AUDIO_SOURCES src/audio/fmod/*.c)
elseif(NOT AUDIO_BACKEND STREQUAL "none")
    message(FATAL_ERROR "Invalid AUDIO_BACKEND!")
endif()

if(AUDIO_BACKEND STREQUAL "openal")
    target_include_directories(butterscotch PUBLIC src/audio/openal)
    add_compile_definitions(USE_OPENAL)
    if(APPLE)
        set(AUDIO_LIBRARIES "-framework OpenAL")
    elseif(PLATFORM STREQUAL "vita" OR PLATFORM STREQUAL "switch")
        set(AUDIO_LIBRARIES openal pthread)
    elseif(NOT PLATFORM STREQUAL "ps3")
        find_package(OpenAL REQUIRED)
        target_include_directories(butterscotch PUBLIC "${OPENAL_INCLUDE_DIR}")
        set(AUDIO_LIBRARIES "${OPENAL_LIBRARY}")
    endif()
elseif(AUDIO_BACKEND STREQUAL "miniaudio")
    target_include_directories(butterscotch SYSTEM PUBLIC vendor/miniaudio)
    target_include_directories(butterscotch PUBLIC src/audio/miniaudio)
    add_compile_definitions(USE_MINIAUDIO)
    if (NOT WIN32)
        set(AUDIO_LIBRARIES pthread)
    endif()
elseif(AUDIO_BACKEND STREQUAL "web")
    target_include_directories(butterscotch SYSTEM PUBLIC vendor/miniaudio)
    target_include_directories(butterscotch PUBLIC src/audio/web)
    target_include_directories(butterscotch PUBLIC src/audio/miniaudio)
    add_compile_definitions(USE_WEB_AUDIO)
elseif(AUDIO_BACKEND STREQUAL "ps2")
    target_include_directories(butterscotch PUBLIC src/audio/ps2)
    add_compile_definitions(USE_PS2_AUDIO)
elseif(AUDIO_BACKEND STREQUAL "fmod")
    target_include_directories(butterscotch PUBLIC src/audio/fmod)
    add_compile_definitions(USE_FMOD)

    if(PLATFORM STREQUAL "android")
        set(FMOD_ROOT "$ENV{FMOD_DIR}" CACHE PATH "Path to the FMOD SDK root")
        if(NOT FMOD_ROOT)
            set(FMOD_ROOT "$ENV{FMOD_HOME}" CACHE PATH "Path to the FMOD SDK root" FORCE)
        endif()

        if(FMOD_ROOT)
            find_library(FMOD_LIBRARY NAMES fmod
                PATHS
                    "${FMOD_ROOT}/api/lowlevel/lib/aarch64-android"
                    "${FMOD_ROOT}/api/lowlevel/lib/arm64-v8a"
                    "${FMOD_ROOT}/lib/android"
                    "${FMOD_ROOT}/lib"
                    "${FMOD_ROOT}"
                NO_DEFAULT_PATH)
            find_path(FMOD_INCLUDE_DIR NAMES fmod.h
                PATHS
                    "${FMOD_ROOT}/api/lowlevel/inc"
                    "${FMOD_ROOT}/include"
                    "${FMOD_ROOT}/inc"
                    "${FMOD_ROOT}"
                NO_DEFAULT_PATH)
        endif()

        if(FMOD_LIBRARY AND FMOD_INCLUDE_DIR)
            target_include_directories(butterscotch SYSTEM PUBLIC "${FMOD_INCLUDE_DIR}")
            set(AUDIO_LIBRARIES "${FMOD_LIBRARY}")
            message(STATUS "Found FMOD for Android: ${FMOD_LIBRARY}")
        else()
            message(WARNING "FMOD SDK not found for Android; building stub FMOD backend")
            add_compile_definitions(BUTTERSCOTCH_FMOD_STUB)
            set(AUDIO_LIBRARIES "")
        endif()
    else()
        set(FMOD_ROOT "$ENV{FMOD_DIR}" CACHE PATH "Path to the FMOD SDK root")
        if(NOT FMOD_ROOT)
            set(FMOD_ROOT "$ENV{FMOD_HOME}" CACHE PATH "Path to the FMOD SDK root" FORCE)
        endif()

        if(FMOD_ROOT)
            find_library(FMOD_LIBRARY NAMES fmod fmodL
                PATHS
                    "${FMOD_ROOT}/lib"
                    "${FMOD_ROOT}/api/lowlevel/lib/x86_64"
                    "${FMOD_ROOT}/api/lowlevel/lib"
                    "${FMOD_ROOT}"
                NO_DEFAULT_PATH)
            find_path(FMOD_INCLUDE_DIR NAMES fmod.h
                PATHS
                    "${FMOD_ROOT}/api/lowlevel/inc"
                    "${FMOD_ROOT}/include"
                    "${FMOD_ROOT}/inc"
                    "${FMOD_ROOT}"
                NO_DEFAULT_PATH)
        endif()

        if(FMOD_LIBRARY AND FMOD_INCLUDE_DIR)
            target_include_directories(butterscotch SYSTEM PUBLIC "${FMOD_INCLUDE_DIR}")
            set(AUDIO_LIBRARIES "${FMOD_LIBRARY}")
            message(STATUS "Found FMOD: ${FMOD_LIBRARY}")
        else()
            message(WARNING "FMOD SDK not found; building stub FMOD backend")
            add_compile_definitions(BUTTERSCOTCH_FMOD_STUB)
            set(AUDIO_LIBRARIES "")
        endif()
    endif()
endif()

file(GLOB SOURCES src/*.c)

# These platforms haven't been converted to the refactored system yet.
# This list should NEVER grow, all new platforms should use the new system.
if(PLATFORM STREQUAL "ps2" OR PLATFORM STREQUAL "ps3" OR PLATFORM STREQUAL "android")
    list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/loop.c")
endif()

# Platform specific files builds
file(GLOB PLATFORM_SOURCES src/${PLATFORM}/*.c)

if(PLATFORM STREQUAL "android")
    add_library(butterscotch SHARED ${SOURCES} ${PLATFORM_SOURCES} ${AUDIO_SOURCES})
else()
    add_executable(butterscotch ${SOURCES} ${PLATFORM_SOURCES} ${AUDIO_SOURCES})
endif()

# Video playback backend
set(VIDEO_BACKEND "ffmpeg" CACHE STRING "Video playback backend (ffmpeg or none)")
set(BUTTERSCOTCH_FFMPEG_STATIC_LIBS "")

if(NOT VIDEO_BACKEND STREQUAL "ffmpeg" AND NOT VIDEO_BACKEND STREQUAL "none")
    message(FATAL_ERROR "Invalid VIDEO_BACKEND: ${VIDEO_BACKEND}")
endif()

if(VIDEO_BACKEND STREQUAL "ffmpeg")
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        if(PLATFORM STREQUAL "vita" OR PLATFORM STREQUAL "switch")
            # Console SDKs only ship static FFmpeg archives, and the private
            # dependency closure (mpg123, lame, zlib, ...) is not exposed by a
            # plain --libs query. Ask for the static closure and link it as a
            # single, normalised list so each archive is passed exactly once.
            execute_process(
                COMMAND ${PKG_CONFIG_EXECUTABLE} --static --libs libavformat libavcodec libavutil libswscale libswresample
                OUTPUT_VARIABLE _ffmpeg_static_libs
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _ffmpeg_static_rc)
            execute_process(
                COMMAND ${PKG_CONFIG_EXECUTABLE} --static --cflags libavformat libavcodec libavutil libswscale libswresample
                OUTPUT_VARIABLE _ffmpeg_static_cflags
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _ffmpeg_static_cflags_rc)
            if(_ffmpeg_static_rc EQUAL 0 AND NOT _ffmpeg_static_libs STREQUAL "")
                add_compile_definitions(BUTTERSCOTCH_FFMPEG)
                set(BUTTERSCOTCH_FFMPEG 1)
                separate_arguments(_ffmpeg_static_libs UNIX_COMMAND "${_ffmpeg_static_libs}")
                foreach(_lib IN LISTS _ffmpeg_static_libs)
                    if(_lib MATCHES "^-l(.+)$")
                        list(APPEND BUTTERSCOTCH_FFMPEG_STATIC_LIBS "${CMAKE_MATCH_1}")
                    elseif(_lib STREQUAL "-pthread")
                        list(APPEND BUTTERSCOTCH_FFMPEG_STATIC_LIBS pthread)
                    else()
                        list(APPEND BUTTERSCOTCH_FFMPEG_STATIC_LIBS "${_lib}")
                    endif()
                endforeach()
                if(NOT _ffmpeg_static_cflags_rc EQUAL 0)
                    set(_ffmpeg_static_cflags "")
                endif()
                separate_arguments(_ffmpeg_static_cflags UNIX_COMMAND "${_ffmpeg_static_cflags}")
                target_compile_options(butterscotch PRIVATE ${_ffmpeg_static_cflags})
            else()
                message(WARNING "FFmpeg development libraries not found - video playback will be disabled")
                add_compile_definitions(BUTTERSCOTCH_VIDEO_NULL)
                set(BUTTERSCOTCH_VIDEO_NULL 1)
            endif()
        else()
            pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
            if(FFMPEG_FOUND)
                add_compile_definitions(BUTTERSCOTCH_FFMPEG)
                set(BUTTERSCOTCH_FFMPEG 1)
                target_link_libraries(butterscotch PRIVATE PkgConfig::FFMPEG)
            else()
                message(WARNING "FFmpeg development libraries not found - video playback will be disabled")
                add_compile_definitions(BUTTERSCOTCH_VIDEO_NULL)
                set(BUTTERSCOTCH_VIDEO_NULL 1)
            endif()
        endif()
    else()
        message(WARNING "pkg-config not found - video playback will be disabled")
        add_compile_definitions(BUTTERSCOTCH_VIDEO_NULL)
        set(BUTTERSCOTCH_VIDEO_NULL 1)
    endif()
else()
    # VIDEO_BACKEND=none
    add_compile_definitions(BUTTERSCOTCH_VIDEO_NULL)
    set(BUTTERSCOTCH_VIDEO_NULL 1)
endif()

target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(butterscotch PRIVATE src/${PLATFORM})
target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/video)

if(AUDIO_BACKEND STREQUAL "openal")
    target_include_directories(butterscotch PUBLIC src/audio/openal)
    add_compile_definitions(USE_OPENAL)
    if(APPLE)
        set(AUDIO_LIBRARIES "-framework OpenAL")
    elseif(PLATFORM STREQUAL "vita" OR PLATFORM STREQUAL "switch")
        set(AUDIO_LIBRARIES openal pthread)
    elseif(NOT PLATFORM STREQUAL "ps3")
        find_package(OpenAL REQUIRED)
        target_include_directories(butterscotch PUBLIC "${OPENAL_INCLUDE_DIR}")
        set(AUDIO_LIBRARIES "${OPENAL_LIBRARY}")
    endif()
elseif(AUDIO_BACKEND STREQUAL "miniaudio")
    target_include_directories(butterscotch SYSTEM PUBLIC vendor/miniaudio)
    target_include_directories(butterscotch PUBLIC src/audio/miniaudio)
    add_compile_definitions(USE_MINIAUDIO)
    if (NOT WIN32)
        set(AUDIO_LIBRARIES pthread)
    endif()
elseif(AUDIO_BACKEND STREQUAL "web")
    target_include_directories(butterscotch SYSTEM PUBLIC vendor/miniaudio)
    target_include_directories(butterscotch PUBLIC src/audio/web)
    target_include_directories(butterscotch PUBLIC src/audio/miniaudio)
    add_compile_definitions(USE_WEB_AUDIO)
elseif(AUDIO_BACKEND STREQUAL "ps2")
    target_include_directories(butterscotch PUBLIC src/audio/ps2)
    add_compile_definitions(USE_PS2_AUDIO)
endif()

# stb_ds
add_library(stb_ds INTERFACE)
target_include_directories(stb_ds SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/vendor/stb/ds)

# stb_vorbis
add_library(stb_vorbis INTERFACE)
target_include_directories(stb_vorbis SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/vendor/stb/vorbis)

# RFC 1321 reference MD5 implementation
target_sources(butterscotch PRIVATE vendor/md5/md5.c)
target_include_directories(butterscotch PUBLIC vendor/md5)

# Steve Reid's SHA1 implementation
add_library(sha1 STATIC vendor/sha1/sha1.c)
target_include_directories(sha1 SYSTEM PUBLIC vendor/sha1)
target_compile_options(sha1 PRIVATE -w)

# Base64 implementation
target_sources(butterscotch PRIVATE vendor/base64/base64.c)
target_include_directories(butterscotch PUBLIC vendor/base64)

# bzip2
add_library(bzip2 STATIC
    vendor/bzip2/bz_internal_error.c
    vendor/bzip2/bzlib.c
    vendor/bzip2/crctable.c
    vendor/bzip2/decompress.c
    vendor/bzip2/huffman.c
    vendor/bzip2/randtable.c
)
target_include_directories(bzip2 PUBLIC vendor/bzip2)
target_compile_options(bzip2 PRIVATE -w)

# miniz 3.1.2 (https://github.com/richgel999/miniz, tag 3.1.2, commit 77d0dce86277).
# All files vendored byte-for-byte from that tag except:
#  - miniz_export.h, which upstream generates during its own CMake configure; this
#    static build just leaves MINIZ_EXPORT and MINIZ_NO_EXPORT empty since the
#    archive/stdio APIs are disabled above.
#  - miniz.h's MZ_FORCEINLINE for _MSC_VER, changed from __forceinline to __inline
#    because the zlib-compat layer writes "static MZ_FORCEINLINE" and older MSVC C
#    rejects "static __forceinline".
target_sources(butterscotch PRIVATE
    vendor/miniz/miniz.c
    vendor/miniz/miniz_tdef.c
    vendor/miniz/miniz_tinfl.c
)
target_include_directories(butterscotch SYSTEM PUBLIC vendor/miniz)
target_compile_definitions(butterscotch PRIVATE MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO)
if(NOT MSVC)
    set_source_files_properties(
        vendor/miniz/miniz.c
        vendor/miniz/miniz_tdef.c
        vendor/miniz/miniz_tinfl.c
        PROPERTIES COMPILE_OPTIONS "-w"
    )
endif()

# Video playback source
if(BUTTERSCOTCH_FFMPEG)
    target_sources(butterscotch PRIVATE src/video/ffmpeg/ffmpeg.c)
elseif(BUTTERSCOTCH_VIDEO_NULL)
    target_sources(butterscotch PRIVATE src/video/null_video.c)
endif()

# Spatial grid logs
option(ENABLE_SPATIAL_GRID_LOGS "Enable Spatial Grid logs" OFF)
if(ENABLE_SPATIAL_GRID_LOGS)
    add_compile_definitions(ENABLE_SPATIAL_GRID_LOGS)
endif()

# VM Exceptions logs
option(ENABLE_VM_EXCEPTIONS_LOGS "Enable VM Exceptions logs" OFF)
if(ENABLE_VM_EXCEPTIONS_LOGS)
    add_compile_definitions(ENABLE_VM_EXCEPTIONS_LOGS)
endif()

if(PLATFORM STREQUAL "cli" OR PLATFORM STREQUAL "vita" OR PLATFORM STREQUAL "switch" OR PLATFORM STREQUAL "web")
    if(PLATFORM STREQUAL "vita")
        # Normally, __VITA__ should already be defined, but for some reason,
        # in my case, this didn't really work... so just in case, when we're
        # building for the Vita, just force-feed these...
        add_compile_definitions(__VITA__)
        add_compile_definitions(PLATFORM_VITA)

        set(VM_GML_PROFILER_DEFAULT OFF)
        set(VM_TRACING_DEFAULT OFF)
        set(VM_OPCODE_PROFILER_DEFAULT OFF)
        set(VM_STUB_LOGS_DEFAULT OFF)
    elseif(PLATFORM STREQUAL "switch")
        add_compile_definitions(PLATFORM_SWITCH)

        set(VM_GML_PROFILER_DEFAULT OFF)
        set(VM_TRACING_DEFAULT OFF)
        set(VM_OPCODE_PROFILER_DEFAULT OFF)
        set(VM_STUB_LOGS_DEFAULT OFF)
    elseif(PLATFORM STREQUAL "web")
        add_compile_definitions(PLATFORM_WEB)
        target_sources(butterscotch PRIVATE src/cli/args.c)
    else()
        set(VM_GML_PROFILER_DEFAULT ON)
        set(VM_TRACING_DEFAULT ON)
        set(VM_OPCODE_PROFILER_DEFAULT ON)
        set(VM_STUB_LOGS_DEFAULT ON)
    endif()
    # Matches the GitHub Actions container flags (FORTIFY requires building with optimizations enabled!)
    target_compile_options(butterscotch PRIVATE "$<$<CONFIG:Release,RelWithDebInfo>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=2>")

    if(NOT ENABLE_LEGACY_GL AND NOT ENABLE_MODERN_GL AND NOT ENABLE_NOOP_RENDERER)
        message(FATAL_ERROR "You must enable at least one renderer!")
    endif()
    file(GLOB GL_SOURCES src/image/*.c src/gl_common/*.c)
    if(ENABLE_LEGACY_GL)
        file(GLOB GL_SOURCES ${GL_SOURCES} src/gl_legacy/*.c)
        target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/gl_legacy)
    endif()
    if(ENABLE_MODERN_GL)
        file(GLOB GL_SOURCES ${GL_SOURCES} src/gl/*.c)
    endif()
    target_sources(butterscotch PRIVATE ${GL_SOURCES})
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/gl)
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/gl_common)
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/image)

    file(GLOB DEBUG_FONT_SOURCES src/debug_font/*.c)
    target_sources(butterscotch PRIVATE ${DEBUG_FONT_SOURCES})
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/debug_font)

    if (MSVC)
        target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/compat/getopt)
        add_compile_definitions(NO_STRTOK_R)
        add_compile_definitions(NO_STRCASECMP)
    endif()

    # Butterscotch VM/interpreter profiler
    option(ENABLE_VM_GML_PROFILER "Enable Butterscotch VM/interpreter profiler" ${VM_GML_PROFILER_DEFAULT})
    if(ENABLE_VM_GML_PROFILER)
        add_compile_definitions(ENABLE_VM_GML_PROFILER)
    endif()

    # Butterscotch VM/interpreter tracing
    option(ENABLE_VM_TRACING "Enable Butterscotch VM/interpreter tracing checks" ${VM_TRACING_DEFAULT})
    if(ENABLE_VM_TRACING)
        add_compile_definitions(ENABLE_VM_TRACING)
    endif()

    # Butterscotch VM opcode count profiler
    option(ENABLE_VM_OPCODE_PROFILER "Enable Butterscotch VM opcode count profiler" ${VM_OPCODE_PROFILER_DEFAULT})
    if(ENABLE_VM_OPCODE_PROFILER)
        add_compile_definitions(ENABLE_VM_OPCODE_PROFILER)
    endif()

    # VM unknown/stubbed function logs
    option(ENABLE_VM_STUB_LOGS "Enable VM unknown/stubbed function logs" ${VM_STUB_LOGS_DEFAULT})
    if(ENABLE_VM_STUB_LOGS)
        add_compile_definitions(ENABLE_VM_STUB_LOGS)
    endif()

    # stb_image
    target_include_directories(butterscotch PUBLIC vendor/stb/image)

    set(PLATFORM_LIBRARIES bzip2 stb_ds sha1 stb_vorbis)
    if(NOT PLATFORM STREQUAL "vita" AND NOT PLATFORM STREQUAL "switch")
        # GLAD
        add_library(glad STATIC vendor/glad/src/glad.c)
        target_include_directories(glad PUBLIC vendor/glad/include)
        target_compile_options(glad PRIVATE -Wno-unused-but-set-variable)
        set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} glad)
    endif()
    if(NOT MSVC)
        set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} m)
    endif()
    if(WIN32)
        set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} opengl32 gdi32 winmm)
    elseif(NOT PLATFORM STREQUAL "vita" AND NOT PLATFORM STREQUAL "switch")
        set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} ${CMAKE_DL_LIBS})
    endif()

    if(BACKEND STREQUAL "glfw3")
        add_compile_definitions(USE_GLFW3)

        if(MINGW)
            find_package(glfw3 CONFIG REQUIRED)

            # Target Windows 7+ (avoids referencing newer APIs like MapViewOfFileNuma2 that MinGW import libs don't provide)
            # https://github.com/mirror/mingw-w64/blob/master/mingw-w64-headers/include/sdkddkver.h
            target_compile_definitions(butterscotch PRIVATE _WIN32_WINNT=0x0601 NTDDI_VERSION=0x06010000 WIN32_LEAN_AND_MEAN)
            target_link_options(butterscotch PRIVATE -static)
            set(BACKEND_LIBRARIES glfw opengl32 gdi32 winmm)
        elseif(MSVC)
            target_compile_definitions(butterscotch PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
            find_package(glfw3 CONFIG REQUIRED)
            set(BACKEND_LIBRARIES glfw opengl32 gdi32 winmm)
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(GLFW3 REQUIRED glfw3)
            target_include_directories(butterscotch PRIVATE ${GLFW3_INCLUDE_DIRS})
            target_link_directories(butterscotch PRIVATE ${GLFW3_LIBRARY_DIRS})
            set(BACKEND_LIBRARIES ${GLFW3_LIBRARIES})
        endif()
    elseif(BACKEND STREQUAL "glfw2")
        add_compile_definitions(USE_GLFW2)

        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW2 REQUIRED libglfw)
        target_include_directories(butterscotch PRIVATE ${GLFW2_INCLUDE_DIRS})
        target_link_directories(butterscotch PRIVATE ${GLFW2_LIBRARY_DIRS})
        set(BACKEND_LIBRARIES ${GLFW2_LIBRARIES})
    elseif(BACKEND STREQUAL "sdl1")
        add_compile_definitions(USE_SDL1)

        find_package(SDL REQUIRED)
        target_include_directories(butterscotch PUBLIC ${SDL_INCLUDE_DIR})
        set(BACKEND_LIBRARIES ${SDL_LIBRARY})
    elseif(BACKEND STREQUAL "sdl2")
        add_compile_definitions(USE_SDL2)

        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED sdl2)
        target_include_directories(butterscotch PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_directories(butterscotch PRIVATE ${SDL2_LIBRARY_DIRS})
        set(BACKEND_LIBRARIES ${SDL2_LIBRARIES})
    elseif(BACKEND STREQUAL "sdl3")
        add_compile_definitions(USE_SDL3)

        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL3 REQUIRED sdl3)
        target_include_directories(butterscotch PRIVATE ${SDL3_INCLUDE_DIRS})
        target_link_directories(butterscotch PRIVATE ${SDL3_LIBRARY_DIRS})
        set(BACKEND_LIBRARIES ${SDL3_LIBRARIES})
    elseif(BACKEND STREQUAL "appkit")
        add_compile_definitions(USE_APPKIT)
        target_sources(butterscotch PRIVATE src/backends/appkit.m)
        target_link_options(butterscotch PRIVATE -objc)
        set(BACKEND_LIBRARIES "-framework Cocoa -framework IOKit")
    elseif(BACKEND STREQUAL "noop")
        add_compile_definitions(USE_NOOP)
        set(BACKEND_LIBRARIES "")
    else()
        message(FATAL_ERROR "Invalid backend: ${BACKEND}")
    endif()
endif()

target_link_libraries(butterscotch PRIVATE ${BACKEND_LIBRARIES} ${PLATFORM_LIBRARIES} ${AUDIO_LIBRARIES})

if(PLATFORM STREQUAL "android")
    target_link_libraries(butterscotch PRIVATE log)
endif()
