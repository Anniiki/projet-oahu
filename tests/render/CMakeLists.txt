cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(TDURecompilationD3D12DeviceTests LANGUAGES CXX)
    enable_testing()
endif()

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The native D3D12 device test requires Windows and MSVC")
endif()

get_filename_component(TDUR_RENDER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(tdur_d3d12 STATIC
    "${TDUR_RENDER_ROOT}/src/render/d3d12/native_device.cpp")
target_include_directories(tdur_d3d12 PUBLIC "${TDUR_RENDER_ROOT}/include")
target_compile_features(tdur_d3d12 PUBLIC cxx_std_23)
target_compile_definitions(tdur_d3d12 PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(tdur_d3d12 PRIVATE /W4 /WX /EHsc /std:c++latest)
target_link_libraries(tdur_d3d12 PUBLIC d3d12 dxgi)

add_executable(tdur_d3d12_device_test d3d12_device_test.cpp)
target_compile_options(tdur_d3d12_device_test PRIVATE /W4 /WX /EHsc /std:c++latest)
target_link_libraries(tdur_d3d12_device_test PRIVATE tdur_d3d12)
target_link_libraries(tdur_d3d12_device_test PRIVATE d3dcompiler user32)

add_test(NAME d3d12_device_hardware COMMAND tdur_d3d12_device_test)
add_test(NAME d3d12_device_warp COMMAND tdur_d3d12_device_test --warp)
set_tests_properties(d3d12_device_hardware d3d12_device_warp PROPERTIES TIMEOUT 60)

add_executable(tdur_native_submission_lifecycle_test native_submission_lifecycle_test.cpp)
target_compile_options(tdur_native_submission_lifecycle_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_link_libraries(tdur_native_submission_lifecycle_test PRIVATE tdur_d3d12)
add_test(NAME native_submission_lifecycle COMMAND tdur_native_submission_lifecycle_test)
set_tests_properties(native_submission_lifecycle PROPERTIES TIMEOUT 20)

include(texture_viewer.cmake)
include(mesh_viewer.cmake)

add_executable(tdur_d3d12_async_present_test d3d12_async_present_test.cpp)
target_compile_options(tdur_d3d12_async_present_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_link_libraries(tdur_d3d12_async_present_test PRIVATE
    tdur_d3d12_mesh_viewer tdur_d3d12_texture_viewer user32)
add_test(NAME d3d12_async_present_hardware COMMAND tdur_d3d12_async_present_test)
add_test(NAME d3d12_async_present_warp COMMAND tdur_d3d12_async_present_test --warp)
set_tests_properties(d3d12_async_present_hardware d3d12_async_present_warp
    PROPERTIES TIMEOUT 45)
