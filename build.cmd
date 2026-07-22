@echo off
setlocal enabledelayedexpansion

if "%CC%"=="" set CC=cl
if "%CFLAGS%"=="" set CFLAGS=/O2 /DNDEBUG /nologo

if not exist compat\tmp mkdir compat\tmp
>compat\tmp\cc-new echo.%CC%
if not exist compat\config.mk goto run_config
if not exist compat\tmp\cc goto run_config
fc compat\tmp\cc compat\tmp\cc-new >nul 2>&1
if errorlevel 1 goto run_config
goto skip_config
:run_config
call compat\configure.cmd
copy /y compat\tmp\cc-new compat\tmp\cc >nul
:skip_config
del compat\tmp\cc-new 2>nul

if not defined DESKTOP_BACKEND set DESKTOP_BACKEND=glfw3
if not defined AUDIO_BACKEND set AUDIO_BACKEND=miniaudio

if defined DISABLE_LEGACY_GL if defined DISABLE_MODERN_GL (
    echo must enable at least 1 renderer
    exit /b 1
)

if defined DISABLE_WAD14 if defined DISABLE_WAD16 if defined DISABLE_WAD17 (
    echo must enable at least 1 bytecode version
    exit /b 1
)

set DEFINES=
set DEFINES=%DEFINES% /DENABLE_VM_GML_PROFILER
set DEFINES=%DEFINES% /DENABLE_VM_OPCODE_PROFILER
set DEFINES=%DEFINES% /DENABLE_VM_STUB_LOGS
set DEFINES=%DEFINES% /DENABLE_VM_TRACING
set DEFINES=%DEFINES% /DBUTTERSCOTCH_COMMIT_DATE=\"unknown\"
set DEFINES=%DEFINES% /DBUTTERSCOTCH_COMMIT_HASH=\"unknown\"
set DEFINES=%DEFINES% /D_CRT_SECURE_NO_WARNINGS
set DEFINES=%DEFINES% /DWIN32_LEAN_AND_MEAN
set DEFINES=%DEFINES% /DNO_STRTOK_R

if not defined DISABLE_WAD14 set DEFINES=%DEFINES% /DENABLE_WAD14
if not defined DISABLE_WAD16 set DEFINES=%DEFINES% /DENABLE_WAD16
if not defined DISABLE_WAD17 set DEFINES=%DEFINES% /DENABLE_WAD17
if not defined DISABLE_LEGACY_GL set DEFINES=%DEFINES% /DENABLE_LEGACY_GL
if not defined DISABLE_MODERN_GL set DEFINES=%DEFINES% /DENABLE_MODERN_GL

if "%DESKTOP_BACKEND%"=="glfw3" set DEFINES=%DEFINES% /DUSE_GLFW3
if "%DESKTOP_BACKEND%"=="glfw2" set DEFINES=%DEFINES% /DUSE_GLFW2
if "%DESKTOP_BACKEND%"=="sdl1" set DEFINES=%DEFINES% /DUSE_SDL1
if "%DESKTOP_BACKEND%"=="sdl2" set DEFINES=%DEFINES% /DUSE_SDL2
if "%DESKTOP_BACKEND%"=="sdl3" set DEFINES=%DEFINES% /DUSE_SDL3
if "%AUDIO_BACKEND%"=="miniaudio" set DEFINES=%DEFINES% /DUSE_MINIAUDIO
if "%AUDIO_BACKEND%"=="openal" set DEFINES=%DEFINES% /DUSE_OPENAL

