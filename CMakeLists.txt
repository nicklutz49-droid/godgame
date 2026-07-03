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

add_executable(godgame
  src/main.cpp
  src/gl.cpp
  src/Shader.cpp
  src/Mesh.cpp
  src/Terrain.cpp
  src/Water.cpp
  src/Sky.cpp
  src/Camera.cpp
  src/World.cpp
  src/Hand.cpp
  src/Physics.cpp
  src/Village.cpp
  src/Villagers.cpp
  src/GodAI.cpp
  src/MapFile.cpp
  src/Models.cpp
)

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

if(TARGET glm::glm)
  target_link_libraries(godgame PRIVATE glm::glm)
endif()

if(MSVC)
  target_compile_options(godgame PRIVATE /W3 /permissive-)
else()
  target_compile_options(godgame PRIVATE -Wall -Wextra)
endif()
