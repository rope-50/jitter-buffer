# jitter-buffer

[![CI](https://github.com/rope-50/jitter-buffer/actions/workflows/ci.yml/badge.svg)](https://github.com/rope-50/jitter-buffer/actions/workflows/ci.yml)

Header-only C++20 building blocks for moving audio between threads and across a
lossy network without glitches.

| Component | Role |
|---|---|
| `sw::SpscRingBuffer<T>` | Bounded, wait-free single-producer / single-consumer ring buffer. |
| `sw::JitterBuffer` | Adaptive audio playout: prebuffering plus clock-drift compensation. |
| `sw::RedundancyPacketizer` | Forward-error-correction packetizer for audio sent over UDP. |

## Why this exists

Real-time audio has to keep feeding the sound card on a deadline of a few
milliseconds, and the callback that does it cannot block, allocate, or take a
lock without causing an audible glitch. That makes three problems unavoidable,
and a textbook ring buffer solves none of them:

- **Two threads, no locks.** The producer and the consumer run concurrently and
  must hand off audio without ever waiting on each other.
- **Clocks that drift.** The input and output run on independent clocks that
  slowly diverge, so something has to absorb the drift instead of starving or
  overflowing.
- **Packets that get lost.** Over UDP you cannot retransmit in time, so each
  chunk of audio is sent redundantly and gaps are filled from copies that did
  arrive.

Each problem gets its own small, tested component, and they compose.

## Components

### `sw::SpscRingBuffer<T>`

- Wait-free `try_push` / `try_pop` and bulk `write` / `read`, none of which lock
  or allocate.
- One writer per index: the producer owns `head`, the consumer owns `tail`.
- Power-of-two capacity with monotonic counters, so "full" and "empty" are never
  ambiguous and the whole capacity is usable.
- `acquire` / `release` ordering and cache-line padding to avoid false sharing.
- Generic over any trivially copyable `T` (enforced by a C++20 concept).

### `sw::JitterBuffer`

Wraps `SpscRingBuffer<int16_t>` with a warm-up gate so playback does not start
dry, and a drift policy that skips a block on overflow or serves silence on
underrun. The hot path does no I/O; it returns a `PlaybackAction`
(`Warmup` / `Normal` / `Skipped` / `Underrun`) so the caller can observe drift
handling without touching the audio thread.

### `sw::RedundancyPacketizer`

Packs each audio burst with copies of the previous N bursts plus a sequence
number, so `sw::RedundancyDepacketizer` can rebuild lost packets from redundant
copies. A burst is only lost if `redundancy` packets in a row are dropped; an
unrecoverable gap is emitted as silence to keep the stream aligned. Pure logic,
no sockets.

## Usage

```cpp
#include <sw/spsc_ring_buffer.hpp>

sw::SpscRingBuffer<float> rb(1024); // rounded up to a power of two

// Producer thread
if (!rb.try_push(0.5f)) {
    // buffer full, apply your overflow policy
}

// Consumer thread (e.g. the audio callback)
float out{};
if (rb.try_pop(out)) {
    // got a sample
}

// Bulk paths for block-based audio
std::array<float, 256> block{ /* ... */ };
std::size_t written = rb.write(block); // returns how many fit
std::array<float, 256> dst{};
std::size_t read = rb.read(dst);       // returns how many were available
```

## Build and test

Requires CMake 3.21+ and a C++20 compiler. Catch2 is fetched automatically.

```bash
cmake --preset windows-msvc          # or ci-gcc / ci-clang on Linux
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The `asan` and `tsan` presets run the suite under sanitizers. Benchmarks:

```bash
cmake -S . -B build -DJITTER_BUFFER_BUILD_BENCHMARKS=ON
cmake --build build --config Release --target jitter_benchmarks
./build/benchmarks/Release/jitter_benchmarks
```

## Benchmarks

`SpscRingBuffer` versus a `std::mutex` + `std::queue` baseline (16-core x86-64,
MSVC Release; the ratio matters more than the absolute figures):

| Benchmark | `SpscRingBuffer` | Mutex queue | Speedup |
|---|---|---|---|
| Uncontended push + pop | 2.5 ns | 35.3 ns | ~14x |
| 1M-item two-thread throughput | 7.2 ms | 148 ms | ~20x |

The throughput gap is the larger one because the mutex serializes the producer
and consumer, while the lock-free buffer lets them run in parallel.

## License

MIT. See [LICENSE](LICENSE).
