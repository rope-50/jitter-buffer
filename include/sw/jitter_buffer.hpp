// SPDX-License-Identifier: MIT
//
// sw::JitterBuffer
// ----------------
// Adaptive audio playout buffer built on top of SpscRingBuffer<int16_t>.
//
// It solves the two timing problems a raw ring buffer does not know about:
//
//   1. Warm-up / prebuffering. Playback does not begin until a configurable
//      fill level is reached, so the first audio callback is not served from a
//      nearly empty buffer (which would click immediately).
//
//   2. Clock-drift compensation. The producer clock (network / mixer) and the
//      consumer clock (the sound card) never tick at exactly the same rate. Over
//      time the buffer fill drifts up or down. JitterBuffer watches the fill
//      level on each pull and resynchronises: if the buffer is running too full
//      it drops one block to catch up; if it has run dry it serves silence
//      instead of stale or invalid data.
//
// The hot path (pull) does no logging, no allocation and no locking. Instead of
// printing, it RETURNS a PlaybackAction describing what it did, so the caller
// (or a test) can observe drift handling without touching the audio thread's
// timing.
//
// Audio layout: interleaved int16_t samples. One "block" is the amount of audio
// consumed per callback: frames_per_block * channels samples.

#ifndef SW_JITTER_BUFFER_HPP
#define SW_JITTER_BUFFER_HPP

#include <sw/spsc_ring_buffer.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sw {

// What pull() did on this call. Lets the caller observe drift handling without
// any I/O on the audio thread.
enum class PlaybackAction {
    Warmup,   // still prebuffering; output filled with silence
    Normal,   // a block was delivered normally
    Skipped,  // buffer too full; one block dropped to resync, then delivered
    Underrun, // not enough buffered data; output filled with silence
};

class JitterBuffer {
public:
    struct Config {
        std::size_t frames_per_block;     // frames consumed per pull()
        std::size_t channels = 2;         // interleaved channel count
        std::size_t capacity_blocks = 6;  // ring is sized to hold this many blocks
        float warmup_fill = 0.25f;        // begin playback once fill reaches this
        float high_water = 0.80f;         // above this fill, drop a block to resync
    };

    explicit JitterBuffer(const Config& cfg)
        : block_samples_(cfg.frames_per_block * cfg.channels),
          warmup_fill_(cfg.warmup_fill),
          high_water_(cfg.high_water),
          ring_(cfg.capacity_blocks * block_samples_) {}

    // Number of int16_t samples in one block.
    [[nodiscard]] std::size_t block_samples() const noexcept { return block_samples_; }

    // Fraction of the ring currently occupied, in [0, 1].
    [[nodiscard]] float fill_ratio() const noexcept {
        return static_cast<float>(ring_.size()) / static_cast<float>(ring_.capacity());
    }

    // --------------------------------------------------------------------
    // Producer side (network / mixer thread).
    // --------------------------------------------------------------------

    // Enqueue one block of interleaved audio. Returns false if it did not fit
    // (the producer is outrunning the consumer); the caller decides whether to
    // drop it. Partial writes are not committed: either the whole block goes in
    // or nothing does.
    [[nodiscard]] bool push(std::span<const std::int16_t> block) noexcept {
        if (block.size() != block_samples_) {
            return false;
        }
        if (ring_.size() + block_samples_ > ring_.capacity()) {
            return false; // not enough room for a whole block
        }
        return ring_.write(block) == block_samples_;
    }

    // --------------------------------------------------------------------
    // Consumer side (audio callback). Call from the single consumer thread.
    // --------------------------------------------------------------------

    // Fill `out` (one block) with the next audio, applying warm-up and drift
    // policy. `out.size()` must equal block_samples(). Returns what it did.
    PlaybackAction pull(std::span<std::int16_t> out) noexcept {
        if (out.size() != block_samples_) {
            return PlaybackAction::Underrun;
        }

        // Warm-up gate: stay silent until the buffer has filled enough to give
        // drift compensation room to work in both directions.
        if (!active_) {
            if (fill_ratio() < warmup_fill_) {
                fill_silence(out);
                return PlaybackAction::Warmup;
            }
            active_ = true;
        }

        PlaybackAction action = PlaybackAction::Normal;

        // Overflow side of drift: producer outran consumer. Drop one block so we
        // catch up. One dropped block is a far smaller artifact than letting the
        // buffer saturate and overwrite continuously.
        if (fill_ratio() > high_water_) {
            ring_.read(out); // discard one block into the scratch we are about to overwrite
            action = PlaybackAction::Skipped;
        }

        // Underflow side of drift: not enough buffered for a full block. Serve
        // silence rather than a short or stale read, and re-arm warm-up so we
        // rebuild a cushion before resuming.
        if (ring_.size() < block_samples_) {
            fill_silence(out);
            active_ = false;
            return PlaybackAction::Underrun;
        }

        ring_.read(out);
        return action;
    }

    // Drop all buffered audio and return to the warm-up state (e.g. on device
    // change). Not safe to call concurrently with push/pull.
    void reset() noexcept {
        std::int16_t scratch;
        while (ring_.try_pop(scratch)) {
        }
        active_ = false;
    }

private:
    static void fill_silence(std::span<std::int16_t> out) noexcept {
        std::fill(out.begin(), out.end(), std::int16_t{0});
    }

    const std::size_t block_samples_;
    const float warmup_fill_;
    const float high_water_;
    bool active_ = false;
    SpscRingBuffer<std::int16_t> ring_;
};

} // namespace sw

#endif // SW_JITTER_BUFFER_HPP
