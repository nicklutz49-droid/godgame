cmake_minimum_required(VERSION 3.16)
project(godgame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

# ---- SDL2: prefer system package, fall back to building from source ----
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
  message(STATUS "System SDL2 not found - fetching release-2.30.11 and building it")
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.11
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL2)
endif()

# ---- glm: prefer system package, fall back to fetching (header-only) ----
find_package(glm QUIET)
if(NOT glm_FOUND AND NOT TARGET glm::glm AND NOT EXISTS "/usr/include/glm/glm.hpp")
  message(STATUS "System glm not found - fetching 1.0.1")
  FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glm)
endif()

# Warnings, applied to every target we own.
function(godgame_warnings tgt)
  if(MSVC)
    target_compile_options(${tgt} PRIVATE /W3 /permissive-)
  else()
    target_compile_options(${tgt} PRIVATE -Wall -Wextra)
  endif()
endfunction()

# glm (header-only) for a target: link the imported target when present,
# otherwise the system headers on the default include path already satisfy it.
function(godgame_link_glm tgt vis)
  if(TARGET glm::glm)
    target_link_libraries(${tgt} ${vis} glm::glm)
  endif()
endfunction()

# ---- godgame_sim: the engine-free deterministic simulation ----------------
# No GL, no SDL, no window. This is the checksummed model layer: the headless
# self-test suite links it today, and the future Godot GDExtension bridge will
# link this exact library so every determinism invariant lives in one place.
# MeshData.cpp is CPU-only geometry (Terrain builds meshes); the GL Mesh class
# stays in the app below.
add_library(godgame_sim STATIC
  src/World.cpp
  src/Village.cpp
  src/Villagers.cpp
  src/GodAI.cpp
  src/Terrain.cpp
  src/Hand.cpp
  src/Physics.cpp
  src/MapFile.cpp
  src/SaveFile.cpp
  src/MeshData.cpp
  src/bw/BWPack.cpp
  src/bw/BWLand.cpp
  src/bw/BWMesh.cpp
  src/bw/BWAssets.cpp
)
target_include_directories(godgame_sim PUBLIC src)
godgame_link_glm(godgame_sim PUBLIC)
godgame_warnings(godgame_sim)

# ---- godgame: the interactive app + headless suite (view + shell) ---------
add_executable(godgame
  src/main.cpp
  src/gl.cpp
  src/Shader.cpp
  src/Mesh.cpp
  src/Water.cpp
  src/Sky.cpp
  src/Camera.cpp
  src/Font.cpp
  src/Models.cpp
  src/Sound.cpp
)
target_link_libraries(godgame PRIVATE godgame_sim)

# We call SDL_SetMainReady() ourselves and use a plain main().
target_compile_definitions(godgame PRIVATE SDL_MAIN_HANDLED)

if(TARGET SDL2::SDL2)
  target_link_libraries(godgame PRIVATE SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
  target_link_libraries(godgame PRIVATE SDL2::SDL2-static)
else()
  target_include_directories(godgame PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(godgame PRIVATE ${SDL2_LIBRARIES})
endif()

godgame_link_glm(godgame PRIVATE)
godgame_warnings(godgame)
