add_executable(tdur_observe_launch observe_launch.cpp)
target_include_directories(tdur_observe_launch PRIVATE ../../include)
target_compile_features(tdur_observe_launch PRIVATE cxx_std_23)
target_compile_options(tdur_observe_launch PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_compile_definitions(tdur_observe_launch PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(tdur_observe_launch PRIVATE tdur_detours bcrypt)

add_executable(tdur_launch_witness ../launcher_tests/launch_witness.cpp)
target_compile_features(tdur_launch_witness PRIVATE cxx_std_23)
target_compile_options(tdur_launch_witness PRIVATE
    /std:c++latest /W4 /WX /permissive- /utf-8 /EHsc)
target_compile_definitions(tdur_launch_witness PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(tdur_launch_witness PRIVATE user32)

add_test(NAME observation_launcher_lifecycle
    COMMAND "${Python3_EXECUTABLE}" -I
        "${CMAKE_CURRENT_SOURCE_DIR}/../launcher_tests/run_launcher_fixture.py"
        --launcher "$<TARGET_FILE:tdur_observe_launch>"
        --witness "$<TARGET_FILE:tdur_launch_witness>"
        --dll "$<TARGET_FILE:tdur_observation_proxy>"
        --work-root "${CMAKE_BINARY_DIR}/launcher-fixture")
set_tests_properties(observation_launcher_lifecycle PROPERTIES TIMEOUT 45)

add_test(NAME observation_reject_late_load
    COMMAND "${Python3_EXECUTABLE}" -I
        "${CMAKE_CURRENT_SOURCE_DIR}/../launcher_tests/run_late_load_fixture.py"
        --witness "$<TARGET_FILE:tdur_launch_witness>"
        --dll "$<TARGET_FILE:tdur_observation_proxy>"
        --work-root "${CMAKE_BINARY_DIR}/late-load-fixture")
set_tests_properties(observation_reject_late_load PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
