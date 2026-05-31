// Deterministic, single-threaded coverage of sw::JitterBuffer's playout policy:
// warm-up gating, normal delivery, underrun handling, and overflow skip. The
// drift logic is a pure function of the push/pull sequence, so no threads are
// needed to exercise it.

#include <sw/jitter_buffer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace {

// A block whose every sample carries the same marker value, so we can identify
// which block came out of a pull.
std::vector<std::int16_t> marker_block(std::size_t samples, std::int16_t marker) {
    return std::vector<std::int16_t>(samples, marker);
}

bool all_equal(std::span<const std::int16_t> data, std::int16_t value) {
    for (std::int16_t s : data) {
        if (s != value) {
            return false;
        }
    }
    return true;
}

sw::JitterBuffer::Config small_config() {
    // block_samples = 4 frames * 2 ch = 8; ring rounds 6*8=48 up to 64 samples.
    // warm-up at 0.25 -> needs >= 16 samples (2 blocks). high water 0.80 ->
    // triggers above 51 samples (7 blocks = 56 = 0.875).
    return sw::JitterBuffer::Config{
        .frames_per_block = 4, .channels = 2, .capacity_blocks = 6};
}

} // namespace

TEST_CASE("block_samples reflects frames times channels", "[jitter]") {
    sw::JitterBuffer jb(small_config());
    REQUIRE(jb.block_samples() == 8);
}

TEST_CASE("pull stays in warm-up until the fill threshold is reached", "[jitter]") {
    sw::JitterBuffer jb(small_config());
    const std::size_t bs = jb.block_samples();
    std::vector<std::int16_t> out(bs, 123);

    // One block buffered is below the 0.25 warm-up threshold (8/64 = 0.125).
    REQUIRE(jb.push(marker_block(bs, 50)));
    REQUIRE(jb.pull(out) == sw::PlaybackAction::Warmup);
    REQUIRE(all_equal(out, 0)); // warm-up serves silence

    // A second block crosses the threshold (16/64 = 0.25); playback begins.
    REQUIRE(jb.push(marker_block(bs, 60)));
    REQUIRE(jb.pull(out) == sw::PlaybackAction::Normal);
}

TEST_CASE("normal delivery returns buffered blocks in FIFO order", "[jitter]") {
    sw::JitterBuffer jb(small_config());
    const std::size_t bs = jb.block_samples();
    std::vector<std::int16_t> out(bs, 0);

    REQUIRE(jb.push(marker_block(bs, 11))); // block A
    REQUIRE(jb.push(marker_block(bs, 22))); // block B (crosses warm-up)

    REQUIRE(jb.pull(out) == sw::PlaybackAction::Normal);
    REQUIRE(all_equal(out, 11)); // A first
    REQUIRE(jb.pull(out) == sw::PlaybackAction::Normal);
    REQUIRE(all_equal(out, 22)); // then B
}

TEST_CASE("draining the buffer yields an underrun and re-arms warm-up", "[jitter]") {
    sw::JitterBuffer jb(small_config());
    const std::size_t bs = jb.block_samples();
    std::vector<std::int16_t> out(bs, 0);

    REQUIRE(jb.push(marker_block(bs, 11)));
    REQUIRE(jb.push(marker_block(bs, 22)));
    REQUIRE(jb.pull(out) == sw::PlaybackAction::Normal);
    REQUIRE(jb.pull(out) == sw::PlaybackAction::Normal);

    // Buffer empty now: underrun and silence.
    std::fill(out.begin(), out.end(), std::int16_t{99});
    REQUIRE(jb.pull(out) == sw::PlaybackAction::Underrun);
    REQUIRE(all_equal(out, 0));

    // After an underrun the gate re-arms: a single fresh block is not enough to
    // resume immediately, it must rebuild the cushion.
    REQUIRE(jb.push(marker_block(bs, 33)));
    REQUIRE(jb.pull(out) == sw::PlaybackAction::Warmup);
}

TEST_CASE("an over-full buffer drops one block to resync", "[jitter]") {
    sw::JitterBuffer jb(small_config());
    const std::size_t bs = jb.block_samples();
    std::vector<std::int16_t> out(bs, 0);

    // Push 7 blocks: 56/64 = 0.875, above the 0.80 high-water mark.
    for (std::int16_t i = 0; i < 7; ++i) {
        REQUIRE(jb.push(marker_block(bs, static_cast<std::int16_t>(i + 1))));
    }
    REQUIRE(jb.fill_ratio() > 0.80f);

    // First pull activates, sees the buffer over high water, drops block 1 and
    // delivers block 2.
    REQUIRE(jb.pull(out) == sw::PlaybackAction::Skipped);
    REQUIRE(all_equal(out, 2)); // block marked 1 was dropped
}

TEST_CASE("push rejects wrong-sized blocks and refuses to overflow", "[jitter]") {
    sw::JitterBuffer jb(small_config());
    const std::size_t bs = jb.block_samples();

    REQUIRE_FALSE(jb.push(marker_block(bs - 1, 1))); // wrong size
    REQUIRE_FALSE(jb.push(marker_block(bs + 1, 1))); // wrong size

    // Fill to capacity (8 blocks of 8 = 64 samples) then expect rejection.
    int accepted = 0;
    for (int i = 0; i < 20; ++i) {
        if (jb.push(marker_block(bs, static_cast<std::int16_t>(i + 1)))) {
            ++accepted;
        }
    }
    REQUIRE(accepted == 8); // exactly capacity / block_samples blocks fit
    REQUIRE_FALSE(jb.push(marker_block(bs, 1)));
}

TEST_CASE("reset clears buffered audio and returns to warm-up", "[jitter]") {
    sw::JitterBuffer jb(small_config());
    const std::size_t bs = jb.block_samples();
    std::vector<std::int16_t> out(bs, 0);

    REQUIRE(jb.push(marker_block(bs, 11)));
    REQUIRE(jb.push(marker_block(bs, 22)));
    jb.reset();
    REQUIRE(jb.fill_ratio() == 0.0f);

    // One block after reset is below warm-up again.
    REQUIRE(jb.push(marker_block(bs, 33)));
    REQUIRE(jb.pull(out) == sw::PlaybackAction::Warmup);
}
