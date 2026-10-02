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
        else()
            message(WARNING "FMOD SDK not found; building stub FMOD backend")
            add_compile_definitions(BUTTERSCOTCH_FMOD_STUB)
            set(AUDIO_LIBRARIES "")
        endif()
    endif()
endif()
