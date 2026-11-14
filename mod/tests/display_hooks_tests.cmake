# Include after display_backend_tests.cmake; never activates the proxy by itself.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Display hooks currently require the x86 public COM ABI")
endif()
get_filename_component(TDUR_DISPLAY_HOOK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT TARGET tdur_detours)
    if(NOT TDUR_DETOURS_LIBRARY)
        message(FATAL_ERROR "Provide TDUR_DETOURS_LIBRARY for a standalone fixture build")
    endif()
    add_library(tdur_detours STATIC IMPORTED)
    set_target_properties(tdur_detours PROPERTIES
        IMPORTED_LOCATION "${TDUR_DETOURS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TDUR_DISPLAY_HOOK_ROOT}/mod/src/vendor/detours")
endif()
add_library(tdur_display_hooks STATIC "${TDUR_DISPLAY_HOOK_ROOT}/mod/src/display_hooks.cpp"
    "${TDUR_DISPLAY_HOOK_ROOT}/mod/src/cursor_diagnostics_core.cpp"
    "${TDUR_DISPLAY_HOOK_ROOT}/mod/src/frametime_recorder.cpp"
    "${TDUR_DISPLAY_HOOK_ROOT}/mod/src/foreground_cursor_core.cpp"
    "${TDUR_DISPLAY_HOOK_ROOT}/mod/src/window_diagnostics_core.cpp")
target_link_libraries(tdur_display_hooks PUBLIC tdur_display_backend PRIVATE tdur_detours comctl32)
target_compile_options(tdur_display_hooks PRIVATE /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_executable(tdur_display_hooks_test "${TDUR_DISPLAY_HOOK_ROOT}/mod/tests/display_hooks_test.cpp")
target_link_libraries(tdur_display_hooks_test PRIVATE tdur_display_hooks d3d9)
target_compile_options(tdur_display_hooks_test PRIVATE /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_test(NAME display_classic_hooks_reset_identity COMMAND tdur_display_hooks_test)
set_tests_properties(display_classic_hooks_reset_identity PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
# C2 through the real subclass and C3 at Reset, up to the second native Reset
# only: that Reset fails on some machines and would hide a regression here.
add_test(NAME display_classic_hooks_wiring
    COMMAND tdur_display_hooks_test --stop-before-second-reset)
set_tests_properties(display_classic_hooks_wiring PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
# C3: window operation and presentation request diagnostics, in memory only.
add_executable(tdur_window_diagnostics_test
    "${TDUR_DISPLAY_HOOK_ROOT}/mod/tests/window_diagnostics_test.cpp")
target_link_libraries(tdur_window_diagnostics_test PRIVATE tdur_display_hooks user32)
target_compile_options(tdur_window_diagnostics_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_test(NAME display_window_diagnostics COMMAND tdur_window_diagnostics_test)
set_tests_properties(display_window_diagnostics PROPERTIES TIMEOUT 20)
# C2: the injected startup gate reads TDUR_BORDERLESS_CONSTRAINT ("0" and an
# invalid value). Full mod build only; the targets resolve at generation.
if(TARGET tdur_observation_proxy)
    foreach(case off invalid)
        add_test(NAME display_borderless_constraint_${case}
            COMMAND "${Python3_EXECUTABLE}" -I -B
                "${TDUR_DISPLAY_HOOK_ROOT}/mod/tests/run_display_startup_fixture.py"
                --launcher "$<TARGET_FILE:tdur_observe_launch>"
                --witness "$<TARGET_FILE:tdur_display_startup_witness>"
                --dll "$<TARGET_FILE:tdur_observation_proxy>"
                --work-root "${CMAKE_BINARY_DIR}/display-startup-fixtures"
                --constraint ${case})
        set_tests_properties(display_borderless_constraint_${case}
            PROPERTIES TIMEOUT 35 RUN_SERIAL TRUE)
    endforeach()
endif()
