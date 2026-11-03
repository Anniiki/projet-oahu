# Experimental in-process DirectInput device. No SDL dependency when OFF.
option(TDUR_MOD_GAMEPAD "Build the experimental SDL DirectInput gamepad" OFF)
set(TDUR_MOD_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

function(tdur_gamepad_build_settings target)
    target_include_directories(${target} PRIVATE "${TDUR_MOD_DIR}/include")
    target_compile_options(${target} PRIVATE
        /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
    target_compile_definitions(${target} PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
endfunction()

add_library(tdur_gamepad_dinput_format STATIC
    "${TDUR_MOD_DIR}/src/gamepad_dinput_format.cpp")
tdur_gamepad_build_settings(tdur_gamepad_dinput_format)
target_link_libraries(tdur_gamepad_dinput_format PUBLIC dxguid)

add_executable(tdur_gamepad_dinput_format_test
    "${TDUR_MOD_DIR}/tests/gamepad_dinput_format_test.cpp")
tdur_gamepad_build_settings(tdur_gamepad_dinput_format_test)
target_link_libraries(tdur_gamepad_dinput_format_test PRIVATE
    tdur_gamepad_dinput_format dinput8)
add_test(NAME gamepad_dinput_format COMMAND tdur_gamepad_dinput_format_test)

foreach(kind identity mock)
    add_executable(tdur_gamepad_factory_${kind}_test
        "${TDUR_MOD_DIR}/tests/gamepad_factory_${kind}_test.cpp"
        "${TDUR_MOD_DIR}/src/gamepad_dinput.cpp")
    tdur_gamepad_build_settings(tdur_gamepad_factory_${kind}_test)
    target_link_libraries(tdur_gamepad_factory_${kind}_test PRIVATE
        tdur_gamepad_dinput_format dinput8)
    add_test(NAME gamepad_factory_${kind}
        COMMAND tdur_gamepad_factory_${kind}_test)
endforeach()
set_tests_properties(gamepad_dinput_format gamepad_factory_identity
    gamepad_factory_mock PROPERTIES TIMEOUT 15)

if(TDUR_MOD_GAMEPAD)
    enable_language(C)
    if(NOT TARGET SDL3::SDL3-static)
        include("${TDUR_MOD_DIR}/../cmake/sdl3.cmake")
    endif()
    add_library(tdur_mod_sdl_gamepads STATIC
        "${TDUR_MOD_DIR}/../src/input/sdl_gamepads.cpp")
    tdur_gamepad_build_settings(tdur_mod_sdl_gamepads)
    target_include_directories(tdur_mod_sdl_gamepads PUBLIC
        "${TDUR_MOD_DIR}/../include")
    target_link_libraries(tdur_mod_sdl_gamepads PUBLIC SDL3::SDL3-static)

    add_library(tdur_gamepad_service STATIC
        "${TDUR_MOD_DIR}/src/gamepad_service.cpp")
    tdur_gamepad_build_settings(tdur_gamepad_service)
    target_link_libraries(tdur_gamepad_service PUBLIC
        tdur_mod_sdl_gamepads tdur_gamepad_dinput_format)

    add_executable(tdur_gamepad_service_lifecycle_test
        "${TDUR_MOD_DIR}/tests/gamepad_service_lifecycle_test.cpp")
    tdur_gamepad_build_settings(tdur_gamepad_service_lifecycle_test)
    target_link_libraries(tdur_gamepad_service_lifecycle_test PRIVATE
        tdur_gamepad_service)
    add_test(NAME gamepad_service_lifecycle
        COMMAND tdur_gamepad_service_lifecycle_test)

    add_executable(tdur_gamepad_virtual_chain_test
        "${TDUR_MOD_DIR}/tests/gamepad_virtual_chain_test.cpp"
        "${TDUR_MOD_DIR}/src/gamepad_dinput.cpp")
    tdur_gamepad_build_settings(tdur_gamepad_virtual_chain_test)
    target_link_libraries(tdur_gamepad_virtual_chain_test PRIVATE
        tdur_gamepad_service dinput8)
    add_test(NAME gamepad_virtual_chain COMMAND tdur_gamepad_virtual_chain_test)

    # Compile a separate service object with fault injection only in this test.
    # The proxy and its service library never receive these definitions.
    add_executable(tdur_gamepad_service_fault_test
        "${TDUR_MOD_DIR}/tests/gamepad_service_fault_test.cpp"
        "${TDUR_MOD_DIR}/src/gamepad_service.cpp")
    tdur_gamepad_build_settings(tdur_gamepad_service_fault_test)
    target_compile_options(tdur_gamepad_service_fault_test PRIVATE
        "/FI${TDUR_MOD_DIR}/tests/gamepad_service_fault_hooks.hpp")
    target_link_libraries(tdur_gamepad_service_fault_test PRIVATE
        tdur_mod_sdl_gamepads tdur_gamepad_dinput_format)
    foreach(mode event thread cpp closing closing-timeout shared)
        add_test(NAME gamepad_service_fault_${mode}
            COMMAND tdur_gamepad_service_fault_test ${mode})
        set_tests_properties(gamepad_service_fault_${mode} PROPERTIES TIMEOUT 25)
    endforeach()
    set_tests_properties(gamepad_service_lifecycle gamepad_virtual_chain
        PROPERTIES TIMEOUT 40)
endif()

function(tdur_proxy_gamepad target)
    if(TDUR_MOD_GAMEPAD)
        target_sources(${target} PRIVATE "${TDUR_MOD_DIR}/src/gamepad_dinput.cpp")
        target_compile_definitions(${target} PRIVATE TDUR_MOD_GAMEPAD)
        target_link_libraries(${target} PRIVATE tdur_gamepad_service)
    endif()
endfunction()
