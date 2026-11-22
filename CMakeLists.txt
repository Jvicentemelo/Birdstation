cmake_minimum_required(VERSION 3.21)
project(Birdstation VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Statically link the MSVC runtime so the .zip runs without the VC++ redistributable.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- SDL2 (downloaded and built statically) --------------------------------
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL2
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        release-2.30.9
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(SDL2)

# ---- Dear ImGui (vendored in third_party/) ---------------------------------
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2-static)

# ---- Birdstation -----------------------------------------------------------
set(BS_SOURCES
  src/main.cpp
  src/app.cpp
  src/core.cpp
  src/config.cpp
  src/input.cpp
  src/library.cpp
  src/covermatch.cpp)

if(WIN32)
  list(APPEND BS_SOURCES resources/birdstation.rc)
  add_executable(Birdstation WIN32 ${BS_SOURCES})
  target_link_libraries(Birdstation PRIVATE winhttp shell32 ole32)
else()
  add_executable(Birdstation ${BS_SOURCES})
endif()

target_include_directories(Birdstation PRIVATE
  src third_party/libretro third_party/stb)
target_link_libraries(Birdstation PRIVATE SDL2::SDL2main SDL2::SDL2-static imgui)
if(MSVC)
  target_compile_options(Birdstation PRIVATE /W3 /utf-8)
  target_compile_definitions(Birdstation PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
target_compile_definitions(Birdstation PRIVATE BIRDSTATION_VERSION="${PROJECT_VERSION}")

# Run-time assets next to the executable (portable layout).
add_custom_command(TARGET Birdstation POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Birdstation>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/assets/birdstation.png
          $<TARGET_FILE_DIR:Birdstation>/assets/birdstation.png)
