cmake_minimum_required(VERSION 3.18)
project(imagetunnel LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(IT_BUILD_TESTS "Build unit tests" ON)

# Two platforms share this code:
#   Linux / Raspberry Pi : OpenGL ES 3.1, KMS/DRM kiosk, libcurl            (deploy/install.sh)
#   Windows (MSYS2 UCRT64): desktop OpenGL 4.3 via glad, WinHTTP, .scr screensaver, fully static
if(WIN32)
  set(IT_WINDOWS ON)
  enable_language(RC)
endif()

find_package(Threads REQUIRED)
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)  # allow FetchContent_Populate for non-CMake deps on newer CMake
endif()

# ---------------------------------------------------------------------------
# Platform libraries -> it_platform (INTERFACE target everything links against)
# ---------------------------------------------------------------------------
add_library(it_platform INTERFACE)
if(IT_WINDOWS)
  # Static SDL2 + libwebp from MSYS2 so ImageTunnel.scr is a single self-contained file.
  find_package(SDL2 REQUIRED CONFIG)
  find_library(WEBP_STATIC NAMES libwebp.a REQUIRED)
  find_library(SHARPYUV_STATIC NAMES libsharpyuv.a REQUIRED)
  find_path(WEBP_INCLUDE webp/decode.h REQUIRED)
  add_library(glad STATIC third_party/glad/src/gl.c)
  target_include_directories(glad PUBLIC third_party/glad/include)
  target_compile_definitions(it_platform INTERFACE IT_DESKTOP_GL IT_WINDOWS UNICODE _UNICODE NOMINMAX)
  target_include_directories(it_platform INTERFACE ${WEBP_INCLUDE})
  target_link_libraries(it_platform INTERFACE
    glad SDL2::SDL2-static ${WEBP_STATIC} ${SHARPYUV_STATIC} winhttp opengl32 dwmapi Threads::Threads)
  set(IMGUI_GL_DEFS "")
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)
  pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)
  find_package(CURL REQUIRED)
  target_link_libraries(it_platform INTERFACE
    PkgConfig::SDL2 PkgConfig::GLES PkgConfig::WEBP CURL::libcurl Threads::Threads)
  set(IMGUI_GL_DEFS IMGUI_IMPL_OPENGL_ES3)
endif()

find_package(nlohmann_json 3.9 QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(glm QUIET)
if(NOT TARGET glm::glm)
  FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glm)
endif()

# Header/source-only libraries without CMake support are fetched and wired up by hand.
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.5 GIT_SHALLOW TRUE)
FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG master GIT_SHALLOW TRUE)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
  FetchContent_Populate(stb)
endif()

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
if(IMGUI_GL_DEFS)
  target_compile_definitions(imgui PUBLIC ${IMGUI_GL_DEFS})
endif()
target_link_libraries(imgui PUBLIC it_platform)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Core library (everything except main) so tests can link against it
# ---------------------------------------------------------------------------
add_library(itcore STATIC
  src/config/Config.cpp
  src/assets/ImageSource.cpp
  src/assets/HttpClient.cpp
  src/assets/Decoder.cpp
  src/assets/AsyncLoader.cpp
  src/assets/TextureStreamer.cpp
  src/render/Gl.cpp
  src/render/Primitives.cpp
  src/render/Renderer.cpp
  src/modes/CardBatch.cpp
  src/modes/MovingLights.cpp
  src/modes/FloatingMode.cpp
  src/modes/TunnelMode.cpp
  src/modes/GridMode.cpp
  src/modes/MazeGen.cpp
  src/modes/MazeMode.cpp
  src/ui/Ui.cpp
  src/util/FrameStats.cpp
  src/util/FramePacer.cpp
  src/util/GpuTimer.cpp
  src/util/Thermal.cpp
  src/platform/Platform.cpp
  src/App.cpp)
target_include_directories(itcore PUBLIC src)
target_compile_definitions(itcore PUBLIC GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
target_link_libraries(itcore PUBLIC imgui stb glm::glm nlohmann_json::nlohmann_json it_platform)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(itcore PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(IT_WINDOWS)
  # ImageTunnel.scr: GUI subsystem screensaver (/s run, /c configure, /p preview).
  add_executable(imagetunnel_scr WIN32 src/main.cpp src/platform/windows/resources.rc)
  target_link_libraries(imagetunnel_scr PRIVATE itcore)
  set_target_properties(imagetunnel_scr PROPERTIES OUTPUT_NAME "ImageTunnel" SUFFIX ".scr")
  # Console build of the same program for benchmarking and logs (imagetunnel.exe --bench ...).
  add_executable(imagetunnel src/main.cpp)
  target_link_libraries(imagetunnel PRIVATE itcore)
  target_compile_definitions(imagetunnel PRIVATE IT_CONSOLE)
  foreach(t imagetunnel_scr imagetunnel)
    target_link_options(${t} PRIVATE -static -static-libgcc -static-libstdc++)
  endforeach()
else()
  add_executable(imagetunnel src/main.cpp)
  target_link_libraries(imagetunnel PRIVATE itcore)
  install(TARGETS imagetunnel RUNTIME DESTINATION bin)
endif()

if(IT_BUILD_TESTS)
  add_executable(imagetunnel_tests tests/tests.cpp)
  target_link_libraries(imagetunnel_tests PRIVATE itcore)
  if(IT_WINDOWS)
    target_link_options(imagetunnel_tests PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  enable_testing()
  add_test(NAME unit COMMAND imagetunnel_tests)
endif()
