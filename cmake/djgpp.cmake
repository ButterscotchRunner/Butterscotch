# Cross-compilation toolchain for DJGPP (32-bit DOS).
#
# Select a compiler explicitly with -DDJGPP_CC=/path/to/i586-pc-msdosdjgpp-gcc,
# or put i586-pc-msdosdjgpp-gcc on PATH.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR i386)
set(CMAKE_EXECUTABLE_SUFFIX ".exe")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

if(DEFINED DJGPP_CC)
    set(CMAKE_C_COMPILER "${DJGPP_CC}" CACHE FILEPATH "DJGPP C compiler")
else()
    find_program(DJGPP_C_COMPILER
        NAMES i586-pc-msdosdjgpp-gcc djgpp-gcc
        REQUIRED
    )
    set(CMAKE_C_COMPILER "${DJGPP_C_COMPILER}" CACHE FILEPATH "DJGPP C compiler")
endif()

get_filename_component(_djgpp_bin_dir "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(DJGPP_AR NAMES i586-pc-msdosdjgpp-ar djgpp-ar ar HINTS "${_djgpp_bin_dir}")
find_program(DJGPP_RANLIB NAMES i586-pc-msdosdjgpp-ranlib djgpp-ranlib ranlib HINTS "${_djgpp_bin_dir}")
if(DJGPP_AR)
    set(CMAKE_AR "${DJGPP_AR}" CACHE FILEPATH "DJGPP archiver")
endif()
if(DJGPP_RANLIB)
    set(CMAKE_RANLIB "${DJGPP_RANLIB}" CACHE FILEPATH "DJGPP ranlib")
endif()
