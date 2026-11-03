cmake_minimum_required(VERSION 3.25)
project(TDURecompilation VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TDUR_BUILD_SDL_GAMEPADS "Build the SDL3 gamepad transport" ON)
if(TDUR_BUILD_SDL_GAMEPADS)
    include(cmake/sdl3.cmake)
endif()

function(tdur_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /std:c++latest /W4 /WX /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_library(tdur_mtps_flags STATIC src/game/mtps_flags.cpp)
target_include_directories(tdur_mtps_flags PUBLIC include)
tdur_compile_options(tdur_mtps_flags)

add_library(tdur_profile_menu_core STATIC src/game/profile_menu_core.cpp)
target_include_directories(tdur_profile_menu_core PUBLIC include)
tdur_compile_options(tdur_profile_menu_core)

add_library(tdur_profile_menu_dispatch STATIC src/game/profile_menu_dispatch.cpp)
target_link_libraries(tdur_profile_menu_dispatch PUBLIC tdur_profile_menu_core)
tdur_compile_options(tdur_profile_menu_dispatch)

add_library(tdur_profile_menu_result STATIC src/game/profile_menu_result.cpp)
target_link_libraries(tdur_profile_menu_result PUBLIC tdur_profile_menu_dispatch)
tdur_compile_options(tdur_profile_menu_result)

add_library(tdur_game_mode_lifecycle STATIC src/game/game_mode_lifecycle.cpp)
target_include_directories(tdur_game_mode_lifecycle PUBLIC include)
tdur_compile_options(tdur_game_mode_lifecycle)

add_library(tdur_startup_services STATIC src/game/startup_services.cpp)
target_link_libraries(tdur_startup_services PUBLIC tdur_game_mode_lifecycle)
tdur_compile_options(tdur_startup_services)

add_library(tdur_startup_9b1df0_tail STATIC src/game/startup_9b1df0_tail.cpp)
target_link_libraries(tdur_startup_9b1df0_tail PUBLIC tdur_startup_services)
tdur_compile_options(tdur_startup_9b1df0_tail)

add_library(tdur_startup_9b1df0 STATIC src/game/startup_9b1df0.cpp)
target_link_libraries(tdur_startup_9b1df0 PUBLIC tdur_startup_9b1df0_tail)
tdur_compile_options(tdur_startup_9b1df0)

add_library(tdur_config_drivers STATIC
    src/game/config_file_loader.cpp
    src/game/config_external_loader.cpp
    src/game/config_section_parser.cpp)
target_include_directories(tdur_config_drivers PUBLIC include)
tdur_compile_options(tdur_config_drivers)

add_library(tdur_config_registry_9896c0 STATIC
    src/game/config_registry_9896c0.cpp)
target_link_libraries(tdur_config_registry_9896c0 PUBLIC tdur_startup_services)
tdur_compile_options(tdur_config_registry_9896c0)

add_library(tdur_config_startup STATIC src/game/config_driver_98e960.cpp)
target_link_libraries(tdur_config_startup PUBLIC
    tdur_config_drivers tdur_config_registry_9896c0)
tdur_compile_options(tdur_config_startup)

add_library(tdur_config_auxiliary STATIC
    src/game/config_aux_986b60.cpp
    src/game/config_aux_988fc0.cpp
    src/game/config_aux_drivers.cpp)
target_link_libraries(tdur_config_auxiliary PUBLIC tdur_config_startup)
tdur_compile_options(tdur_config_auxiliary)

add_library(tdur_startup_lifecycle_binding STATIC
    src/game/startup_lifecycle_binding.cpp)
target_link_libraries(tdur_startup_lifecycle_binding PUBLIC tdur_config_startup)
tdur_compile_options(tdur_startup_lifecycle_binding)

add_library(tdur_startup_completion STATIC src/game/startup_completion.cpp)
target_link_libraries(tdur_startup_completion PUBLIC
    tdur_startup_lifecycle_binding)
tdur_compile_options(tdur_startup_completion)

add_library(tdur_startup_config_drivers STATIC
    src/game/startup_config_drivers.cpp)
target_link_libraries(tdur_startup_config_drivers PUBLIC
    tdur_startup_completion tdur_config_auxiliary)
tdur_compile_options(tdur_startup_config_drivers)

add_library(tdur_startup_file_sources STATIC
    src/game/startup_file_sources.cpp)
target_link_libraries(tdur_startup_file_sources PUBLIC
    tdur_startup_config_drivers)
tdur_compile_options(tdur_startup_file_sources)

if(TDUR_BUILD_SDL_GAMEPADS)
    add_library(tdur_controller_bindings STATIC
        src/input/controller_bindings.cpp)
    target_include_directories(tdur_controller_bindings PUBLIC include)
    target_link_libraries(tdur_controller_bindings PUBLIC SDL3::Headers)
    tdur_compile_options(tdur_controller_bindings)

    add_library(tdur_sdl_gamepads STATIC src/input/sdl_gamepads.cpp)
    target_include_directories(tdur_sdl_gamepads PUBLIC include)
    target_link_libraries(tdur_sdl_gamepads PUBLIC SDL3::SDL3-static)
    tdur_compile_options(tdur_sdl_gamepads)
endif()

add_library(tdur_xtea_words STATIC src/crypto/xtea_words.cpp)
target_include_directories(tdur_xtea_words PUBLIC include)
tdur_compile_options(tdur_xtea_words)

add_library(tdur_assets STATIC
    src/assets/bank_archive.cpp
    src/assets/database.cpp
    src/assets/geometry_3dg.cpp
    src/assets/material_2dm.cpp
    src/assets/material_textures.cpp
    src/assets/resource_hash.cpp
    src/assets/resource_name_hash.cpp
    src/assets/scene_3dd.cpp
    src/assets/scene_mesh.cpp
    src/assets/texture_2db.cpp
    src/assets/vmf_movie.cpp)
target_include_directories(tdur_assets PUBLIC include)
target_link_libraries(tdur_assets PUBLIC tdur_xtea_words)
tdur_compile_options(tdur_assets)

add_library(tdur_vmf_image_bindings STATIC src/assets/vmf_image_bindings.cpp)
target_include_directories(tdur_vmf_image_bindings PUBLIC include)
target_link_libraries(tdur_vmf_image_bindings PUBLIC tdur_assets)
tdur_compile_options(tdur_vmf_image_bindings)

add_library(tdur_vmf_display_data STATIC src/assets/vmf_display_data.cpp)
target_include_directories(tdur_vmf_display_data PUBLIC include)
target_link_libraries(tdur_vmf_display_data PUBLIC tdur_assets)
tdur_compile_options(tdur_vmf_display_data)

add_library(tdur_vmf_program STATIC src/ui/vmf_program.cpp)
target_include_directories(tdur_vmf_program PUBLIC include)
target_link_libraries(tdur_vmf_program PUBLIC
    tdur_vmf_display_data tdur_vmf_text_data)
tdur_compile_options(tdur_vmf_program)

add_library(tdur_vmf_text_data STATIC src/assets/vmf_text_data.cpp)
target_include_directories(tdur_vmf_text_data PUBLIC include)
target_link_libraries(tdur_vmf_text_data PUBLIC tdur_assets)
tdur_compile_options(tdur_vmf_text_data)

add_library(tdur_native_input_state STATIC src/input/native_input_state.cpp)
target_include_directories(tdur_native_input_state PUBLIC include)
tdur_compile_options(tdur_native_input_state)

add_library(tdur_native_input_selection STATIC src/input/native_input_selection.cpp)
target_link_libraries(tdur_native_input_selection PUBLIC tdur_native_input_state)
tdur_compile_options(tdur_native_input_selection)

add_library(tdur_startup_driver_980af0 STATIC src/game/startup_driver_980af0.cpp)
target_link_libraries(tdur_startup_driver_980af0 PUBLIC
    tdur_startup_services tdur_native_input_selection)
tdur_compile_options(tdur_startup_driver_980af0)

add_library(tdur_vmf_number STATIC
    src/ui/vmf_number.cpp
    src/ui/vmf_number_format.cpp)
target_include_directories(tdur_vmf_number PUBLIC include)
tdur_compile_options(tdur_vmf_number)
if(MSVC)
    target_compile_options(tdur_vmf_number PRIVATE /fp:strict)
else()
    target_compile_options(tdur_vmf_number PRIVATE
        -ffp-contract=off -frounding-math)
endif()

add_library(tdur_vmf_variable_hash STATIC src/ui/vmf_variable_hash.cpp)
target_include_directories(tdur_vmf_variable_hash PUBLIC include)
tdur_compile_options(tdur_vmf_variable_hash)

add_library(tdur_vmf_variable_reference STATIC
    src/ui/vmf_variable_reference.cpp)
target_include_directories(tdur_vmf_variable_reference PUBLIC include)
tdur_compile_options(tdur_vmf_variable_reference)

add_library(tdur_vmf_stack STATIC src/ui/vmf_stack.cpp)
target_include_directories(tdur_vmf_stack PUBLIC include)
target_link_libraries(tdur_vmf_stack PUBLIC tdur_vmf_number)
tdur_compile_options(tdur_vmf_stack)
if(MSVC)
    target_compile_options(tdur_vmf_stack PRIVATE /fp:strict)
else()
    target_compile_options(tdur_vmf_stack PRIVATE
        -ffp-contract=off -frounding-math)
endif()

add_library(tdur_vmf_variables STATIC src/ui/vmf_variables.cpp)
target_include_directories(tdur_vmf_variables PUBLIC include)
target_link_libraries(tdur_vmf_variables PUBLIC
    tdur_vmf_variable_hash tdur_vmf_stack)
tdur_compile_options(tdur_vmf_variables)
if(MSVC)
    target_compile_options(tdur_vmf_variables PRIVATE /fp:strict)
endif()

add_library(tdur_vmf_transform STATIC
    src/ui/vmf_transform.cpp
    src/ui/vmf_property_math.cpp
    src/ui/vmf_transform_ops.cpp)
target_include_directories(tdur_vmf_transform PUBLIC include)
tdur_compile_options(tdur_vmf_transform)
# Keep the scalar operation calls opaque so unmasked SSE traps retain order.
set_target_properties(tdur_vmf_transform PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION FALSE
    INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE)
if(MSVC)
    target_compile_options(tdur_vmf_transform PRIVATE /fp:strict)
else()
    target_compile_options(tdur_vmf_transform PRIVATE
        -ffp-contract=off -frounding-math)
endif()

add_library(tdur_vmf_execution STATIC src/ui/vmf_execution.cpp)
target_include_directories(tdur_vmf_execution PUBLIC include)
target_link_libraries(tdur_vmf_execution PUBLIC
    tdur_vmf_stack tdur_assets tdur_vmf_display_runtime
    tdur_vmf_variables tdur_vmf_variable_reference)
tdur_compile_options(tdur_vmf_execution)
if(MSVC)
    target_compile_options(tdur_vmf_execution PRIVATE /fp:strict)
else()
    target_compile_options(tdur_vmf_execution PRIVATE
        -ffp-contract=off -frounding-math)
endif()

add_library(tdur_vmf_display_runtime STATIC src/ui/vmf_display_runtime.cpp)
target_include_directories(tdur_vmf_display_runtime PUBLIC include)
target_link_libraries(tdur_vmf_display_runtime PUBLIC
    tdur_vmf_program tdur_vmf_transform tdur_vmf_variables)
target_link_libraries(tdur_vmf_display_runtime PRIVATE tdur_vmf_variable_reference)
tdur_compile_options(tdur_vmf_display_runtime)
if(MSVC)
    target_compile_options(tdur_vmf_display_runtime PRIVATE /fp:strict)
endif()

add_library(tdur_vmf_frame_player STATIC src/ui/vmf_frame_player.cpp)
target_include_directories(tdur_vmf_frame_player PUBLIC include)
target_link_libraries(tdur_vmf_frame_player PUBLIC tdur_vmf_execution)
tdur_compile_options(tdur_vmf_frame_player)

add_library(tdur_vmf_host_variables STATIC src/ui/vmf_host_variables.cpp)

target_include_directories(tdur_vmf_host_variables PUBLIC include)

target_link_libraries(tdur_vmf_host_variables PUBLIC
    tdur_vmf_display_runtime tdur_vmf_variable_reference)

tdur_compile_options(tdur_vmf_host_variables)

add_library(tdur_vmf_catalog STATIC src/ui/vmf_catalog.cpp)
target_include_directories(tdur_vmf_catalog PUBLIC include)
target_link_libraries(tdur_vmf_catalog PUBLIC tdur_assets tdur_vmf_stack)
tdur_compile_options(tdur_vmf_catalog)

add_library(tdur_vmf_menu_registry STATIC src/ui/vmf_menu_movie.cpp)
target_include_directories(tdur_vmf_menu_registry PUBLIC include)
tdur_compile_options(tdur_vmf_menu_registry)

add_library(tdur_vmf_menu_data STATIC src/ui/vmf_menu_data.cpp
    src/ui/vmf_menu_finalizers.cpp src/ui/vmf_menu_display_selection.cpp)
target_include_directories(tdur_vmf_menu_data PUBLIC include)
target_link_libraries(tdur_vmf_menu_data PUBLIC tdur_vmf_host_variables tdur_assets
    tdur_vmf_menu_registry)
tdur_compile_options(tdur_vmf_menu_data)

add_library(tdur_vmf_help_services STATIC src/ui/vmf_help_selection.cpp
    src/ui/vmf_named_menu.cpp src/ui/vmf_tutorial_data.cpp
    src/ui/vmf_help_message.cpp src/ui/vmf_tutorial_service.cpp
    src/ui/vmf_notification_state.cpp src/ui/vmf_notification_child.cpp
    src/ui/vmf_notification_text.cpp src/ui/vmf_notification_identity.cpp
    src/ui/vmf_notification_record.cpp src/ui/vmf_notification_queue.cpp
    src/ui/vmf_notification_reset.cpp src/ui/vmf_notification_rings.cpp
    src/ui/vmf_notification_flag.cpp src/ui/vmf_notification_format.cpp
    src/ui/vmf_notification_scan.cpp src/ui/vmf_notification_lookup.cpp
    src/ui/vmf_notification_prefix.cpp src/ui/vmf_notification_completion.cpp
    src/ui/vmf_notification_replace.cpp
    src/ui/vmf_notification_money.cpp
    src/ui/vmf_notification_named.cpp
    src/ui/vmf_notification_wrapper.cpp
    src/ui/vmf_notification_display.cpp
    src/ui/vmf_notification_assignment.cpp
    src/ui/vmf_notification_catalog_display.cpp
    src/ui/vmf_notification_deferred_queue.cpp)
target_sources(tdur_vmf_help_services PRIVATE src/ui/vmf_notification_dispatch.cpp)
target_sources(tdur_vmf_help_services PRIVATE src/ui/vmf_notification_toggle.cpp)
target_sources(tdur_vmf_help_services PRIVATE src/ui/vmf_notification_parent.cpp)
target_sources(tdur_vmf_help_services PRIVATE
    src/ui/vmf_notification_effect_services.cpp
    src/ui/vmf_notification_effect_db30.cpp)
target_sources(tdur_vmf_help_services PRIVATE src/ui/vmf_notification_database.cpp)
target_sources(tdur_vmf_help_services PRIVATE src/ui/vmf_notification_effect_bf80.cpp)
target_sources(tdur_vmf_help_services PRIVATE src/ui/vmf_notification_effect_pool.cpp)
target_include_directories(tdur_vmf_help_services PUBLIC include)
target_link_libraries(tdur_vmf_help_services PUBLIC tdur_vmf_menu_data tdur_xtea_words tdur_vmf_catalog)
tdur_compile_options(tdur_vmf_help_services)

add_library(tdur_vmf_host_commands STATIC src/ui/vmf_fscommand.cpp)
target_sources(tdur_vmf_host_commands PRIVATE src/ui/vmf_fscommand_router.cpp)
target_sources(tdur_vmf_host_commands PRIVATE src/ui/vmf_fscommand_global.cpp)

target_sources(tdur_vmf_host_commands PRIVATE src/ui/vmf_fscommand_collection.cpp)
target_include_directories(tdur_vmf_host_commands PUBLIC include)
target_link_libraries(tdur_vmf_host_commands PUBLIC tdur_vmf_menu_registry)
tdur_compile_options(tdur_vmf_host_commands)

if(WIN32)
    add_executable(tdur_inspect_vmf_playback src/tools/inspect_vmf_playback.cpp)
    target_link_libraries(tdur_inspect_vmf_playback PRIVATE tdur_vmf_frame_player)
    tdur_compile_options(tdur_inspect_vmf_playback)
endif()

add_library(tdur_adaptive_automatic STATIC src/physics/adaptive_automatic.cpp)
target_include_directories(tdur_adaptive_automatic PUBLIC include)
tdur_compile_options(tdur_adaptive_automatic)

add_library(tdur_fixed_step STATIC src/game/fixed_step.cpp)
target_include_directories(tdur_fixed_step PUBLIC include)
tdur_compile_options(tdur_fixed_step)

add_library(tdur_legacy_state_machine STATIC src/game/legacy_state_machine.cpp)
target_include_directories(tdur_legacy_state_machine PUBLIC include)
tdur_compile_options(tdur_legacy_state_machine)

add_library(tdur_legacy_population STATIC src/game/legacy_population.cpp)
target_include_directories(tdur_legacy_population PUBLIC include)
tdur_compile_options(tdur_legacy_population)
if(MSVC)
    target_compile_options(tdur_legacy_population PRIVATE /fp:precise)
endif()

add_library(tdur_legacy_physics_timer STATIC src/physics/legacy_timer.cpp)
target_include_directories(tdur_legacy_physics_timer PUBLIC include)
tdur_compile_options(tdur_legacy_physics_timer)

add_library(tdur_fixed_physics_timer STATIC src/physics/fixed_timer.cpp)
target_include_directories(tdur_fixed_physics_timer PUBLIC include)
tdur_compile_options(tdur_fixed_physics_timer)

add_library(tdur_physics_binding STATIC src/diagnostics/physics_binding.cpp)
target_include_directories(tdur_physics_binding PUBLIC include)
tdur_compile_options(tdur_physics_binding)

add_library(tdur_ui_service_binding STATIC src/diagnostics/ui_service_binding.cpp)
target_include_directories(tdur_ui_service_binding PUBLIC include)
tdur_compile_options(tdur_ui_service_binding)

add_executable(tdur_inspect_bank src/bank_inspector_main.cpp)
target_link_libraries(tdur_inspect_bank PRIVATE tdur_assets)
tdur_compile_options(tdur_inspect_bank)

if(WIN32)
    add_library(tdur_fixed_physics_worker STATIC src/physics/fixed_worker.cpp)
    target_include_directories(tdur_fixed_physics_worker PUBLIC include)
    target_compile_definitions(tdur_fixed_physics_worker PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(tdur_fixed_physics_worker PUBLIC tdur_fixed_physics_timer)
    tdur_compile_options(tdur_fixed_physics_worker)

    add_library(tdur_installation STATIC src/game/installation.cpp)
    target_include_directories(tdur_installation PUBLIC include)
    target_compile_definitions(tdur_installation PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(tdur_installation PRIVATE bcrypt)
    tdur_compile_options(tdur_installation)

    add_executable(tdur_inspect_game src/inspect_game_main.cpp)
    target_link_libraries(tdur_inspect_game PRIVATE tdur_installation)
    tdur_compile_options(tdur_inspect_game)

    add_executable(tdur_probe_ui_services src/runtime_ui_service_binding_probe_main.cpp)
    target_link_libraries(tdur_probe_ui_services PRIVATE tdur_installation tdur_ui_service_binding)
    target_compile_definitions(tdur_probe_ui_services PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    tdur_compile_options(tdur_probe_ui_services)

    add_executable(tdur_probe_timing src/runtime_timing_probe_main.cpp)
    target_link_libraries(tdur_probe_timing PRIVATE tdur_installation)
    target_compile_definitions(tdur_probe_timing PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    tdur_compile_options(tdur_probe_timing)

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(tdur_probe_mode src/runtime_mode_probe_main.cpp)
        target_link_libraries(tdur_probe_mode PRIVATE bcrypt)
        target_compile_definitions(tdur_probe_mode PRIVATE
            WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
        tdur_compile_options(tdur_probe_mode)
    endif()

    add_executable(tdur_prepare_video_config src/prepare_video_config_main.cpp)
    target_link_libraries(tdur_prepare_video_config PRIVATE tdur_installation d3d9 user32)
    target_compile_definitions(tdur_prepare_video_config PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    tdur_compile_options(tdur_prepare_video_config)

    add_library(tdur_database_resources STATIC src/game/database_resource_key.cpp
        src/game/menu_language_table.cpp src/game/tutorial_metadata.cpp)
    target_include_directories(tdur_database_resources PUBLIC include)
    target_link_libraries(tdur_database_resources PUBLIC tdur_installation tdur_assets)
    tdur_compile_options(tdur_database_resources)

    add_executable(tdur_inspect_database src/database_inspector_main.cpp)
    target_link_libraries(tdur_inspect_database PRIVATE tdur_database_resources)
    tdur_compile_options(tdur_inspect_database)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(profile_menu_core_test tests/profile_menu_core_test.cpp)
    target_link_libraries(profile_menu_core_test PRIVATE tdur_profile_menu_core)
    tdur_compile_options(profile_menu_core_test)
    add_test(NAME profile_menu_core COMMAND profile_menu_core_test)
    set_tests_properties(profile_menu_core PROPERTIES TIMEOUT 45)

    add_executable(profile_menu_dispatch_test tests/profile_menu_dispatch_test.cpp)
    target_link_libraries(profile_menu_dispatch_test PRIVATE tdur_profile_menu_dispatch)
    tdur_compile_options(profile_menu_dispatch_test)
    add_test(NAME profile_menu_dispatch COMMAND profile_menu_dispatch_test)
    set_tests_properties(profile_menu_dispatch PROPERTIES TIMEOUT 45)

    add_executable(profile_menu_result_test tests/profile_menu_result_test.cpp)
    target_link_libraries(profile_menu_result_test PRIVATE tdur_profile_menu_result)
    tdur_compile_options(profile_menu_result_test)
    add_test(NAME profile_menu_result COMMAND profile_menu_result_test)
    set_tests_properties(profile_menu_result PROPERTIES TIMEOUT 45)

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(game_mode_lifecycle_test tests/game_mode_lifecycle_test.cpp)
        target_link_libraries(game_mode_lifecycle_test PRIVATE tdur_game_mode_lifecycle)
        tdur_compile_options(game_mode_lifecycle_test)
        add_test(NAME game_mode_lifecycle COMMAND game_mode_lifecycle_test
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/game_mode_phases.tsv")
        set_tests_properties(game_mode_lifecycle PROPERTIES TIMEOUT 20)

        add_executable(startup_services_test tests/startup_services_test.cpp)
        target_link_libraries(startup_services_test PRIVATE tdur_startup_services)
        tdur_compile_options(startup_services_test)
        add_test(NAME startup_services COMMAND startup_services_test)
        set_tests_properties(startup_services PROPERTIES TIMEOUT 20)

        add_executable(startup_lifecycle_binding_test
            tests/startup_lifecycle_binding_test.cpp)
        target_link_libraries(startup_lifecycle_binding_test PRIVATE
            tdur_startup_completion)
        tdur_compile_options(startup_lifecycle_binding_test)
        add_test(NAME startup_lifecycle_binding COMMAND startup_lifecycle_binding_test)
        set_tests_properties(startup_lifecycle_binding PROPERTIES TIMEOUT 20)

        add_executable(startup_completion_test tests/startup_completion_test.cpp)
        target_link_libraries(startup_completion_test PRIVATE tdur_startup_completion)
        tdur_compile_options(startup_completion_test)
        add_test(NAME startup_completion COMMAND startup_completion_test
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/startup_completion_services.txt")
        set_tests_properties(startup_completion PROPERTIES TIMEOUT 20)

        add_executable(startup_config_drivers_test
            tests/startup_config_drivers_test.cpp)
        target_link_libraries(startup_config_drivers_test PRIVATE
            tdur_startup_config_drivers)
        tdur_compile_options(startup_config_drivers_test)
        add_test(NAME startup_config_drivers COMMAND startup_config_drivers_test
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/startup_config_stages.txt")
        set_tests_properties(startup_config_drivers PROPERTIES TIMEOUT 20)

        add_executable(startup_file_sources_test
            tests/startup_file_sources_test.cpp)
        target_link_libraries(startup_file_sources_test PRIVATE
            tdur_startup_file_sources)
        tdur_compile_options(startup_file_sources_test)
        add_test(NAME startup_file_sources COMMAND startup_file_sources_test)
        set_tests_properties(startup_file_sources PROPERTIES TIMEOUT 20)
        target_link_libraries(startup_config_drivers_test PRIVATE
            tdur_startup_file_sources)

        add_executable(startup_9b1df0_tail_test tests/startup_9b1df0_tail_test.cpp)
        target_link_libraries(startup_9b1df0_tail_test PRIVATE tdur_startup_9b1df0_tail)
        tdur_compile_options(startup_9b1df0_tail_test)
        add_test(NAME startup_9b1df0_tail COMMAND startup_9b1df0_tail_test)
        set_tests_properties(startup_9b1df0_tail PROPERTIES TIMEOUT 20)

        add_executable(startup_9b1df0_test tests/startup_9b1df0_test.cpp)
        target_link_libraries(startup_9b1df0_test PRIVATE tdur_startup_9b1df0)
        tdur_compile_options(startup_9b1df0_test)
        add_test(NAME startup_9b1df0 COMMAND startup_9b1df0_test)
        set_tests_properties(startup_9b1df0 PROPERTIES TIMEOUT 20)

        add_executable(startup_driver_980af0_test tests/startup_driver_980af0_test.cpp)
        target_link_libraries(startup_driver_980af0_test PRIVATE tdur_startup_driver_980af0)
        tdur_compile_options(startup_driver_980af0_test)
        add_test(NAME startup_driver_980af0 COMMAND startup_driver_980af0_test)
        set_tests_properties(startup_driver_980af0 PROPERTIES TIMEOUT 20)

        add_executable(config_drivers_test tests/config_drivers_test.cpp)
        target_link_libraries(config_drivers_test PRIVATE tdur_config_drivers)
        tdur_compile_options(config_drivers_test)
        add_test(NAME config_drivers COMMAND config_drivers_test)
        set_tests_properties(config_drivers PROPERTIES TIMEOUT 20)

        add_executable(config_external_loader_test tests/config_external_loader_test.cpp)
        target_link_libraries(config_external_loader_test PRIVATE tdur_config_drivers)
        tdur_compile_options(config_external_loader_test)
        add_test(NAME config_external_loader COMMAND config_external_loader_test)
        set_tests_properties(config_external_loader PROPERTIES TIMEOUT 20)

        add_executable(config_registry_9896c0_test tests/config_registry_9896c0_test.cpp)
        target_link_libraries(config_registry_9896c0_test PRIVATE tdur_config_registry_9896c0)
        tdur_compile_options(config_registry_9896c0_test)
        add_test(NAME config_registry_9896c0 COMMAND config_registry_9896c0_test)
        set_tests_properties(config_registry_9896c0 PROPERTIES TIMEOUT 20)

        add_executable(config_driver_98e960_test tests/config_driver_98e960_test.cpp)
        target_link_libraries(config_driver_98e960_test PRIVATE tdur_config_startup)
        tdur_compile_options(config_driver_98e960_test)
        add_test(NAME config_driver_98e960 COMMAND config_driver_98e960_test)
        set_tests_properties(config_driver_98e960 PROPERTIES TIMEOUT 20)

        add_executable(config_aux_core_test tests/config_aux_core_test.cpp)
        target_link_libraries(config_aux_core_test PRIVATE tdur_config_auxiliary)
        tdur_compile_options(config_aux_core_test)
        add_test(NAME config_aux_core COMMAND config_aux_core_test)
        set_tests_properties(config_aux_core PROPERTIES TIMEOUT 20)

        add_executable(config_aux_drivers_test tests/config_aux_drivers_test.cpp)
        target_link_libraries(config_aux_drivers_test PRIVATE tdur_config_auxiliary)
        tdur_compile_options(config_aux_drivers_test)
        add_test(NAME config_aux_drivers COMMAND config_aux_drivers_test)
        set_tests_properties(config_aux_drivers PROPERTIES TIMEOUT 20)
    endif()

    add_executable(runtime_mode_probe_core_test tests/runtime_mode_probe_core_test.cpp)
    target_include_directories(runtime_mode_probe_core_test PRIVATE src)
    tdur_compile_options(runtime_mode_probe_core_test)
    add_test(NAME runtime_mode_probe_core COMMAND runtime_mode_probe_core_test)
    set_tests_properties(runtime_mode_probe_core PROPERTIES TIMEOUT 10)

    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(runtime_mode_probe_win32_test tests/runtime_mode_probe_win32_test.cpp)
        target_link_libraries(runtime_mode_probe_win32_test PRIVATE bcrypt)
        target_compile_definitions(runtime_mode_probe_win32_test PRIVATE
            WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
        tdur_compile_options(runtime_mode_probe_win32_test)
        add_test(NAME runtime_mode_probe_win32 COMMAND runtime_mode_probe_win32_test)
        set_tests_properties(runtime_mode_probe_win32 PROPERTIES TIMEOUT 10)
    endif()

    add_executable(mtps_flags_test tests/mtps_flags_test.cpp)
    target_link_libraries(mtps_flags_test PRIVATE tdur_mtps_flags)
    tdur_compile_options(mtps_flags_test)
    add_test(NAME mtps_flags COMMAND mtps_flags_test)
    set_tests_properties(mtps_flags PROPERTIES TIMEOUT 45)
    if(TDUR_BUILD_SDL_GAMEPADS)
        add_executable(controller_bindings_test
            tests/controller_bindings_test.cpp)
        target_link_libraries(controller_bindings_test PRIVATE
            tdur_controller_bindings)
        tdur_compile_options(controller_bindings_test)
        add_test(NAME controller_context_bindings
            COMMAND controller_bindings_test)

        add_executable(sdl_gamepads_test tests/sdl_gamepads_test.cpp)
        target_link_libraries(sdl_gamepads_test PRIVATE tdur_sdl_gamepads)
        tdur_compile_options(sdl_gamepads_test)
        add_test(NAME sdl_gamepads_virtual COMMAND sdl_gamepads_test)
        set_tests_properties(sdl_gamepads_virtual PROPERTIES TIMEOUT 45)
    endif()

    add_executable(bank_archive_test tests/bank_archive_test.cpp)
    target_link_libraries(bank_archive_test PRIVATE tdur_assets)
    tdur_compile_options(bank_archive_test)
    add_test(NAME pc_knab_archives COMMAND bank_archive_test)

    add_executable(texture_2db_test tests/texture_2db_test.cpp)
    target_link_libraries(texture_2db_test PRIVATE tdur_assets)
    tdur_compile_options(texture_2db_test)
    add_test(NAME pc_2db_textures COMMAND texture_2db_test)

    add_executable(geometry_3dg_test tests/geometry_3dg_test.cpp)
    target_link_libraries(geometry_3dg_test PRIVATE tdur_assets)
    tdur_compile_options(geometry_3dg_test)
    add_test(NAME pc_3dg_geometry COMMAND geometry_3dg_test)

    add_executable(scene_3dd_test tests/scene_3dd_test.cpp)
    target_link_libraries(scene_3dd_test PRIVATE tdur_assets)
    tdur_compile_options(scene_3dd_test)
    add_test(NAME pc_3dd_scene COMMAND scene_3dd_test)

    add_executable(material_2dm_test tests/material_2dm_test.cpp)
    target_link_libraries(material_2dm_test PRIVATE tdur_assets)
    tdur_compile_options(material_2dm_test)
    add_test(NAME pc_2dm_material_bindings COMMAND material_2dm_test)

    add_executable(material_textures_test tests/material_textures_test.cpp)
    target_link_libraries(material_textures_test PRIVATE tdur_assets)
    tdur_compile_options(material_textures_test)
    add_test(NAME pc_material_texture_bindings COMMAND material_textures_test)

    add_executable(resource_hash_test tests/resource_hash_test.cpp)
    target_link_libraries(resource_hash_test PRIVATE tdur_assets)
    tdur_compile_options(resource_hash_test)
    add_test(NAME native_resource_key_hash COMMAND resource_hash_test)
    add_executable(resource_name_hash_test tests/resource_name_hash_test.cpp)
    target_link_libraries(resource_name_hash_test PRIVATE tdur_assets)
    tdur_compile_options(resource_name_hash_test)
    add_test(NAME native_resource_name_hash COMMAND resource_name_hash_test)

    add_executable(scene_mesh_test tests/scene_mesh_test.cpp)
    target_link_libraries(scene_mesh_test PRIVATE tdur_assets)
    tdur_compile_options(scene_mesh_test)
    add_test(NAME pc_scene_mesh_assembly COMMAND scene_mesh_test)

    add_executable(database_asset_test tests/database_test.cpp)
    target_link_libraries(database_asset_test PRIVATE tdur_assets)
    tdur_compile_options(database_asset_test)
    add_test(NAME pc_database_assets COMMAND database_asset_test)

    add_executable(vmf_movie_test tests/vmf_movie_test.cpp)
    target_link_libraries(vmf_movie_test PRIVATE tdur_assets)
    tdur_compile_options(vmf_movie_test)
    add_test(NAME vmf_movie_reader COMMAND vmf_movie_test)

    add_executable(vmf_text_data_test tests/vmf_text_data_test.cpp)
    target_link_libraries(vmf_text_data_test PRIVATE tdur_vmf_program)
    tdur_compile_options(vmf_text_data_test)
    add_test(NAME vmf_text_data COMMAND vmf_text_data_test)
    set_tests_properties(vmf_text_data PROPERTIES TIMEOUT 45)

    add_executable(native_input_state_test tests/native_input_state_test.cpp)
    target_link_libraries(native_input_state_test PRIVATE tdur_native_input_state)
    tdur_compile_options(native_input_state_test)
    add_test(NAME native_input_state COMMAND native_input_state_test)
    set_tests_properties(native_input_state PROPERTIES TIMEOUT 45)

    add_executable(native_input_selection_test tests/native_input_selection_test.cpp)
    target_link_libraries(native_input_selection_test PRIVATE tdur_native_input_selection)
    tdur_compile_options(native_input_selection_test)
    add_test(NAME native_input_selection COMMAND native_input_selection_test)
    set_tests_properties(native_input_selection PROPERTIES TIMEOUT 20)

    add_executable(vmf_image_bindings_test tests/vmf_image_bindings_test.cpp)
    target_link_libraries(vmf_image_bindings_test PRIVATE tdur_vmf_image_bindings)
    tdur_compile_options(vmf_image_bindings_test)
    add_test(NAME vmf_image_bindings COMMAND vmf_image_bindings_test)

    add_executable(vmf_display_data_test tests/vmf_display_data_test.cpp)
    target_link_libraries(vmf_display_data_test PRIVATE tdur_vmf_display_data)
    tdur_compile_options(vmf_display_data_test)
    add_test(NAME vmf_display_data COMMAND vmf_display_data_test)
    set_tests_properties(vmf_display_data PROPERTIES TIMEOUT 45)

    add_executable(vmf_display_runtime_test tests/vmf_display_runtime_test.cpp)
    target_link_libraries(vmf_display_runtime_test PRIVATE
        tdur_vmf_display_runtime)
    tdur_compile_options(vmf_display_runtime_test)
    add_test(NAME vmf_display_runtime COMMAND vmf_display_runtime_test)
    set_tests_properties(vmf_display_runtime PROPERTIES TIMEOUT 45)

    add_executable(vmf_transform_test tests/vmf_transform_test.cpp)
    target_link_libraries(vmf_transform_test PRIVATE tdur_vmf_transform)
    tdur_compile_options(vmf_transform_test)
    if(MSVC)
        target_compile_options(vmf_transform_test PRIVATE /fp:strict)
    else()
        target_compile_options(vmf_transform_test PRIVATE
            -ffp-contract=off -frounding-math)
    endif()
    add_test(NAME vmf_transform COMMAND vmf_transform_test)
    set_tests_properties(vmf_transform PROPERTIES TIMEOUT 45)

    if(MSVC AND WIN32)
        enable_language(ASM_MASM)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(vmf_transform_reference
                tests/fixtures/vmf_transform_reference_x64.asm)
        else()
            set(vmf_transform_reference
                tests/fixtures/vmf_transform_reference_win32.asm)
            set_source_files_properties(${vmf_transform_reference}
                PROPERTIES COMPILE_OPTIONS /safeseh)
        endif()
        add_library(vmf_transform_reference OBJECT ${vmf_transform_reference})
        foreach(kind IN ITEMS order trap)
            add_executable(vmf_transform_${kind}_test
                tests/vmf_transform_${kind}_test.cpp
                $<TARGET_OBJECTS:vmf_transform_reference>)
            target_link_libraries(vmf_transform_${kind}_test
                PRIVATE tdur_vmf_transform)
            tdur_compile_options(vmf_transform_${kind}_test)
            target_compile_options(vmf_transform_${kind}_test PRIVATE /fp:strict)
            add_test(NAME vmf_transform_${kind}
                COMMAND vmf_transform_${kind}_test)
            set_tests_properties(vmf_transform_${kind} PROPERTIES TIMEOUT 45)
        endforeach()
    endif()

    add_executable(vmf_number_test tests/vmf_number_test.cpp)
    target_link_libraries(vmf_number_test PRIVATE tdur_vmf_number)
    tdur_compile_options(vmf_number_test)
    if(MSVC)
        target_compile_options(vmf_number_test PRIVATE /fp:strict)
    else()
        target_compile_options(vmf_number_test PRIVATE
            -ffp-contract=off -frounding-math)
    endif()
    add_test(NAME vmf_number_conversion COMMAND vmf_number_test)

    add_executable(vmf_number_format_test tests/vmf_number_format_test.cpp)
    target_link_libraries(vmf_number_format_test PRIVATE tdur_vmf_number)
    tdur_compile_options(vmf_number_format_test)
    if(MSVC)
        target_compile_options(vmf_number_format_test PRIVATE /fp:strict)
    else()
        target_compile_options(vmf_number_format_test PRIVATE
            -ffp-contract=off -frounding-math)
    endif()
    add_test(NAME vmf_number_format COMMAND vmf_number_format_test)
    add_executable(vmf_variable_hash_test tests/vmf_variable_hash_test.cpp)
    target_link_libraries(vmf_variable_hash_test PRIVATE tdur_vmf_variable_hash)
    tdur_compile_options(vmf_variable_hash_test)
    add_test(NAME vmf_variable_name_hash COMMAND vmf_variable_hash_test)

    add_executable(vmf_variable_reference_test
        tests/vmf_variable_reference_test.cpp)
    target_link_libraries(vmf_variable_reference_test PRIVATE
        tdur_vmf_variable_reference)
    tdur_compile_options(vmf_variable_reference_test)
    add_test(NAME vmf_variable_reference COMMAND vmf_variable_reference_test)

    add_executable(vmf_variables_test tests/vmf_variables_test.cpp)
    target_link_libraries(vmf_variables_test PRIVATE tdur_vmf_variables)
    tdur_compile_options(vmf_variables_test)
    if(MSVC)
        target_compile_options(vmf_variables_test PRIVATE /fp:strict)
    endif()
    add_test(NAME vmf_variable_dictionary COMMAND vmf_variables_test)

    add_executable(vmf_stack_test tests/vmf_stack_test.cpp)
    target_link_libraries(vmf_stack_test PRIVATE tdur_vmf_stack)
    tdur_compile_options(vmf_stack_test)
    if(MSVC)
        target_compile_options(vmf_stack_test PRIVATE /fp:strict)
    else()
        target_compile_options(vmf_stack_test PRIVATE
            -ffp-contract=off -frounding-math)
    endif()
    add_test(NAME vmf_value_stack COMMAND vmf_stack_test)

    add_executable(vmf_execution_test tests/vmf_execution_test.cpp)
    target_link_libraries(vmf_execution_test PRIVATE tdur_vmf_execution)
    tdur_compile_options(vmf_execution_test)
    if(MSVC)
        target_compile_options(vmf_execution_test PRIVATE /fp:strict)
    else()
        target_compile_options(vmf_execution_test PRIVATE
            -ffp-contract=off -frounding-math)
    endif()
    add_test(NAME vmf_scalar_execution COMMAND vmf_execution_test)
    add_executable(vmf_timeline_execution_test
        tests/vmf_timeline_execution_test.cpp)
    target_link_libraries(vmf_timeline_execution_test PRIVATE tdur_vmf_execution)
    tdur_compile_options(vmf_timeline_execution_test)
    add_test(NAME vmf_timeline_execution COMMAND vmf_timeline_execution_test)
    set_tests_properties(vmf_timeline_execution PROPERTIES TIMEOUT 45)
    add_executable(vmf_target_execution_test tests/vmf_target_execution_test.cpp)
    target_link_libraries(vmf_target_execution_test PRIVATE tdur_vmf_execution)
    tdur_compile_options(vmf_target_execution_test)
    add_test(NAME vmf_target_execution COMMAND vmf_target_execution_test)
    set_tests_properties(vmf_target_execution PROPERTIES TIMEOUT 45)

    add_executable(vmf_get_variable_test tests/vmf_get_variable_test.cpp)
    target_link_libraries(vmf_get_variable_test PRIVATE
        tdur_vmf_frame_player tdur_vmf_menu_data tdur_vmf_help_services)
    if(TARGET tdur_database_resources)
        target_link_libraries(vmf_get_variable_test PRIVATE tdur_database_resources)
        target_compile_definitions(vmf_get_variable_test PRIVATE
            TDUR_TEST_OFFICIAL_MENU_DATA=1)
    endif()
    tdur_compile_options(vmf_get_variable_test)
    if(MSVC)
        target_compile_options(vmf_get_variable_test PRIVATE /fp:strict)
    endif()
    add_test(NAME vmf_get_variable COMMAND vmf_get_variable_test)
    add_test(NAME vmf_host_integer COMMAND vmf_get_variable_test --host-integer)
    set_tests_properties(vmf_host_integer PROPERTIES TIMEOUT 45)
    add_test(NAME vmf_host_write COMMAND vmf_get_variable_test --host-write)
    add_test(NAME vmf_binding_mask COMMAND vmf_get_variable_test --binding-mask)
    add_test(NAME vmf_menu_data COMMAND vmf_get_variable_test --menu-data)
    add_test(NAME vmf_menu_finalizers COMMAND vmf_get_variable_test --menu-finalizers)
    add_executable(vmf_help_services_test tests/vmf_help_services_test.cpp)
    target_link_libraries(vmf_help_services_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_help_services_test)
    add_test(NAME vmf_help_services COMMAND vmf_help_services_test)
    set_tests_properties(vmf_help_services PROPERTIES TIMEOUT 45)
    add_executable(vmf_help_message_test tests/vmf_help_message_test.cpp)
    add_executable(vmf_notification_state_test tests/vmf_notification_state_test.cpp)
    target_link_libraries(vmf_notification_state_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_state_test)
    add_test(NAME vmf_notification_state COMMAND vmf_notification_state_test)
    set_tests_properties(vmf_notification_state PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_child_test tests/vmf_notification_child_test.cpp)
    target_link_libraries(vmf_notification_child_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_child_test)
    add_test(NAME vmf_notification_child COMMAND vmf_notification_child_test)
    set_tests_properties(vmf_notification_child PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_text_test tests/vmf_notification_text_test.cpp)
    target_link_libraries(vmf_notification_text_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_text_test)
    add_test(NAME vmf_notification_text COMMAND vmf_notification_text_test)
    set_tests_properties(vmf_notification_text PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_identity_test tests/vmf_notification_identity_test.cpp)
    target_link_libraries(vmf_notification_identity_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_identity_test)
    add_test(NAME vmf_notification_identity COMMAND vmf_notification_identity_test)
    set_tests_properties(vmf_notification_identity PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_record_test tests/vmf_notification_record_test.cpp)
    target_link_libraries(vmf_notification_record_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_record_test)
    add_test(NAME vmf_notification_record COMMAND vmf_notification_record_test)
    set_tests_properties(vmf_notification_record PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_queue_test tests/vmf_notification_queue_test.cpp)
    target_link_libraries(vmf_notification_queue_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_queue_test)
    add_test(NAME vmf_notification_queue COMMAND vmf_notification_queue_test)
    set_tests_properties(vmf_notification_queue PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_reset_test tests/vmf_notification_reset_test.cpp)
    target_link_libraries(vmf_notification_reset_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_reset_test)
    add_test(NAME vmf_notification_reset COMMAND vmf_notification_reset_test)
    set_tests_properties(vmf_notification_reset PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_rings_test tests/vmf_notification_rings_test.cpp)
    target_link_libraries(vmf_notification_rings_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_rings_test)
    add_test(NAME vmf_notification_rings COMMAND vmf_notification_rings_test)
    set_tests_properties(vmf_notification_rings PROPERTIES TIMEOUT 45)
    add_test(NAME vmf_notification_flag COMMAND vmf_get_variable_test --notification-flag)
    set_tests_properties(vmf_notification_flag PROPERTIES TIMEOUT 45)
    add_test(NAME vmf_notification_format COMMAND vmf_get_variable_test --notification-format)
    set_tests_properties(vmf_notification_format PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_scan_test tests/vmf_notification_scan_test.cpp)
    target_link_libraries(vmf_notification_scan_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_scan_test)
    add_test(NAME vmf_notification_scan COMMAND vmf_notification_scan_test)
    set_tests_properties(vmf_notification_scan PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_lookup_test tests/vmf_notification_lookup_test.cpp)
    target_link_libraries(vmf_notification_lookup_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_lookup_test)
    add_test(NAME vmf_notification_lookup COMMAND vmf_notification_lookup_test)
    set_tests_properties(vmf_notification_lookup PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_prefix_test tests/vmf_notification_prefix_test.cpp)
    target_link_libraries(vmf_notification_prefix_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_prefix_test)
    add_test(NAME vmf_notification_prefix COMMAND vmf_notification_prefix_test)
    set_tests_properties(vmf_notification_prefix PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_completion_test tests/vmf_notification_completion_test.cpp)
    target_link_libraries(vmf_notification_completion_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_completion_test)
    if(MSVC)
        target_compile_options(vmf_notification_completion_test PRIVATE /fp:strict)
    endif()
    add_test(NAME vmf_notification_completion COMMAND vmf_notification_completion_test)
    set_tests_properties(vmf_notification_completion PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_replace_test tests/vmf_notification_replace_test.cpp)
    target_link_libraries(vmf_notification_replace_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_replace_test)
    add_test(NAME vmf_notification_replace COMMAND vmf_notification_replace_test)
    set_tests_properties(vmf_notification_replace PROPERTIES TIMEOUT 45)

    add_executable(vmf_notification_money_test tests/vmf_notification_money_test.cpp)
    target_link_libraries(vmf_notification_money_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_money_test)
    add_test(NAME vmf_notification_money COMMAND vmf_notification_money_test)
    set_tests_properties(vmf_notification_money PROPERTIES TIMEOUT 45)

    add_executable(vmf_notification_named_test tests/vmf_notification_named_test.cpp)
    target_link_libraries(vmf_notification_named_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_named_test)
    add_test(NAME vmf_notification_named COMMAND vmf_notification_named_test)
    set_tests_properties(vmf_notification_named PROPERTIES TIMEOUT 45)

    add_executable(vmf_notification_wrapper_test tests/vmf_notification_wrapper_test.cpp)
    target_link_libraries(vmf_notification_wrapper_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_wrapper_test)
    add_test(NAME vmf_notification_wrapper COMMAND vmf_notification_wrapper_test)
    set_tests_properties(vmf_notification_wrapper PROPERTIES TIMEOUT 45)

    add_executable(vmf_notification_display_test tests/vmf_notification_display_test.cpp)
    target_link_libraries(vmf_notification_display_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_display_test)
    add_test(NAME vmf_notification_display COMMAND vmf_notification_display_test)
    set_tests_properties(vmf_notification_display PROPERTIES TIMEOUT 45)

    add_executable(vmf_notification_assignment_test tests/vmf_notification_assignment_test.cpp)
    target_link_libraries(vmf_notification_assignment_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_assignment_test)
    add_test(NAME vmf_notification_assignment COMMAND vmf_notification_assignment_test)
    set_tests_properties(vmf_notification_assignment PROPERTIES TIMEOUT 45)

    add_executable(vmf_menu_display_selection_test tests/vmf_menu_display_selection_test.cpp)
    target_link_libraries(vmf_menu_display_selection_test PRIVATE tdur_vmf_menu_data)
    tdur_compile_options(vmf_menu_display_selection_test)
    add_test(NAME vmf_menu_display_selection COMMAND vmf_menu_display_selection_test)
    set_tests_properties(vmf_menu_display_selection PROPERTIES TIMEOUT 45)

    add_executable(vmf_notification_deferred_queue_test tests/vmf_notification_deferred_queue_test.cpp)
    target_link_libraries(vmf_notification_deferred_queue_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_deferred_queue_test)
    add_test(NAME vmf_notification_deferred_queue COMMAND vmf_notification_deferred_queue_test)
    set_tests_properties(vmf_notification_deferred_queue PROPERTIES TIMEOUT 45)

    add_executable(vmf_notification_child_async_test tests/vmf_notification_child_async_test.cpp)
    target_link_libraries(vmf_notification_child_async_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_child_async_test)
    add_test(NAME vmf_notification_child_continuation COMMAND vmf_notification_child_async_test)
    set_tests_properties(vmf_notification_child_continuation PROPERTIES TIMEOUT 45)

    add_executable(vmf_notification_dispatch_test tests/vmf_notification_dispatch_test.cpp)
    target_link_libraries(vmf_notification_dispatch_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_dispatch_test)
    add_test(NAME vmf_notification_dispatch COMMAND vmf_notification_dispatch_test)
    set_tests_properties(vmf_notification_dispatch PROPERTIES TIMEOUT 45)

    add_executable(vmf_notification_toggle_test tests/vmf_notification_toggle_test.cpp)
    target_link_libraries(vmf_notification_toggle_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_toggle_test)
    add_test(NAME vmf_notification_toggle COMMAND vmf_notification_toggle_test)
    set_tests_properties(vmf_notification_toggle PROPERTIES TIMEOUT 45)

    add_executable(vmf_notification_parent_test tests/vmf_notification_parent_test.cpp)
    target_link_libraries(vmf_notification_parent_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_parent_test)
    add_test(NAME vmf_notification_parent COMMAND vmf_notification_parent_test)
    set_tests_properties(vmf_notification_parent PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_effect_db30_test tests/vmf_notification_effect_db30_test.cpp)
    target_link_libraries(vmf_notification_effect_db30_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_effect_db30_test)
    add_test(NAME vmf_notification_effect_db30 COMMAND vmf_notification_effect_db30_test)
    set_tests_properties(vmf_notification_effect_db30 PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_database_test tests/vmf_notification_database_test.cpp)
    target_link_libraries(vmf_notification_database_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_database_test)
    add_test(NAME vmf_notification_database COMMAND vmf_notification_database_test)
    set_tests_properties(vmf_notification_database PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_effect_bf80_test tests/vmf_notification_effect_bf80_test.cpp)
    target_link_libraries(vmf_notification_effect_bf80_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_effect_bf80_test)
    add_test(NAME vmf_notification_effect_bf80 COMMAND vmf_notification_effect_bf80_test)
    set_tests_properties(vmf_notification_effect_bf80 PROPERTIES TIMEOUT 45)
    add_executable(vmf_notification_effect_pool_test tests/vmf_notification_effect_pool_test.cpp)
    target_link_libraries(vmf_notification_effect_pool_test PRIVATE tdur_vmf_help_services)
    tdur_compile_options(vmf_notification_effect_pool_test)
    add_test(NAME vmf_notification_effect_pool COMMAND vmf_notification_effect_pool_test)
    set_tests_properties(vmf_notification_effect_pool PROPERTIES TIMEOUT 45)
    add_executable(vmf_fscommand_test tests/vmf_fscommand_test.cpp)
    target_link_libraries(vmf_fscommand_test PRIVATE
        tdur_vmf_host_commands tdur_vmf_help_services)
    tdur_compile_options(vmf_fscommand_test)
    add_test(NAME vmf_fscommand COMMAND vmf_fscommand_test)
    set_tests_properties(vmf_fscommand PROPERTIES TIMEOUT 45)
    add_executable(vmf_fscommand_router_test tests/vmf_fscommand_router_test.cpp)
    target_link_libraries(vmf_fscommand_router_test PRIVATE
        tdur_vmf_host_commands tdur_vmf_help_services)
    tdur_compile_options(vmf_fscommand_router_test)
    add_test(NAME vmf_fscommand_router COMMAND vmf_fscommand_router_test)
    set_tests_properties(vmf_fscommand_router PROPERTIES TIMEOUT 45)
    add_executable(vmf_fscommand_global_test tests/vmf_fscommand_global_test.cpp)
    target_link_libraries(vmf_fscommand_global_test PRIVATE
        tdur_vmf_host_commands tdur_vmf_help_services)
    tdur_compile_options(vmf_fscommand_global_test)
    add_test(NAME vmf_fscommand_global COMMAND vmf_fscommand_global_test)
    set_tests_properties(vmf_fscommand_global PROPERTIES TIMEOUT 45)
    add_executable(vmf_fscommand_collection_test tests/vmf_fscommand_collection_test.cpp)
    target_link_libraries(vmf_fscommand_collection_test PRIVATE
        tdur_vmf_host_commands tdur_vmf_menu_data)
    tdur_compile_options(vmf_fscommand_collection_test)
    add_test(NAME vmf_fscommand_collection COMMAND vmf_fscommand_collection_test)
    set_tests_properties(vmf_fscommand_collection PROPERTIES TIMEOUT 45)
    add_executable(vmf_catalog_test tests/vmf_catalog_test.cpp)
    target_link_libraries(vmf_catalog_test PRIVATE tdur_vmf_catalog)
    tdur_compile_options(vmf_catalog_test)
    add_test(NAME vmf_catalog COMMAND vmf_catalog_test)
    set_tests_properties(vmf_catalog PROPERTIES TIMEOUT 45)
    target_link_libraries(vmf_help_message_test PRIVATE tdur_vmf_help_services)
    if(TARGET tdur_database_resources)
        target_link_libraries(vmf_help_message_test PRIVATE tdur_database_resources)
        target_compile_definitions(vmf_help_message_test PRIVATE
            TDUR_TEST_OFFICIAL_TUTORIAL_SERVICE=1)
    endif()
    tdur_compile_options(vmf_help_message_test)
    add_test(NAME vmf_help_message COMMAND vmf_help_message_test)
    add_test(NAME vmf_tutorial_service COMMAND vmf_help_message_test --tutorial-service)
    set_tests_properties(vmf_tutorial_service PROPERTIES TIMEOUT 45)
    set_tests_properties(vmf_help_message PROPERTIES TIMEOUT 45)
    if(TARGET tdur_database_resources)
        add_executable(vmf_tutorial_data_test tests/vmf_tutorial_data_test.cpp)
        target_link_libraries(vmf_tutorial_data_test PRIVATE
            tdur_vmf_help_services tdur_database_resources)
        tdur_compile_options(vmf_tutorial_data_test)
        add_test(NAME vmf_tutorial_data COMMAND vmf_tutorial_data_test)
        set_tests_properties(vmf_tutorial_data PROPERTIES TIMEOUT 45)
    endif()
    set_tests_properties(vmf_menu_finalizers PROPERTIES TIMEOUT 45)
    set_tests_properties(vmf_menu_data PROPERTIES TIMEOUT 45)
    set_tests_properties(vmf_host_write vmf_binding_mask PROPERTIES TIMEOUT 45)
    set_tests_properties(vmf_get_variable PROPERTIES TIMEOUT 45)
    add_test(NAME vmf_set_variable COMMAND vmf_get_variable_test --write)
    add_test(NAME vmf_variable_lifecycle COMMAND vmf_get_variable_test --lifecycle)
    set_tests_properties(vmf_set_variable vmf_variable_lifecycle
        PROPERTIES TIMEOUT 45)
    add_executable(vmf_frame_player_test tests/vmf_frame_player_test.cpp)
    target_link_libraries(vmf_frame_player_test PRIVATE tdur_vmf_frame_player)
    tdur_compile_options(vmf_frame_player_test)
    add_test(NAME vmf_frame_player COMMAND vmf_frame_player_test)
    set_tests_properties(vmf_frame_player PROPERTIES TIMEOUT 45)
    add_executable(vmf_text_bindings_test tests/vmf_text_bindings_test.cpp)
    target_link_libraries(vmf_text_bindings_test PRIVATE tdur_vmf_frame_player)
    tdur_compile_options(vmf_text_bindings_test)
    add_test(NAME vmf_text_bindings COMMAND vmf_text_bindings_test)
    set_tests_properties(vmf_text_bindings PROPERTIES TIMEOUT 45)
    add_executable(vmf_property_test tests/vmf_property_test.cpp)
    target_link_libraries(vmf_property_test PRIVATE tdur_vmf_frame_player)
    tdur_compile_options(vmf_property_test)
    if(MSVC)
        target_compile_options(vmf_property_test PRIVATE /fp:strict)
    endif()
    add_test(NAME vmf_properties COMMAND vmf_property_test)
    set_tests_properties(vmf_properties PROPERTIES TIMEOUT 45)
    set_tests_properties(vmf_image_bindings vmf_number_conversion vmf_number_format vmf_value_stack vmf_scalar_execution
        PROPERTIES TIMEOUT 45)

    add_executable(adaptive_automatic_test tests/adaptive_automatic_test.cpp)
    target_link_libraries(adaptive_automatic_test PRIVATE tdur_adaptive_automatic)
    tdur_compile_options(adaptive_automatic_test)
    add_test(NAME adaptive_automatic_driving_policy COMMAND adaptive_automatic_test)

    add_executable(fixed_step_test tests/fixed_step_test.cpp)
    target_link_libraries(fixed_step_test PRIVATE tdur_fixed_step)
    tdur_compile_options(fixed_step_test)
    add_test(NAME fixed_step_simulation_calendar COMMAND fixed_step_test)

    add_executable(legacy_state_machine_test tests/legacy_state_machine_test.cpp)
    target_link_libraries(legacy_state_machine_test PRIVATE tdur_legacy_state_machine)
    tdur_compile_options(legacy_state_machine_test)
    add_test(NAME original_state_machine_dispatch COMMAND legacy_state_machine_test)

    add_executable(legacy_population_test tests/legacy_population_test.cpp)
    target_link_libraries(legacy_population_test PRIVATE tdur_legacy_population)
    tdur_compile_options(legacy_population_test)
    if(MSVC)
        target_compile_options(legacy_population_test PRIVATE /fp:precise)
    endif()
    add_test(NAME original_ai_population_policy COMMAND legacy_population_test)

    add_executable(legacy_physics_timer_test tests/legacy_timer_test.cpp)
    target_link_libraries(legacy_physics_timer_test PRIVATE tdur_legacy_physics_timer)
    tdur_compile_options(legacy_physics_timer_test)
    add_test(NAME original_physics_timer_callback COMMAND legacy_physics_timer_test)

    add_executable(fixed_physics_timer_test tests/fixed_timer_test.cpp)
    target_link_libraries(fixed_physics_timer_test PRIVATE
        tdur_fixed_physics_timer tdur_legacy_physics_timer)
    tdur_compile_options(fixed_physics_timer_test)
    add_test(NAME fixed_physics_timer_owner COMMAND fixed_physics_timer_test)

    add_executable(physics_binding_test tests/physics_binding_test.cpp)
    target_link_libraries(physics_binding_test PRIVATE tdur_physics_binding)
    tdur_compile_options(physics_binding_test)
    add_test(NAME physics_binding_bounded_core COMMAND physics_binding_test)
    add_executable(ui_service_binding_test tests/ui_service_binding_test.cpp)
    target_link_libraries(ui_service_binding_test PRIVATE tdur_ui_service_binding)
    tdur_compile_options(ui_service_binding_test)
    add_test(NAME ui_service_binding_bounded_core COMMAND ui_service_binding_test)
    set_tests_properties(ui_service_binding_bounded_core PROPERTIES TIMEOUT 20)

    if(WIN32)
        add_executable(fixed_physics_worker_test tests/fixed_worker_test.cpp)
        target_link_libraries(fixed_physics_worker_test PRIVATE tdur_fixed_physics_worker)
        target_compile_definitions(fixed_physics_worker_test PRIVATE
            WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
        tdur_compile_options(fixed_physics_worker_test)
        add_test(NAME fixed_physics_worker_lifecycle COMMAND fixed_physics_worker_test)
        set_tests_properties(fixed_physics_worker_lifecycle PROPERTIES TIMEOUT 25)

        add_test(NAME runtime_ui_service_readonly_probe
            COMMAND tdur_probe_ui_services --self-test)
        set_tests_properties(runtime_ui_service_readonly_probe PROPERTIES TIMEOUT 20)
        add_test(NAME runtime_timing_readonly_probe
            COMMAND tdur_probe_timing --self-test)
        add_test(NAME official_video_config_overrides
            COMMAND tdur_prepare_video_config --self-test)
    endif()

    option(TDUR_BUILD_D3D12_DEVICE_TESTS "Build the independent native D3D12 device tests" ON)
    if(WIN32 AND TDUR_BUILD_D3D12_DEVICE_TESTS)
        add_subdirectory(tests/render)
    endif()
endif()
