// Two-thread concurrency stress for sw::SpscRingBuffer.
//
// One producer thread pushes a known sequence; one consumer thread drains it.
// The test asserts the two SPSC guarantees that single-threaded tests cannot
// reach: nothing is lost, nothing is duplicated, and FIFO order is preserved
// across millions of hand-offs. Run this build under ThreadSanitizer (the
// `tsan` preset) to also catch any data race in the index publishing.

#include <sw/spsc_ring_buffer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <thread>
#include <vector>

TEST_CASE("single-element push/pop survives millions of hand-offs", "[spsc][stress]") {
    constexpr std::uint64_t kCount = 5'000'000;
    sw::SpscRingBuffer<std::uint64_t> rb(1024);

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kCount;) {
            if (rb.try_push(i)) {
                ++i;
            } // else buffer full: spin until the consumer frees a slot
        }
    });

    std::uint64_t received = 0;
    std::uint64_t expected = 0;
    bool order_ok = true;
    while (received < kCount) {
        std::uint64_t value = 0;
        if (rb.try_pop(value)) {
            if (value != expected) {
                order_ok = false;
            }
            ++expected;
            ++received;
        }
    }

    producer.join();
    REQUIRE(order_ok);          // every value arrived exactly once, in order
    REQUIRE(received == kCount); // nothing lost
    REQUIRE(rb.empty());
}

TEST_CASE("bulk write/read survives concurrent block transfers", "[spsc][stress][bulk]") {
    constexpr std::uint64_t kCount = 4'000'000;
    constexpr std::size_t kBlock = 128;
    sw::SpscRingBuffer<std::uint64_t> rb(1024);

    std::thread producer([&] {
        std::vector<std::uint64_t> block(kBlock);
        std::uint64_t next = 0;
        while (next < kCount) {
            const std::size_t n =
                static_cast<std::size_t>(std::min<std::uint64_t>(kBlock, kCount - next));
            for (std::size_t k = 0; k < n; ++k) {
                block[k] = next + k;
            }
            std::size_t written = 0;
            while (written < n) {
                written += rb.write({block.data() + written, n - written});
            }
            next += n;
        }
    });

    std::vector<std::uint64_t> dst(kBlock);
    std::uint64_t received = 0;
    std::uint64_t expected = 0;
    bool order_ok = true;
    while (received < kCount) {
        const std::size_t got = rb.read(dst);
        for (std::size_t k = 0; k < got; ++k) {
            if (dst[k] != expected) {
                order_ok = false;
            }
            ++expected;
        }
        received += got;
    }

    producer.join();
    REQUIRE(order_ok);
    REQUIRE(received == kCount);
    REQUIRE(rb.empty());
}
