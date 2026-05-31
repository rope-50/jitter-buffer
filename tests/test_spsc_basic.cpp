// Single-threaded correctness of sw::SpscRingBuffer.
// Concurrency (two-thread) stress lives in test_spsc_stress.cpp (Phase 2).

#include <sw/spsc_ring_buffer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <numeric>
#include <vector>

TEST_CASE("capacity is rounded up to a power of two", "[spsc]") {
    REQUIRE(sw::SpscRingBuffer<int>(5).capacity() == 8);
    REQUIRE(sw::SpscRingBuffer<int>(8).capacity() == 8);
    REQUIRE(sw::SpscRingBuffer<int>(1).capacity() == 2); // enforced minimum
}

TEST_CASE("a fresh buffer is empty", "[spsc]") {
    sw::SpscRingBuffer<int> rb(4);
    REQUIRE(rb.empty());
    REQUIRE(rb.size() == 0);
    int out = -1;
    REQUIRE_FALSE(rb.try_pop(out));
    REQUIRE(out == -1);
}

TEST_CASE("push then pop returns the same value", "[spsc]") {
    sw::SpscRingBuffer<int> rb(4);
    REQUIRE(rb.try_push(42));
    REQUIRE(rb.size() == 1);
    int out = 0;
    REQUIRE(rb.try_pop(out));
    REQUIRE(out == 42);
    REQUIRE(rb.empty());
}

TEST_CASE("the whole capacity is usable, then push is rejected", "[spsc]") {
    sw::SpscRingBuffer<int> rb(4);
    REQUIRE(rb.capacity() == 4);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(rb.try_push(i));
    }
    REQUIRE(rb.full());
    REQUIRE_FALSE(rb.try_push(99));
    for (int i = 0; i < 4; ++i) {
        int out = -1;
        REQUIRE(rb.try_pop(out));
        REQUIRE(out == i); // FIFO order
    }
    REQUIRE(rb.empty());
}

TEST_CASE("physical wrap-around preserves FIFO order", "[spsc]") {
    sw::SpscRingBuffer<int> rb(4);
    int out = 0;
    REQUIRE(rb.try_push(1));
    REQUIRE(rb.try_push(2));
    REQUIRE(rb.try_push(3));
    REQUIRE(rb.try_pop(out));
    REQUIRE(out == 1);
    REQUIRE(rb.try_pop(out));
    REQUIRE(out == 2);
    // tail is now ahead of zero; these pushes wrap the physical index
    REQUIRE(rb.try_push(4));
    REQUIRE(rb.try_push(5));
    REQUIRE(rb.try_push(6));
    for (int expected : {3, 4, 5, 6}) {
        REQUIRE(rb.try_pop(out));
        REQUIRE(out == expected);
    }
    REQUIRE(rb.empty());
}

TEST_CASE("bulk write and read split correctly across the wrap", "[spsc][bulk]") {
    sw::SpscRingBuffer<int> rb(8);

    std::vector<int> src(6);
    std::iota(src.begin(), src.end(), 0); // 0..5
    REQUIRE(rb.write(src) == 6);

    std::array<int, 4> dst{};
    REQUIRE(rb.read(dst) == 4);
    REQUIRE(dst == std::array<int, 4>{0, 1, 2, 3});

    std::vector<int> src2(6);
    std::iota(src2.begin(), src2.end(), 6); // 6..11, forces a wrap
    REQUIRE(rb.write(src2) == 6);

    std::vector<int> out(8);
    REQUIRE(rb.read(out) == 8);
    std::vector<int> expected(8);
    std::iota(expected.begin(), expected.end(), 4); // 4..11
    REQUIRE(out == expected);
    REQUIRE(rb.empty());
}

TEST_CASE("bulk write returns a partial count when space runs out", "[spsc][bulk]") {
    sw::SpscRingBuffer<int> rb(4);
    std::vector<int> src(10);
    std::iota(src.begin(), src.end(), 0);
    REQUIRE(rb.write(src) == 4); // only the capacity fits
    REQUIRE(rb.full());
}
