include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(sw_tests
    test_spsc_basic.cpp)
target_link_libraries(sw_tests PRIVATE
    sw::ringbuffer
    sw_project_options
    Catch2::Catch2WithMain)

catch_discover_tests(sw_tests)
