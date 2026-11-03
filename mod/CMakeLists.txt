cmake_minimum_required(VERSION 3.21)
project(TDURecompilationObservationMod LANGUAGES CXX)
enable_testing()

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build this observation module separately with MSVC -A Win32")
endif()

set(DETOURS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/detours")
add_library(tdur_detours STATIC
    "${DETOURS_DIR}/detours.cpp" "${DETOURS_DIR}/modules.cpp"
    "${DETOURS_DIR}/disasm.cpp" "${DETOURS_DIR}/image.cpp"
    "${DETOURS_DIR}/creatwth.cpp" "${DETOURS_DIR}/disolx86.cpp"
    "${DETOURS_DIR}/disolx64.cpp" "${DETOURS_DIR}/disolia64.cpp"
    "${DETOURS_DIR}/disolarm.cpp" "${DETOURS_DIR}/disolarm64.cpp")
target_include_directories(tdur_detours SYSTEM PUBLIC "${DETOURS_DIR}")
target_compile_features(tdur_detours PRIVATE cxx_std_17)
target_compile_definitions(tdur_detours PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(tdur_detours PRIVATE /W4 /WX /EHsc)

add_library(tdur_installation STATIC ../src/game/installation.cpp)
target_include_directories(tdur_installation PUBLIC ../include)
target_compile_options(tdur_installation PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_compile_definitions(tdur_installation PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(tdur_installation PRIVATE bcrypt)

add_library(tdur_physics_binding STATIC ../src/diagnostics/physics_binding.cpp)
target_include_directories(tdur_physics_binding PUBLIC ../include)
target_compile_options(tdur_physics_binding PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)

add_executable(tdur_probe_physics_binding ../src/runtime_physics_binding_probe_main.cpp)
target_link_libraries(tdur_probe_physics_binding PRIVATE tdur_physics_binding tdur_installation)
target_compile_options(tdur_probe_physics_binding PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_compile_definitions(tdur_probe_physics_binding PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
add_test(NAME physics_binding_win32_reader COMMAND tdur_probe_physics_binding --self-test)
set_tests_properties(physics_binding_win32_reader PROPERTIES TIMEOUT 15)

add_executable(tdur_physics_binding_test ../tests/physics_binding_test.cpp)
target_link_libraries(tdur_physics_binding_test PRIVATE tdur_physics_binding)
target_compile_options(tdur_physics_binding_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_test(NAME physics_binding_bounded_core COMMAND tdur_physics_binding_test)

add_library(tdur_retail_video_defaults STATIC
    src/retail_video_defaults.cpp src/retail_background.cpp
    src/retail_background_gate.cpp
    src/retail_physics_context.cpp)
target_include_directories(tdur_retail_video_defaults PRIVATE include ../include)
target_compile_options(tdur_retail_video_defaults PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_compile_definitions(tdur_retail_video_defaults PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(tdur_retail_video_defaults PUBLIC tdur_installation)

add_library(tdur_code_patch STATIC src/code_patch.cpp)
target_include_directories(tdur_code_patch PUBLIC include)
target_compile_options(tdur_code_patch PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_compile_definitions(tdur_code_patch PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(tdur_retail_video_defaults PUBLIC tdur_code_patch tdur_detours)

function(tdur_proxy target variant)
    add_library(${target} SHARED src/dinput8_proxy.cpp src/observation_hooks.cpp
        src/display_startup_gate.cpp
        src/cursor_diagnostics.cpp src/dinput8_proxy.def)
    target_include_directories(${target} PRIVATE include)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_options(${target} PRIVATE /W4 /WX /EHsc /std:c++latest)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(${target} PRIVATE tdur_detours tdur_display_hooks tdur_retail_video_defaults
        shell32 ole32 uuid advapi32)
    tdur_proxy_gamepad(${target})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME dinput8
        ARCHIVE_OUTPUT_NAME "${target}_import"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${variant}")
endfunction()

include(cmake/gamepad.cmake)
tdur_proxy(tdur_observation_proxy normal)
tdur_proxy(tdur_observation_mutant_source mutant-source)
target_compile_definitions(tdur_observation_mutant_source PRIVATE TDUR_MUTANT_ALLOW_SOURCE_WRITE)

add_executable(tdur_mod_witness tests/observation_witness.cpp)
target_compile_features(tdur_mod_witness PRIVATE cxx_std_23)
target_compile_definitions(tdur_mod_witness PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(tdur_mod_witness PRIVATE /W4 /WX /EHsc /std:c++latest)
target_link_libraries(tdur_mod_witness PRIVATE dinput8 dxguid shell32 ole32 uuid)
set_target_properties(tdur_mod_witness PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/normal")
add_dependencies(tdur_mod_witness tdur_observation_proxy tdur_observation_mutant_source)

add_custom_command(TARGET tdur_mod_witness POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:tdur_mod_witness>"
        "$<TARGET_FILE_DIR:tdur_observation_mutant_source>/tdur_mod_witness.exe"
    COMMAND "${CMAKE_COMMAND}" -DWITNESS="$<TARGET_FILE:tdur_mod_witness>"
        -DPROXY="$<TARGET_FILE:tdur_observation_proxy>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/write_manifest.cmake"
    COMMAND "${CMAKE_COMMAND}" -DWITNESS="$<TARGET_FILE_DIR:tdur_observation_mutant_source>/tdur_mod_witness.exe"
        -DPROXY="$<TARGET_FILE:tdur_observation_mutant_source>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/write_manifest.cmake"
    VERBATIM)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(tdur_physics_context_test
    tests/physics_context_test.cpp src/retail_physics_context.cpp)
target_include_directories(tdur_physics_context_test PRIVATE include)
target_link_libraries(tdur_physics_context_test PRIVATE tdur_detours)
target_compile_definitions(tdur_physics_context_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX TDUR_SOURCE_OWNED_PHYSICS_CONTEXT_GATE)
target_compile_options(tdur_physics_context_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_test(NAME physics_entry_context_observation COMMAND tdur_physics_context_test)
set_tests_properties(physics_entry_context_observation PROPERTIES TIMEOUT 15)

add_executable(tdur_code_patch_test tests/code_patch_test.cpp)
target_link_libraries(tdur_code_patch_test PRIVATE tdur_code_patch)
target_compile_options(tdur_code_patch_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_compile_definitions(tdur_code_patch_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_test(NAME background_code_transaction COMMAND tdur_code_patch_test)
add_test(NAME background_fatal_restoration
    COMMAND "${Python3_EXECUTABLE}" -I
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_code_patch_fatal_fixture.py"
        --fixture "$<TARGET_FILE:tdur_code_patch_test>")
set_tests_properties(background_code_transaction background_fatal_restoration
    PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
add_executable(tdur_renderer_gate_abi_test
    tests/renderer_gate_abi_test.cpp src/retail_background_gate.cpp)
target_include_directories(tdur_renderer_gate_abi_test PRIVATE include)
target_link_libraries(tdur_renderer_gate_abi_test PRIVATE tdur_detours)
target_compile_definitions(tdur_renderer_gate_abi_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX TDUR_SOURCE_OWNED_RENDERER_GATE)
target_compile_options(tdur_renderer_gate_abi_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_test(NAME background_renderer_initializer_abi COMMAND tdur_renderer_gate_abi_test)
set_tests_properties(background_renderer_initializer_abi PROPERTIES TIMEOUT 10)
add_subdirectory(launcher)
add_executable(tdur_native_service_path_test tests/native_service_path_test.cpp)
target_include_directories(tdur_native_service_path_test PRIVATE include)
target_compile_features(tdur_native_service_path_test PRIVATE cxx_std_23)
target_compile_options(tdur_native_service_path_test PRIVATE /W4 /WX /EHsc /std:c++latest)
add_test(NAME observation_native_service_paths COMMAND tdur_native_service_path_test)

add_executable(tdur_native_input_path_test tests/native_input_path_test.cpp)
target_include_directories(tdur_native_input_path_test PRIVATE include)
target_compile_features(tdur_native_input_path_test PRIVATE cxx_std_23)
target_compile_options(tdur_native_input_path_test PRIVATE /W4 /WX /EHsc /std:c++latest)
add_test(NAME observation_native_input_paths COMMAND tdur_native_input_path_test)
add_test(NAME observation_proxy_and_documents
    COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_fixture.py"
        --witness "$<TARGET_FILE:tdur_mod_witness>" --work-root "${CMAKE_BINARY_DIR}/fixture-normal")
add_test(NAME observation_cursor_installation
    COMMAND "${Python3_EXECUTABLE}" -I -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_fixture.py"
        --witness "$<TARGET_FILE:tdur_mod_witness>"
        --work-root "${CMAKE_BINARY_DIR}/fixture-cursor-installed"
        --cursor-diagnostics)
add_test(NAME observation_guard_mutation_detected
    COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_fixture.py"
        --witness "$<TARGET_FILE_DIR:tdur_observation_mutant_source>/tdur_mod_witness.exe"
        --work-root "${CMAKE_BINARY_DIR}/fixture-mutant" --expect-source-mutant)
set_tests_properties(observation_proxy_and_documents observation_guard_mutation_detected
    PROPERTIES TIMEOUT 45)
set_tests_properties(observation_cursor_installation PROPERTIES TIMEOUT 45)

include("${CMAKE_CURRENT_SOURCE_DIR}/tests/display_backend_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/tests/display_hooks_tests.cmake")

# Injected-API witness of the opt-in cursor diagnostics core.
add_executable(tdur_cursor_diagnostics_test
    tests/cursor_diagnostics_test.cpp src/cursor_diagnostics_core.cpp)
target_include_directories(tdur_cursor_diagnostics_test PRIVATE include)
target_compile_definitions(tdur_cursor_diagnostics_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(tdur_cursor_diagnostics_test PRIVATE
    /std:c++latest /W4 /WX /utf-8 /permissive- /EHsc)
target_link_libraries(tdur_cursor_diagnostics_test PRIVATE comctl32 user32)
add_test(NAME observation_cursor_diagnostics
    COMMAND tdur_cursor_diagnostics_test)
set_tests_properties(observation_cursor_diagnostics PROPERTIES TIMEOUT 45)

add_executable(tdur_foreground_cursor_test
    tests/foreground_cursor_test.cpp src/foreground_cursor_core.cpp)
target_include_directories(tdur_foreground_cursor_test PRIVATE include)
target_compile_definitions(tdur_foreground_cursor_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(tdur_foreground_cursor_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_link_libraries(tdur_foreground_cursor_test PRIVATE user32)
add_test(NAME observation_foreground_cursor_policy
    COMMAND tdur_foreground_cursor_test)
set_tests_properties(observation_foreground_cursor_policy PROPERTIES TIMEOUT 15)
include("${CMAKE_CURRENT_SOURCE_DIR}/tests/display_failure_tests.cmake")

add_executable(tdur_display_resource_test tests/display_resource_test.cpp)
target_include_directories(tdur_display_resource_test PRIVATE include)
target_link_libraries(tdur_display_resource_test PRIVATE tdur_display_hooks d3d9 user32)
target_compile_options(tdur_display_resource_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_compile_definitions(tdur_display_resource_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
add_test(NAME display_resource_descriptions COMMAND tdur_display_resource_test)
set_tests_properties(display_resource_descriptions PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE)

add_executable(tdur_display_startup_witness tests/display_startup_witness.cpp)
target_compile_options(tdur_display_startup_witness PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_compile_definitions(tdur_display_startup_witness PRIVATE NOMINMAX UNICODE _UNICODE)
target_link_libraries(tdur_display_startup_witness PRIVATE d3d9 d3d12 user32)
add_test(NAME display_injected_deferred_startup
    COMMAND "${Python3_EXECUTABLE}" -I
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_display_startup_fixture.py"
        --launcher "$<TARGET_FILE:tdur_observe_launch>"
        --witness "$<TARGET_FILE:tdur_display_startup_witness>"
        --dll "$<TARGET_FILE:tdur_observation_proxy>"
        --work-root "${CMAKE_BINARY_DIR}/display-startup-fixtures")
set_tests_properties(display_injected_deferred_startup PROPERTIES TIMEOUT 35 RUN_SERIAL TRUE)
add_test(NAME display_injected_exclusive_pending
    COMMAND "${Python3_EXECUTABLE}" -I
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_display_startup_fixture.py"
        --launcher "$<TARGET_FILE:tdur_observe_launch>"
        --witness "$<TARGET_FILE:tdur_display_startup_witness>"
        --dll "$<TARGET_FILE:tdur_observation_proxy>"
        --work-root "${CMAKE_BINARY_DIR}/display-startup-fixtures"
        --mode exclusive)
set_tests_properties(display_injected_exclusive_pending PROPERTIES TIMEOUT 35 RUN_SERIAL TRUE)
