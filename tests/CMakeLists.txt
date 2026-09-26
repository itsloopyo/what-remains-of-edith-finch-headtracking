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
    ${CMAKE_SOURCE_DIR}/src/legacy_config/legacy_config.cpp
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

# The differential test for the config conversion. The oracle is the reader of
# the newest published build (v1.1.1), compiled from its own sources with the
# mod's and core's namespaces renamed, so it links beside today's code and
# nothing in it can resolve to a symbol of the mod under test. Its include roots
# put the published build's files ahead of core's headers.
add_library(finch_config_oracle STATIC
    config_differential/oracle_api.cpp
    config_differential/oracle/src/config.cpp
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/src/config/ini_reader.cpp
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/src/logging/file_log.cpp
)
target_include_directories(finch_config_oracle PRIVATE
    config_differential/oracle/src
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/include
)
target_compile_definitions(finch_config_oracle PRIVATE
    finch_ht=finch_ht_oracle
    cameraunlock=finch_oracle_core
)

add_executable(finch_config_differential_tests
    config_differential/differential_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/legacy_config/legacy_config.cpp
)
target_include_directories(finch_config_differential_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    config_differential
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/include
)
target_link_libraries(finch_config_differential_tests PRIVATE finch_config_oracle cameraunlock bcrypt)
target_compile_definitions(finch_config_differential_tests PRIVATE
    FINCH_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(MSVC)
    target_compile_options(finch_config_differential_tests PRIVATE /W4 /WX)
endif()
add_test(NAME finch_config_differential_tests COMMAND finch_config_differential_tests)
