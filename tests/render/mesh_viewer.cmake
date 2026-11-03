# Include after NativeDevice targets in tests/render/CMakeLists.txt.
get_filename_component(TDUR_MESH_VIEWER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_library(tdur_d3d12_mesh_viewer STATIC
    "${TDUR_MESH_VIEWER_ROOT}/src/render/d3d12/mesh_viewer.cpp")
target_link_libraries(tdur_d3d12_mesh_viewer PUBLIC tdur_d3d12 PRIVATE d3dcompiler)
target_compile_options(tdur_d3d12_mesh_viewer PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)

add_executable(tdur_d3d12_mesh_viewer_test "${CMAKE_CURRENT_LIST_DIR}/mesh_viewer_test.cpp")
target_link_libraries(tdur_d3d12_mesh_viewer_test PRIVATE tdur_d3d12_mesh_viewer)
target_compile_options(tdur_d3d12_mesh_viewer_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_test(NAME d3d12_mesh_hardware COMMAND tdur_d3d12_mesh_viewer_test)
add_test(NAME d3d12_mesh_warp COMMAND tdur_d3d12_mesh_viewer_test --warp)
set_tests_properties(d3d12_mesh_hardware d3d12_mesh_warp PROPERTIES TIMEOUT 60)

if(NOT TARGET tdur_assets)
    add_library(tdur_mesh_viewer_assets STATIC
        "${TDUR_MESH_VIEWER_ROOT}/src/assets/bank_archive.cpp"
        "${TDUR_MESH_VIEWER_ROOT}/src/assets/geometry_3dg.cpp"
        "${TDUR_MESH_VIEWER_ROOT}/src/assets/material_2dm.cpp"
        "${TDUR_MESH_VIEWER_ROOT}/src/assets/material_textures.cpp"
        "${TDUR_MESH_VIEWER_ROOT}/src/assets/texture_2db.cpp"
        "${TDUR_MESH_VIEWER_ROOT}/src/assets/scene_3dd.cpp"
        "${TDUR_MESH_VIEWER_ROOT}/src/assets/scene_mesh.cpp")
    target_include_directories(tdur_mesh_viewer_assets PUBLIC "${TDUR_MESH_VIEWER_ROOT}/include")
    target_compile_features(tdur_mesh_viewer_assets PUBLIC cxx_std_23)
    target_compile_options(tdur_mesh_viewer_assets PRIVATE
        /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
    set(TDUR_MESH_VIEWER_ASSETS tdur_mesh_viewer_assets)
else()
    set(TDUR_MESH_VIEWER_ASSETS tdur_assets)
endif()
add_executable(tdur_view_mesh "${TDUR_MESH_VIEWER_ROOT}/src/mesh_viewer_main.cpp")
target_compile_definitions(tdur_view_mesh PRIVATE UNICODE _UNICODE)
target_link_libraries(tdur_view_mesh PRIVATE tdur_d3d12_mesh_viewer
    ${TDUR_MESH_VIEWER_ASSETS} user32)
target_compile_options(tdur_view_mesh PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)

add_executable(tdur_d3d12_scene_mesh_test "${CMAKE_CURRENT_LIST_DIR}/scene_mesh_render_test.cpp")
target_link_libraries(tdur_d3d12_scene_mesh_test PRIVATE
    tdur_d3d12_mesh_viewer ${TDUR_MESH_VIEWER_ASSETS})
target_compile_options(tdur_d3d12_scene_mesh_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_test(NAME d3d12_scene_assembly_hardware COMMAND tdur_d3d12_scene_mesh_test)
add_test(NAME d3d12_scene_assembly_warp COMMAND tdur_d3d12_scene_mesh_test --warp)
set_tests_properties(d3d12_scene_assembly_hardware d3d12_scene_assembly_warp PROPERTIES TIMEOUT 60)

add_executable(tdur_d3d12_mesh_texture_test "${CMAKE_CURRENT_LIST_DIR}/mesh_texture_render_test.cpp")
target_link_libraries(tdur_d3d12_mesh_texture_test PRIVATE tdur_d3d12_mesh_viewer)
target_compile_options(tdur_d3d12_mesh_texture_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_test(NAME d3d12_textured_mesh_hardware COMMAND tdur_d3d12_mesh_texture_test)
add_test(NAME d3d12_textured_mesh_warp COMMAND tdur_d3d12_mesh_texture_test --warp)
set_tests_properties(d3d12_textured_mesh_hardware d3d12_textured_mesh_warp PROPERTIES TIMEOUT 60)

add_executable(tdur_d3d12_mesh_sampler_cache_test
    "${CMAKE_CURRENT_LIST_DIR}/mesh_sampler_cache_test.cpp")
target_link_libraries(tdur_d3d12_mesh_sampler_cache_test PRIVATE
    tdur_d3d12_mesh_viewer user32)
target_compile_options(tdur_d3d12_mesh_sampler_cache_test PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_test(NAME d3d12_sampler_cache_hardware
    COMMAND tdur_d3d12_mesh_sampler_cache_test)
add_test(NAME d3d12_sampler_cache_warp
    COMMAND tdur_d3d12_mesh_sampler_cache_test --warp)
set_tests_properties(d3d12_sampler_cache_hardware d3d12_sampler_cache_warp
    PROPERTIES TIMEOUT 45)

add_executable(mesh_material_cli_fixture "${TDUR_MESH_VIEWER_ROOT}/tests/mesh_material_cli_fixture.cpp")
target_link_libraries(mesh_material_cli_fixture PRIVATE ${TDUR_MESH_VIEWER_ASSETS})
target_compile_options(mesh_material_cli_fixture PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
add_test(NAME source_owned_material_cli_bank COMMAND mesh_material_cli_fixture --self-test)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME material_cli_hardware COMMAND "${Python3_EXECUTABLE}" -I
        "${TDUR_MESH_VIEWER_ROOT}/tests/mesh_material_cli_test.py"
        --viewer "$<TARGET_FILE:tdur_view_mesh>"
        --fixture "$<TARGET_FILE:mesh_material_cli_fixture>"
        --work-parent "${CMAKE_BINARY_DIR}/mesh-material-cli/hardware")
    add_test(NAME material_cli_warp COMMAND "${Python3_EXECUTABLE}" -I
        "${TDUR_MESH_VIEWER_ROOT}/tests/mesh_material_cli_test.py"
        --viewer "$<TARGET_FILE:tdur_view_mesh>"
        --fixture "$<TARGET_FILE:mesh_material_cli_fixture>"
        --work-parent "${CMAKE_BINARY_DIR}/mesh-material-cli/warp" --warp)
    set_tests_properties(material_cli_hardware material_cli_warp PROPERTIES TIMEOUT 60)
endif()
