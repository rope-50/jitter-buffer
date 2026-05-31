// Microbenchmarks for sw::SpscRingBuffer against a conventional mutex-guarded
// bounded queue. Two angles:
//
//   * Uncontended cost: the amortized cost of a push+pop pair on one thread.
//     This isolates the per-operation overhead, a masked index and two atomic
//     stores with release/acquire ordering, versus a mutex lock/unlock pair.
//
//   * Real SPSC throughput: a dedicated producer thread and consumer thread
//     handing a large run of items across the buffer, which is the workload the
//     structure actually exists for.

#include <sw/spsc_ring_buffer.hpp>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>

namespace {

// Minimal bounded SPSC-capable queue guarded by a mutex, as the baseline.
template <class T>
class MutexQueue {
public:
    explicit MutexQueue(std::size_t capacity) : capacity_(capacity) {}

    bool try_push(const T& v) {
        std::lock_guard<std::mutex> lock(m_);
        if (q_.size() >= capacity_) {
            return false;
        }
        q_.push(v);
        return true;
    }

    bool try_pop(T& out) {
        std::lock_guard<std::mutex> lock(m_);
        if (q_.empty()) {
            return false;
        }
        out = q_.front();
        q_.pop();
        return true;
    }

private:
    std::size_t capacity_;
    std::mutex m_;
    std::queue<T> q_;
};

} // namespace

// ---- Uncontended push+pop cost --------------------------------------------

static void BM_Spsc_PushPop(benchmark::State& state) {
    sw::SpscRingBuffer<std::uint64_t> rb(1024);
    std::uint64_t v = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(rb.try_push(v));
        std::uint64_t out = 0;
        benchmark::DoNotOptimize(rb.try_pop(out));
        benchmark::ClobberMemory();
        ++v;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Spsc_PushPop);

static void BM_MutexQueue_PushPop(benchmark::State& state) {
    MutexQueue<std::uint64_t> q(1024);
    std::uint64_t v = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(q.try_push(v));
        std::uint64_t out = 0;
        benchmark::DoNotOptimize(q.try_pop(out));
        benchmark::ClobberMemory();
        ++v;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MutexQueue_PushPop);

// ---- Two-thread throughput -------------------------------------------------

static void BM_Spsc_Throughput(benchmark::State& state) {
    constexpr std::uint64_t kItems = 1'000'000;
    sw::SpscRingBuffer<std::uint64_t> rb(1024);
    for (auto _ : state) {
        std::thread producer([&] {
            for (std::uint64_t i = 0; i < kItems;) {
                if (rb.try_push(i)) {
                    ++i;
                }
            }
        });
        std::uint64_t received = 0;
        std::uint64_t out = 0;
        while (received < kItems) {
            if (rb.try_pop(out)) {
                ++received;
            }
        }
        producer.join();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kItems));
}
BENCHMARK(BM_Spsc_Throughput)->UseRealTime();

static void BM_MutexQueue_Throughput(benchmark::State& state) {
    constexpr std::uint64_t kItems = 1'000'000;
    MutexQueue<std::uint64_t> q(1024);
    for (auto _ : state) {
        std::thread producer([&] {
            for (std::uint64_t i = 0; i < kItems;) {
                if (q.try_push(i)) {
                    ++i;
                }
            }
        });
        std::uint64_t received = 0;
        std::uint64_t out = 0;
        while (received < kItems) {
            if (q.try_pop(out)) {
                ++received;
            }
        }
        producer.join();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kItems));
}
BENCHMARK(BM_MutexQueue_Throughput)->UseRealTime();

BENCHMARK_MAIN();
