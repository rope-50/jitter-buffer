include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(jitter_tests
    test_spsc_basic.cpp
    test_spsc_stress.cpp
    test_jitter_buffer.cpp
    test_redundancy_packetizer.cpp)
target_link_libraries(jitter_tests PRIVATE
    jitter::buffer
    jitter_project_options
    Catch2::Catch2WithMain)

find_package(Threads REQUIRED)
target_link_libraries(jitter_tests PRIVATE Threads::Threads)

catch_discover_tests(jitter_tests)
