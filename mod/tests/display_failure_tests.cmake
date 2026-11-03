# Optional, source-owned GPU fixtures. Include after project()/enable_testing().
# Standalone builds may supply the existing matching-architecture Detours .lib.
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Display failure witnesses require Windows and MSVC")
endif()
get_filename_component(TDUR_DISPLAY_FAILURE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT TARGET tdur_display_backend)
    add_library(tdur_display_backend STATIC
        "${TDUR_DISPLAY_FAILURE_ROOT}/mod/src/display_backend.cpp")
    target_include_directories(tdur_display_backend PUBLIC
        "${TDUR_DISPLAY_FAILURE_ROOT}/mod/include")
    target_compile_definitions(tdur_display_backend PUBLIC
        UNICODE _UNICODE NOMINMAX)
    target_compile_options(tdur_display_backend PRIVATE
        /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
    target_link_libraries(tdur_display_backend PUBLIC d3d12 dxgi
        PRIVATE d3dcompiler user32)
endif()
if(NOT TARGET tdur_detours)
    if(NOT TDUR_DETOURS_LIBRARY)
        message(FATAL_ERROR
            "Provide TDUR_DETOURS_LIBRARY for the standalone failure witness")
    endif()
    add_library(tdur_detours STATIC IMPORTED)
    set_target_properties(tdur_detours PROPERTIES
        IMPORTED_LOCATION "${TDUR_DETOURS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES
            "${TDUR_DISPLAY_FAILURE_ROOT}/mod/src/vendor/detours")
endif()
add_executable(tdur_display_failure_test
    "${TDUR_DISPLAY_FAILURE_ROOT}/mod/tests/display_failure_test.cpp")
target_compile_options(tdur_display_failure_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_link_libraries(tdur_display_failure_test PRIVATE
    tdur_display_backend tdur_detours user32)
add_test(NAME display_pending_gpu_signal_failure
    COMMAND tdur_display_failure_test --signal-failure)
add_test(NAME display_private_device_removal
    COMMAND tdur_display_failure_test --remove-device)
set_tests_properties(display_pending_gpu_signal_failure
    display_private_device_removal PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
set_tests_properties(display_private_device_removal PROPERTIES
    SKIP_RETURN_CODE 77)
