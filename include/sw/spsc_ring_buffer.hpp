// SPDX-License-Identifier: MIT
//
// sw::SpscRingBuffer<T>
// ---------------------
// A bounded, lock-free, single-producer / single-consumer (SPSC) ring buffer.
//
// Design notes (the "why", expanded in DESIGN.md):
//   * Wait-free on both sides: try_push / try_pop never block, never allocate,
//     never call into the OS. Safe to use from a real-time audio callback.
//   * Exactly one writer per index. The producer owns `head_`, the consumer
//     owns `tail_`. Neither ever writes the other's index. This is what makes
//     the lock-free reasoning tractable.
//   * Monotonic counters + power-of-two capacity. `head_` and `tail_` only ever
//     increase; the physical slot is `counter & mask_`. Element count is simply
//     `head_ - tail_`, so "full" and "empty" are never ambiguous and the whole
//     capacity is usable (no sacrificial slot).
//   * acquire / release pairing publishes the data write before the index move,
//     so the consumer that observes a new `head_` is guaranteed to see the bytes.
//   * `head_` and `tail_` live on separate cache lines to avoid false sharing
//     between the producer and consumer cores.

#ifndef SW_SPSC_RING_BUFFER_HPP
#define SW_SPSC_RING_BUFFER_HPP

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <type_traits>

namespace sw {

// Cache-line size used to pad the two index atoms apart. Falls back to 64 when
// the implementation does not expose the interference-size constant.
#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t cache_line_size = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t cache_line_size = 64;
#endif

// The bulk copy paths rely on a plain memcpy-style move, so the element type
// must be trivially copyable. Audio sample types (int16_t, float, ...) qualify.
template <class T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

#if defined(_MSC_VER)
#    pragma warning(push)
// C4324: the struct is padded because of the alignas() on head_/tail_. That
// padding is the entire point (it prevents false sharing), so the warning is
// expected and intentional here.
#    pragma warning(disable : 4324)
#endif

template <TriviallyCopyable T>
class SpscRingBuffer {
public:
    // Capacity is rounded up to the next power of two (minimum 2) so that the
    // slot index can be computed with a bit mask instead of a modulo.
    explicit SpscRingBuffer(std::size_t min_capacity)
        : capacity_(std::bit_ceil(min_capacity < 2 ? std::size_t{2} : min_capacity)),
          mask_(capacity_ - 1),
          storage_(std::make_unique_for_overwrite<T[]>(capacity_)) {}

    // Owns cross-thread atomics; copying or moving it would be a footgun.
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;
    SpscRingBuffer(SpscRingBuffer&&) = delete;
    SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;
    ~SpscRingBuffer() = default;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    // --------------------------------------------------------------------
    // Producer side. Call only from the single producer thread.
    // --------------------------------------------------------------------

    // Push one element. Returns false if the buffer is full.
    [[nodiscard]] bool try_push(const T& value) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= capacity_) {
            return false; // full
        }
        storage_[head & mask_] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Push as many of `src` as fit. Returns the number actually written.
    [[nodiscard]] std::size_t write(std::span<const T> src) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t free = capacity_ - (head - tail);
        const std::size_t n = std::min(src.size(), free);

        const std::size_t first_index = head & mask_;
        const std::size_t first_chunk = std::min(n, capacity_ - first_index);
        std::copy_n(src.data(), first_chunk, storage_.get() + first_index);
        std::copy_n(src.data() + first_chunk, n - first_chunk, storage_.get());

        head_.store(head + n, std::memory_order_release);
        return n;
    }

    // --------------------------------------------------------------------
    // Consumer side. Call only from the single consumer thread.
    // --------------------------------------------------------------------

    // Pop one element into `out`. Returns false if the buffer is empty.
    [[nodiscard]] bool try_pop(T& out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (head == tail) {
            return false; // empty
        }
        out = storage_[tail & mask_];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Pop up to `dst.size()` elements. Returns the number actually read.
    [[nodiscard]] std::size_t read(std::span<T> dst) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t avail = head - tail;
        const std::size_t n = std::min(dst.size(), avail);

        const std::size_t first_index = tail & mask_;
        const std::size_t first_chunk = std::min(n, capacity_ - first_index);
        std::copy_n(storage_.get() + first_index, first_chunk, dst.data());
        std::copy_n(storage_.get(), n - first_chunk, dst.data() + first_chunk);

        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

    // --------------------------------------------------------------------
    // Observers. Safe from either thread, but the result is a snapshot that
    // may be stale by the time the caller acts on it.
    // --------------------------------------------------------------------

    [[nodiscard]] std::size_t size() const noexcept {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] bool full() const noexcept { return size() >= capacity_; }

private:
    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<T[]> storage_;

    // Padded onto independent cache lines: the producer hammers head_, the
    // consumer hammers tail_, and we do not want them to ping-pong one line.
    alignas(cache_line_size) std::atomic<std::size_t> head_{0}; // owned by producer
    alignas(cache_line_size) std::atomic<std::size_t> tail_{0}; // owned by consumer
};

#if defined(_MSC_VER)
#    pragma warning(pop)
#endif

} // namespace sw

#endif // SW_SPSC_RING_BUFFER_HPP
