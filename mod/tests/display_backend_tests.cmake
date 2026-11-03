# Optional integration, included by mod/CMakeLists.txt after its project setup.
get_filename_component(TDUR_DISPLAY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_library(tdur_display_backend STATIC "${TDUR_DISPLAY_ROOT}/mod/src/display_backend.cpp")
target_include_directories(tdur_display_backend PUBLIC "${TDUR_DISPLAY_ROOT}/mod/include")
target_compile_definitions(tdur_display_backend PUBLIC UNICODE _UNICODE NOMINMAX)
target_compile_options(tdur_display_backend PRIVATE /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_link_libraries(tdur_display_backend PUBLIC d3d12 dxgi PRIVATE d3dcompiler user32)

add_executable(tdur_display_backend_test
    "${TDUR_DISPLAY_ROOT}/mod/tests/display_backend_test.cpp"
    "${TDUR_DISPLAY_ROOT}/src/game/fixed_step.cpp")
target_include_directories(tdur_display_backend_test PRIVATE "${TDUR_DISPLAY_ROOT}/include")
target_compile_options(tdur_display_backend_test PRIVATE /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_link_libraries(tdur_display_backend_test PRIVATE tdur_display_backend user32)
add_test(NAME display_window_and_unfocused_host COMMAND tdur_display_backend_test --window-only)
add_test(NAME display_d3d9on12_interop COMMAND tdur_display_backend_test --interop-only)
set_tests_properties(display_window_and_unfocused_host display_d3d9on12_interop
    PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
