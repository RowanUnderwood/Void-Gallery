cmake_minimum_required(VERSION 3.18)
project(imagetunnel LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(IT_BUILD_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------------------
# System dependencies (Raspberry Pi OS Bookworm: see deploy/install.sh)
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)  # allow FetchContent_Populate for non-CMake deps on newer CMake
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
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)
target_link_libraries(imgui PUBLIC PkgConfig::SDL2 PkgConfig::GLES)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Core library (everything except main) so tests can link against it
# ---------------------------------------------------------------------------
add_library(itcore STATIC
  src/config/Config.cpp
  src/assets/ImageSource.cpp
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
  src/util/Thermal.cpp
  src/App.cpp)
target_include_directories(itcore PUBLIC src)
target_compile_definitions(itcore PUBLIC GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
target_link_libraries(itcore PUBLIC
  imgui stb glm::glm nlohmann_json::nlohmann_json
  PkgConfig::SDL2 PkgConfig::GLES PkgConfig::WEBP CURL::libcurl Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(itcore PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_executable(imagetunnel src/main.cpp)
target_link_libraries(imagetunnel PRIVATE itcore)

if(IT_BUILD_TESTS)
  add_executable(imagetunnel_tests tests/tests.cpp)
  target_link_libraries(imagetunnel_tests PRIVATE itcore)
  enable_testing()
  add_test(NAME unit COMMAND imagetunnel_tests)
endif()

install(TARGETS imagetunnel RUNTIME DESTINATION bin)
