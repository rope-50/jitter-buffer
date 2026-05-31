include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(benchmark)

add_executable(sw_benchmarks bench_spsc.cpp)
target_link_libraries(sw_benchmarks PRIVATE
    sw::ringbuffer
    sw_project_options
    benchmark::benchmark)

find_package(Threads REQUIRED)
target_link_libraries(sw_benchmarks PRIVATE Threads::Threads)
