include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# The first include sets up the toolchain before project(); the second configures the target.
if(NOT TARGET butterscotch)
    set(VCPKG_TARGET_TRIPLET "x64-uwp" CACHE STRING "vcpkg target triplet")
    if(NOT CMAKE_TOOLCHAIN_FILE)
        FetchContent_Declare(uwp_vcpkg
            URL https://github.com/microsoft/vcpkg/archive/d59510da59a2ee8b943509998b63da7b472d93cf.tar.gz
            URL_HASH SHA256=aa84624f370c2dce536ac9dc7fb30b840823dc0e21bb1487656696a59b66d22d
            SOURCE_SUBDIR unused)
        FetchContent_MakeAvailable(uwp_vcpkg)
        if(NOT EXISTS "${uwp_vcpkg_SOURCE_DIR}/vcpkg.exe")
            execute_process(
                COMMAND cmd /c "${uwp_vcpkg_SOURCE_DIR}/bootstrap-vcpkg.bat" -disableMetrics
                COMMAND_ERROR_IS_FATAL ANY)
        endif()
        set(CMAKE_TOOLCHAIN_FILE "${uwp_vcpkg_SOURCE_DIR}/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "CMake toolchain file")
    endif()
    return()
endif()

if(NOT CMAKE_GENERATOR MATCHES "^Visual Studio" OR NOT CMAKE_VS_PLATFORM_NAME STREQUAL "x64")
    message(FATAL_ERROR "UWP requires an x64 Visual Studio build (-A x64).")
endif()

include(ExternalProject)

set(SDL_UWP_ROOT "" CACHE PATH "Existing UWP SDL source directory; downloaded when empty")
if(NOT SDL_UWP_ROOT)
    FetchContent_Declare(uwp_sdl
        URL https://www.libsdl.org/release/SDL2-2.28.5.tar.gz
        URL_HASH SHA256=332cb37d0be20cb9541739c61f79bae5a477427d79ae85e352089afdaf6666e4
        SOURCE_SUBDIR VisualC-WinRT)
    FetchContent_MakeAvailable(uwp_sdl)
    set(SDL_UWP_ROOT "${uwp_sdl_SOURCE_DIR}")
endif()
FetchContent_Declare(uwp_angle
    URL https://api.nuget.org/v3-flatcontainer/angle.windowsstore/2.1.13/angle.windowsstore.2.1.13.nupkg
    DOWNLOAD_NAME angle-windowsstore.zip
    URL_HASH SHA256=d8f6c2bf2f6069bc2c1f77548140cf7c9dccd3aff79b15c5eb40c0e8f4032977)
FetchContent_MakeAvailable(uwp_angle)

if(NOT EXISTS "${SDL_UWP_ROOT}/VisualC-WinRT/SDL-UWP.vcxproj")
    message(FATAL_ERROR "Missing UWP SDL project: ${SDL_UWP_ROOT}/VisualC-WinRT/SDL-UWP.vcxproj")
endif()

get_filename_component(uwp_sdk_root "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
find_file(UWP_D3DCOMPILER_DLL d3dcompiler_47.dll
    HINTS "${uwp_sdk_root}/Redist/D3D/x64" "$ENV{WindowsSdkDir}/Redist/D3D/x64"
        "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Redist/D3D/x64"
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)

set(sdl_output "${CMAKE_CURRENT_BINARY_DIR}/sdl-uwp-angle")
set(SDL_UWP_LIBRARY "${sdl_output}/SDL2.lib")
set(SDL_UWP_DLL "${sdl_output}/SDL2.dll")
ExternalProject_Add(uwp_sdl_build
    SOURCE_DIR "${SDL_UWP_ROOT}"
    BINARY_DIR "${sdl_output}"
    BUILD_ALWAYS TRUE
    DOWNLOAD_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND "${CMAKE_VS_MSBUILD_COMMAND}" "<SOURCE_DIR>/VisualC-WinRT/SDL-UWP.vcxproj"
        /p:Configuration=Release /p:Platform=x64 "/p:PlatformToolset=${CMAKE_VS_PLATFORM_TOOLSET}"
        "/p:WindowsTargetPlatformVersion=${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
        "/p:OutDir=${sdl_output}/" "/p:IntDir=${sdl_output}/obj/"
        /p:GenerateProjectSpecificOutputFolder=false /m /v:minimal
    BUILD_BYPRODUCTS "${SDL_UWP_LIBRARY}" "${SDL_UWP_DLL}"
    INSTALL_COMMAND "")
add_dependencies(butterscotch uwp_sdl_build)

file(COPY "${SDL_UWP_ROOT}/include/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/include/SDL2")
target_include_directories(butterscotch PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include" "${SDL_UWP_ROOT}/include")
target_include_directories(butterscotch PRIVATE "${CMAKE_SOURCE_DIR}/vendor/nuklear")
target_sources(butterscotch PRIVATE
    src/uwp/frontend/launcher.c
    src/uwp/frontend/launch_options.c
    src/uwp/frontend/ui.c
    src/cli/stb_impl.c
    "${SDL_UWP_ROOT}/src/main/winrt/SDL2-WinRTResources.rc")

set_property(SOURCE src/data_win.c src/input_recording.c src/loop.c APPEND PROPERTY COMPILE_OPTIONS "/FI${CMAKE_SOURCE_DIR}/src/uwp/stdio_redirect.h")
# MSVC does not track sprSelf's initialization through the selfDirty guard.
set_property(SOURCE src/runner.c APPEND PROPERTY COMPILE_OPTIONS /w14703)
set(UWP_CERTIFICATE_THUMBPRINT "$ENV{UWP_CERTIFICATE_THUMBPRINT}" CACHE STRING "Signing certificate in CurrentUser/My; empty builds an unsigned package")
set(uwp_sign false)
if(UWP_CERTIFICATE_THUMBPRINT)
    set(uwp_sign true)
endif()
set_target_properties(butterscotch PROPERTIES WIN32_EXECUTABLE TRUE VS_GLOBAL_AppContainerApplication true VS_GLOBAL_AppxPackageSigningEnabled "${uwp_sign}" VS_GLOBAL_PackageCertificateThumbprint "${UWP_CERTIFICATE_THUMBPRINT}" VS_GLOBAL_AppxPackageExtension ".appx" VS_GLOBAL_AppxGeneratePrisForPortableLibrariesEnabled false VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION "10.0.19041.0")
set(BACKEND_LIBRARIES "${SDL_UWP_LIBRARY}" windowsapp)

target_compile_definitions(butterscotch PRIVATE DISABLE_DESKTOP_GL MA_NO_RUNTIME_LINKING MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_CUSTOM)

set(package_files "${CMAKE_SOURCE_DIR}/src/uwp/frontend/Package.appxmanifest")
set_source_files_properties(${package_files} PROPERTIES VS_DEPLOYMENT_CONTENT 1)
file(GLOB icons "${CMAKE_SOURCE_DIR}/src/uwp/frontend/Assets/*.png")
set_source_files_properties(${icons} PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "Assets")
list(APPEND package_files ${icons} "${SDL_UWP_DLL}")
set(ui_font "${CMAKE_SOURCE_DIR}/vendor/nuklear/Roboto-Regular.ttf")
set(ui_licenses "${CMAKE_SOURCE_DIR}/vendor/nuklear/LICENSE.txt" "${CMAKE_SOURCE_DIR}/vendor/nuklear/Roboto-LICENSE.txt")
set_source_files_properties("${ui_font}" PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "Assets")
set_source_files_properties(${ui_licenses} PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "Licenses/Nuklear")
list(APPEND package_files "${ui_font}" ${ui_licenses})
set(glyph_license "${CMAKE_SOURCE_DIR}/src/uwp/frontend/Assets/ControllerGlyphs-LICENSE.txt")
set_source_files_properties("${glyph_license}" PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "Licenses/ControllerGlyphs")
list(APPEND package_files "${glyph_license}")
set(graphics_dlls "${uwp_angle_SOURCE_DIR}/bin/UAP/x64/libEGL.dll" "${uwp_angle_SOURCE_DIR}/bin/UAP/x64/libGLESv2.dll" "${UWP_D3DCOMPILER_DLL}")
set(angle_license "${uwp_angle_SOURCE_DIR}/LICENSE")
set_source_files_properties("${angle_license}" PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "Licenses/ANGLE")
list(APPEND package_files "${angle_license}")
list(APPEND package_files ${graphics_dlls})
set_source_files_properties("${SDL_UWP_DLL}" ${graphics_dlls} PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION ".")
if(BUTTERSCOTCH_FFMPEG)
    set(ffmpeg_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    file(GLOB ffmpeg_dlls "${ffmpeg_root}/bin/av*.dll" "${ffmpeg_root}/bin/sw*.dll")
    foreach(dll IN LISTS ffmpeg_dlls)
        get_filename_component(name "${dll}" NAME)
        set(debug_dll "${ffmpeg_root}/debug/bin/${name}")
        set_source_files_properties("${dll}" PROPERTIES VS_DEPLOYMENT_CONTENT "$<NOT:$<CONFIG:Debug>>" VS_DEPLOYMENT_LOCATION ".")
        set_source_files_properties("${debug_dll}" PROPERTIES VS_DEPLOYMENT_CONTENT "$<CONFIG:Debug>" VS_DEPLOYMENT_LOCATION ".")
        list(APPEND package_files "${dll}" "${debug_dll}")
    endforeach()
    set(ffmpeg_license "${ffmpeg_root}/share/ffmpeg/copyright")
    set_source_files_properties("${ffmpeg_license}" PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "Licenses/FFmpeg")
    list(APPEND package_files "${ffmpeg_license}")
endif()

target_sources(butterscotch PRIVATE ${package_files})
