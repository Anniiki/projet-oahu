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
    "${TDUR_DISPLAY_HOOK_ROOT}/mod/src/foreground_cursor_core.cpp")
target_link_libraries(tdur_display_hooks PUBLIC tdur_display_backend PRIVATE tdur_detours comctl32)
target_compile_options(tdur_display_hooks PRIVATE /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_executable(tdur_display_hooks_test "${TDUR_DISPLAY_HOOK_ROOT}/mod/tests/display_hooks_test.cpp")
target_link_libraries(tdur_display_hooks_test PRIVATE tdur_display_hooks d3d9)
target_compile_options(tdur_display_hooks_test PRIVATE /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_test(NAME display_classic_hooks_reset_identity COMMAND tdur_display_hooks_test)
set_tests_properties(display_classic_hooks_reset_identity PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