set INCLUDES=
set INCLUDES=%INCLUDES% /I.
set INCLUDES=%INCLUDES% /Isrc
set INCLUDES=%INCLUDES% /Ivendor/stb/ds
set INCLUDES=%INCLUDES% /Isrc/image
set INCLUDES=%INCLUDES% /Ivendor/stb/image
set INCLUDES=%INCLUDES% /Ivendor/stb/vorbis
set INCLUDES=%INCLUDES% /Ivendor/md5
set INCLUDES=%INCLUDES% /Ivendor/sha1
set INCLUDES=%INCLUDES% /Ivendor/base64
set INCLUDES=%INCLUDES% /Ivendor/bzip2
set INCLUDES=%INCLUDES% /Isrc/desktop
set INCLUDES=%INCLUDES% /Icompat/getopt
set INCLUDES=%INCLUDES% /Ivendor/glad/include
if not defined DISABLE_LEGACY_GL set INCLUDES=%INCLUDES% /Isrc/gl_common /Isrc/gl_legacy /Isrc/gl
if not defined DISABLE_MODERN_GL (
    if defined DISABLE_LEGACY_GL set INCLUDES=%INCLUDES% /Isrc/gl_common
    set INCLUDES=%INCLUDES% /Isrc/gl
)
if "%AUDIO_BACKEND%"=="miniaudio" set INCLUDES=%INCLUDES% /Isrc/audio/miniaudio /Ivendor/miniaudio
if "%AUDIO_BACKEND%"=="openal" set INCLUDES=%INCLUDES% /Isrc/audio/openal

set LIBS=winmm.lib
if "%DESKTOP_BACKEND%"=="glfw3" (
    if defined GLFW3_LIBS ( set LIBS=%LIBS% %GLFW3_LIBS%
    ) else set LIBS=%LIBS% glfw3.lib opengl32.lib gdi32.lib
) else if "%DESKTOP_BACKEND%"=="glfw2" (
    if defined GLFW2_LIBS ( set LIBS=%LIBS% %GLFW2_LIBS%
    ) else set LIBS=%LIBS% glfw.lib opengl32.lib gdi32.lib
) else if "%DESKTOP_BACKEND%"=="sdl1" (
    if defined SDL1_LIBS ( set LIBS=%LIBS% %SDL1_LIBS%
    ) else set LIBS=%LIBS% sdl.lib
) else if "%DESKTOP_BACKEND%"=="sdl2" (
    if defined SDL2_LIBS ( set LIBS=%LIBS% %SDL2_LIBS%
    ) else set LIBS=%LIBS% sdl2.lib
) else if "%DESKTOP_BACKEND%"=="sdl3" (
    if defined SDL3_LIBS ( set LIBS=%LIBS% %SDL3_LIBS%
    ) else set LIBS=%LIBS% sdl3.lib
)

set SRCS=
set SRCS=%SRCS% src\*.c
set SRCS=%SRCS% src\image\*.c
set SRCS=%SRCS% src\desktop\*.c
set SRCS=%SRCS% "src\desktop\backends\%DESKTOP_BACKEND%.c"
set SRCS=%SRCS% vendor\bzip2\*.c
set SRCS=%SRCS% vendor\md5\*.c
set SRCS=%SRCS% vendor\sha1\*.c
set SRCS=%SRCS% vendor\base64\*.c
set SRCS=%SRCS% vendor\glad\src\glad.c
if not defined DISABLE_LEGACY_GL set SRCS=%SRCS% src\gl_common\*.c src\gl_legacy\*.c
if not defined DISABLE_MODERN_GL (
    if defined DISABLE_LEGACY_GL set SRCS=%SRCS% src\gl_common\*.c
    set SRCS=%SRCS% src\gl\*.c
)
if "%AUDIO_BACKEND%"=="miniaudio" set SRCS=%SRCS% src\audio\miniaudio\*.c
if "%AUDIO_BACKEND%"=="openal" set SRCS=%SRCS% src\audio\openal\*.c

if exist compat\config.bat call compat\config.bat

if not exist build mkdir build

for %%f in (%SRCS%) do (
    %CC% %CFLAGS% %DEFINES% %INCLUDES% /c "%%f" /Fobuild\
    if errorlevel 1 exit /b 1
)

%CC% %CFLAGS% build\*.obj /Febuild\butterscotch.exe /link %LIBS% %EXTRALIBS%
if errorlevel 1 exit /b 1
