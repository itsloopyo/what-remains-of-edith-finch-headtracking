cmake_minimum_required(VERSION 3.20)
project(EdithFinchHeadTracking VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP)
endif()

# MinHook (for the GetPlayerViewPoint trampoline). Fetched + built locally;
# cameraunlock-core's hook manager links against this `minhook` target.
# Pinned to the commit v1.3.3 points at rather than the tag: a tag is mutable,
# and THIRD-PARTY-NOTICES.md has to be able to name exactly what we shipped.
include(FetchContent)
FetchContent_Declare(
    minhook_src
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        9fbd087432700d73fc571118d6a9697a36443d88  # v1.3.3, 2017-01-07
)
FetchContent_MakeAvailable(minhook_src)
add_library(minhook STATIC
    ${minhook_src_SOURCE_DIR}/src/hook.c
    ${minhook_src_SOURCE_DIR}/src/buffer.c
    ${minhook_src_SOURCE_DIR}/src/trampoline.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde64.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC ${minhook_src_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /wd4267 /wd4244 /wd4127 /wd4131)
endif()

# Cycling the inject mode live moves head tracking off the render caller, which
# looks exactly like a broken mod to a player who trips the chord by accident.
# The diagnostic is only wanted while re-deriving the render caller after a game
# patch, so it is compiled out of anything a player runs.
option(EDITHFINCH_DEV_HOTKEYS "Build the inject-mode diagnostic hotkeys (developer aid, not for release)" OFF)

# Shared cameraunlock-core (C++ side) with the hook manager and UE runtime
# helpers enabled.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_UNREAL ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/caller_trace.cpp
    src/config.cpp
    src/exe_paths.cpp
    src/fov_override.cpp
    src/hotkeys.cpp
    src/lean_trace.cpp
    src/steamstub.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
)

add_library(EdithFinchHeadTracking SHARED ${MOD_SOURCES})

target_include_directories(EdithFinchHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(EdithFinchHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    cameraunlock_unreal
    minhook
    ws2_32
    psapi
)

if(EDITHFINCH_DEV_HOTKEYS)
    target_compile_definitions(EdithFinchHeadTracking PRIVATE EDITHFINCH_DEV_HOTKEYS=1)
    message(WARNING "EDITHFINCH_DEV_HOTKEYS is ON. This build registers Ctrl+Shift+U "
                    "and Ctrl+Shift+J, which cycle which GetPlayerViewPoint caller is "
                    "injected and can leave the view not following the head. "
                    "Do not ship it.")
endif()

# Output as .asi for Ultimate ASI Loader.
set_target_properties(EdithFinchHeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Characterization tests for the parts of the hook that are decidable without a
# game process: the caller gate, the pose maths, the view-suppression rule and
# the INI contract. `pixi run test` builds and runs them.
option(EDITHFINCH_BUILD_TESTS "Build unit tests" ON)
if(EDITHFINCH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
