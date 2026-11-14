# Crash and hang dumps. Included from launcher/CMakeLists.txt, so
# relative paths start in mod/launcher.
set(TDUR_CRASH_OPTIONS /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
set(TDUR_CRASH_DEFINITIONS WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

# TH2: shared channel layout, names, header and heartbeat.
add_executable(tdur_crash_channel_test ../tests/crash_channel_test.cpp)
target_include_directories(tdur_crash_channel_test PRIVATE ../include)
target_compile_features(tdur_crash_channel_test PRIVATE cxx_std_23)
target_compile_options(tdur_crash_channel_test PRIVATE ${TDUR_CRASH_OPTIONS})
target_compile_definitions(tdur_crash_channel_test PRIVATE
    ${TDUR_CRASH_DEFINITIONS})
add_test(NAME crash_channel_layout COMMAND tdur_crash_channel_test)

# The real detour armed in a test process with a fake launcher thread.
add_executable(tdur_crash_handler_test ../launcher_tests/crash_handler_test.cpp
    ../src/crash_handler.cpp)
target_include_directories(tdur_crash_handler_test PRIVATE ../include)
target_compile_features(tdur_crash_handler_test PRIVATE cxx_std_23)
target_compile_options(tdur_crash_handler_test PRIVATE ${TDUR_CRASH_OPTIONS})
target_compile_definitions(tdur_crash_handler_test PRIVATE
    ${TDUR_CRASH_DEFINITIONS})
target_link_libraries(tdur_crash_handler_test PRIVATE tdur_detours)
foreach(mode chain absent invalid missing foreign)
    add_test(NAME crash_handler_${mode}
        COMMAND tdur_crash_handler_test ${mode})
    set_tests_properties(crash_handler_${mode} PROPERTIES TIMEOUT 60)
endforeach()

# TH1: watchdog decisions on simulated timelines.
add_executable(tdur_watchdog_core_test ../launcher_tests/watchdog_core_test.cpp
    watchdog_core.cpp)
target_include_directories(tdur_watchdog_core_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tdur_watchdog_core_test PRIVATE cxx_std_23)
target_compile_options(tdur_watchdog_core_test PRIVATE ${TDUR_CRASH_OPTIONS})
add_test(NAME watchdog_decisions COMMAND tdur_watchdog_core_test)

# Out-of-process dump helper, next to the launcher that looks for it.
function(tdur_add_dump_helper target)
    add_executable(${target} dump_helper.cpp)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_options(${target} PRIVATE ${TDUR_CRASH_OPTIONS})
    target_compile_definitions(${target} PRIVATE ${TDUR_CRASH_DEFINITIONS})
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()
tdur_add_dump_helper(tdur_dump_helper)
add_dependencies(tdur_observe_launch tdur_dump_helper)
# Test variant only: stays 30 s inside the dump (review D2 measurement).
# Measured with dbghelp 6.2.26100.9818: the target
# is suspended during the dump and resumes once the helper process ends.
tdur_add_dump_helper(tdur_dump_helper_stall)
target_compile_definitions(tdur_dump_helper_stall PRIVATE
    TDUR_CRASH_HELPER_STALL=30000)
set_target_properties(tdur_dump_helper_stall PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/stall")

add_executable(tdur_crash_witness ../launcher_tests/crash_witness.cpp)
target_compile_features(tdur_crash_witness PRIVATE cxx_std_23)
target_compile_options(tdur_crash_witness PRIVATE ${TDUR_CRASH_OPTIONS})
target_compile_definitions(tdur_crash_witness PRIVATE ${TDUR_CRASH_DEFINITIONS})
target_link_libraries(tdur_crash_witness PRIVATE user32)
target_link_options(tdur_crash_witness PRIVATE /NXCOMPAT)

add_library(tdur_loader_hold SHARED ../launcher_tests/loader_hold_dll.cpp)
target_compile_options(tdur_loader_hold PRIVATE ${TDUR_CRASH_OPTIONS})
target_compile_definitions(tdur_loader_hold PRIVATE ${TDUR_CRASH_DEFINITIONS})

# TH3, TH11, TH14 and the D2 measurement: the helper alone, on witnesses.
set(TDUR_CRASH_HELPER_FIXTURE
    "${CMAKE_CURRENT_SOURCE_DIR}/../launcher_tests/run_dump_helper_fixture.py")
add_test(NAME dump_helper_self_check
    COMMAND "${Python3_EXECUTABLE}" -I "${TDUR_CRASH_HELPER_FIXTURE}" self-check
        --helper "$<TARGET_FILE:tdur_dump_helper>")
add_test(NAME dump_helper_refuses
    COMMAND "${Python3_EXECUTABLE}" -I "${TDUR_CRASH_HELPER_FIXTURE}" refuses
        --helper "$<TARGET_FILE:tdur_dump_helper>"
        --witness "$<TARGET_FILE:tdur_crash_witness>"
        --work-root "${CMAKE_BINARY_DIR}/dump-helper-fixture")
add_test(NAME dump_helper_dump_readable
    COMMAND "${Python3_EXECUTABLE}" -I "${TDUR_CRASH_HELPER_FIXTURE}" dump
        --helper "$<TARGET_FILE:tdur_dump_helper>"
        --witness "$<TARGET_FILE:tdur_crash_witness>"
        --reader "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/minidump_summary.py"
        --work-root "${CMAKE_BINARY_DIR}/dump-helper-fixture")
add_test(NAME dump_helper_suspension
    COMMAND "${Python3_EXECUTABLE}" -I "${TDUR_CRASH_HELPER_FIXTURE}" suspension
        --helper "$<TARGET_FILE:tdur_dump_helper_stall>"
        --witness "$<TARGET_FILE:tdur_crash_witness>"
        --expect-resumed yes
        --work-root "${CMAKE_BINARY_DIR}/dump-helper-fixture")
add_test(NAME dump_helper_loader_lock
    COMMAND "${Python3_EXECUTABLE}" -I "${TDUR_CRASH_HELPER_FIXTURE}" dump
        --helper "$<TARGET_FILE:tdur_dump_helper>"
        --witness "$<TARGET_FILE:tdur_crash_witness>"
        --loader-dll "$<TARGET_FILE:tdur_loader_hold>"
        --reader "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/minidump_summary.py"
        --work-root "${CMAKE_BINARY_DIR}/dump-helper-fixture")
add_test(NAME minidump_summary_parser
    COMMAND "${Python3_EXECUTABLE}" -I
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/tests/minidump_summary_test.py")
set_tests_properties(dump_helper_self_check dump_helper_refuses
    dump_helper_dump_readable dump_helper_suspension
    dump_helper_loader_lock
    minidump_summary_parser PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

# TH4 to TH13, D1, D6, D7: crash and hang dumps through the real launcher
# and the real observer DLL, on tdur_crash_witness.
set(TDUR_CRASH_LAUNCHER_FIXTURE
    "${CMAKE_CURRENT_SOURCE_DIR}/../launcher_tests/run_crash_launcher_fixture.py")
foreach(scenario crash-exec-private crash-write-reserved crash-stack-overflow
        crash-handled hang-ui-stop hang-cross-thread hang-loader busy-pumping
        stutter foreign helper-gone options)
    string(REPLACE "-" "_" test_name "crash_launcher_${scenario}")
    add_test(NAME ${test_name}
        COMMAND "${Python3_EXECUTABLE}" -I "${TDUR_CRASH_LAUNCHER_FIXTURE}"
            ${scenario}
            --launcher "$<TARGET_FILE:tdur_observe_launch>"
            --witness "$<TARGET_FILE:tdur_crash_witness>"
            --dll "$<TARGET_FILE:tdur_observation_proxy>"
            --reader "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/minidump_summary.py"
            --loader-dll "$<TARGET_FILE:tdur_loader_hold>"
            --work-root "${CMAKE_BINARY_DIR}/crash-launcher-fixture")
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 150 RUN_SERIAL TRUE)
endforeach()

# Review F1: the question of a play session through the real launcher. Test
# variants of tdur_observe_launch, same sources and settings, whose question
# returns a fixed answer instead of opening a box. Each one sits next to its
# own copy of the dump helper, which the launcher requires before starting.
get_target_property(tdur_ask_launch_sources tdur_observe_launch SOURCES)
foreach(tdur_ask_answer yes no)
    set(tdur_ask_variant tdur_observe_launch_ask_${tdur_ask_answer})
    add_executable(${tdur_ask_variant} ${tdur_ask_launch_sources})
    foreach(tdur_ask_property INCLUDE_DIRECTORIES COMPILE_FEATURES
            COMPILE_OPTIONS COMPILE_DEFINITIONS LINK_LIBRARIES)
        get_target_property(tdur_ask_value tdur_observe_launch
            ${tdur_ask_property})
        if(tdur_ask_value)
            set_property(TARGET ${tdur_ask_variant} PROPERTY
                ${tdur_ask_property} "${tdur_ask_value}")
        endif()
    endforeach()
    if(tdur_ask_answer STREQUAL "yes")
        target_compile_definitions(${tdur_ask_variant} PRIVATE
            TDUR_CRASH_TEST_ANSWER=IDYES)
    else()
        target_compile_definitions(${tdur_ask_variant} PRIVATE
            TDUR_CRASH_TEST_ANSWER=IDNO)
    endif()
    set_target_properties(${tdur_ask_variant} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_CURRENT_BINARY_DIR}/ask-${tdur_ask_answer}")
    add_dependencies(${tdur_ask_variant} tdur_dump_helper)
    add_custom_command(TARGET ${tdur_ask_variant} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:tdur_dump_helper>"
            "$<TARGET_FILE_DIR:${tdur_ask_variant}>")
    add_test(NAME crash_launcher_hang_ui_ask_${tdur_ask_answer}
        COMMAND "${Python3_EXECUTABLE}" -I "${TDUR_CRASH_LAUNCHER_FIXTURE}"
            hang-ui-ask-${tdur_ask_answer}
            --launcher "$<TARGET_FILE:${tdur_ask_variant}>"
            --witness "$<TARGET_FILE:tdur_crash_witness>"
            --dll "$<TARGET_FILE:tdur_observation_proxy>"
            --reader "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/minidump_summary.py"
            --loader-dll "$<TARGET_FILE:tdur_loader_hold>"
            --work-root "${CMAKE_BINARY_DIR}/crash-launcher-fixture")
    set_tests_properties(crash_launcher_hang_ui_ask_${tdur_ask_answer}
        PROPERTIES TIMEOUT 150 RUN_SERIAL TRUE)
endforeach()
