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

add_executable(jitter_benchmarks bench_spsc.cpp)
target_link_libraries(jitter_benchmarks PRIVATE
    jitter::buffer
    jitter_project_options
    benchmark::benchmark)

find_package(Threads REQUIRED)
target_link_libraries(jitter_benchmarks PRIVATE Threads::Threads)
