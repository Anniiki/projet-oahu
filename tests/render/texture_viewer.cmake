# Included after NativeDevice's targets, from tests/render/CMakeLists.txt.
get_filename_component(TDUR_TEXTURE_VIEWER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_library(tdur_d3d12_texture_viewer STATIC
    "${TDUR_TEXTURE_VIEWER_ROOT}/src/render/d3d12/texture_viewer.cpp")
target_link_libraries(tdur_d3d12_texture_viewer PUBLIC tdur_d3d12 PRIVATE d3dcompiler)
target_compile_options(tdur_d3d12_texture_viewer PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)

add_executable(tdur_d3d12_texture_viewer_test
    "${CMAKE_CURRENT_LIST_DIR}/texture_viewer_test.cpp")
target_link_libraries(tdur_d3d12_texture_viewer_test PRIVATE tdur_d3d12_texture_viewer)
target_compile_options(tdur_d3d12_texture_viewer_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_test(NAME d3d12_texture_hardware COMMAND tdur_d3d12_texture_viewer_test)
add_test(NAME d3d12_texture_warp COMMAND tdur_d3d12_texture_viewer_test --warp)
set_tests_properties(d3d12_texture_hardware d3d12_texture_warp PROPERTIES TIMEOUT 60)

# The standalone native-render test project has no root asset target.
if(NOT TARGET tdur_assets)
    add_library(tdur_texture_viewer_assets STATIC
        "${TDUR_TEXTURE_VIEWER_ROOT}/src/assets/bank_archive.cpp"
        "${TDUR_TEXTURE_VIEWER_ROOT}/src/assets/texture_2db.cpp")
    target_include_directories(tdur_texture_viewer_assets PUBLIC "${TDUR_TEXTURE_VIEWER_ROOT}/include")
    target_compile_features(tdur_texture_viewer_assets PUBLIC cxx_std_23)
    target_compile_options(tdur_texture_viewer_assets PRIVATE
        /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
    set(TDUR_TEXTURE_VIEWER_ASSETS tdur_texture_viewer_assets)
else()
    set(TDUR_TEXTURE_VIEWER_ASSETS tdur_assets)
endif()
add_executable(tdur_view_texture "${TDUR_TEXTURE_VIEWER_ROOT}/src/texture_viewer_main.cpp")
target_compile_definitions(tdur_view_texture PRIVATE UNICODE _UNICODE)
target_link_libraries(tdur_view_texture PRIVATE tdur_d3d12_texture_viewer
    ${TDUR_TEXTURE_VIEWER_ASSETS} user32)
target_compile_options(tdur_view_texture PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
