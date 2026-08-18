# Only the mod TUs whose behaviour is decidable without a game process are
# compiled in - headtracking_mod.cpp, fov_override.cpp and hotkeys.cpp are left
# out because they only do anything against a live hook. The ones listed do pull
# core in (caller_trace.cpp needs the UE runtime for the module range,
# steamstub.cpp and config.cpp need core's logger and INI reader), which is what
# the target_link_libraries below satisfies.
add_executable(edithfinch_tests
    test_main.cpp
    view_injection_tests.cpp
    config_tests.cpp
    diagnostics_tests.cpp
    build_profile_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/caller_trace.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/exe_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/steamstub.cpp
    ${CMAKE_SOURCE_DIR}/src/builds/steam_offsets.cpp
)

target_include_directories(edithfinch_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/include
)

target_link_libraries(edithfinch_tests PRIVATE
    cameraunlock
    cameraunlock_unreal
)

if(MSVC)
    target_compile_options(edithfinch_tests PRIVATE /W4 /WX)
endif()

add_test(NAME edithfinch_tests COMMAND edithfinch_tests)
