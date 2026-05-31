# sw-ringbuffer

Header-only C++20 building blocks for moving audio between threads and across a
lossy network without glitches.

The library is three focused layers that stack on top of each other:

| Component | Role |
|---|---|
| `sw::SpscRingBuffer<T>` | Bounded, wait-free single-producer / single-consumer ring buffer. |
| `sw::JitterBuffer` | Adaptive audio playout: prebuffering plus clock-drift compensation. |
| `sw::RedundancyPacketizer` | Forward-error-correction packetizer for audio sent over UDP. |

## Why this exists

Real-time audio has a requirement that looks simple and is not: the thread that
**fills** the audio and the thread that **drains** it run on two clocks that
never agree, and the draining thread (the sound card callback) cannot be made to
wait. The moment it blocks, allocates, or takes a lock, you hear a click or a
dropout. So the structure that sits between producer and consumer has to satisfy
three constraints at once:

1. **Wait-free on the audio side.** No locks, no allocation, no system calls on
   push or pop. The audio callback runs on a deadline measured in single-digit
   milliseconds and missing it is audible.

2. **Tolerant of mismatched rates.** The input clock (a microphone, a socket, a
   mixer) and the output clock (the sound card) drift apart over minutes of
   playback. Something has to absorb that drift on purpose, instead of letting
   the buffer slowly starve into silence or overflow into garbage.

3. **Tolerant of packet loss.** When audio travels over UDP you cannot ask for a
   lost packet again, the replacement would arrive too late to play. The only
   defense that works in real time is to send each chunk of audio more than
   once, so a gap can be filled from a copy that did arrive.

A textbook ring buffer solves none of these. It is either a single-threaded
convenience or, at best, a generic lock-free queue with no idea that audio has
timing or that a network drops data. This project takes the opposite stance:
each of the three problems above gets its own small, tested component, and they
compose.

### Origin

This started as one class, `SWRingBuffer`, living inside a production audio
engine (`sw-audioengine`). That version shipped and worked, but over time it had
absorbed three jobs into a single type: the ring itself, the audio timing logic,
and a network redundancy packetizer. It also had several rough edges that are
easy to miss until they bite, more than one thread advancing the same index, an
ambiguous "is it full or empty" state, and `printf` calls on the audio thread.

`sw-ringbuffer` is that idea extracted and rebuilt: split into clean layers,
given a single writer per index, explicit and documented memory ordering,
exhaustive tests (including a two-thread stress run under ThreadSanitizer), and
benchmarks against the usual alternatives. The original is preserved under
[`legacy/`](legacy/) so the before-and-after is visible in the git history and in
[`DESIGN.md`](docs/DESIGN.md).

## What you get

### `sw::SpscRingBuffer<T>` (the core)

- Wait-free `try_push` / `try_pop` and bulk `write` / `read`, none of which lock
  or allocate.
- Exactly one writer per index: the producer owns `head`, the consumer owns
  `tail`. Neither touches the other. This is what makes the lock-free reasoning
  small enough to actually verify.
- Power-of-two capacity with monotonic counters, so "full" and "empty" are never
  ambiguous and the entire capacity is usable.
- `acquire` / `release` ordering that publishes the data before the index move.
- `head` and `tail` padded onto separate cache lines to avoid false sharing.
- Generic over any trivially copyable `T` (a C++20 concept enforces it).

### `sw::JitterBuffer` (audio playout)

Wraps `SpscRingBuffer<int16_t>` and adds the audio timing brain: a prebuffer /
warm-up gate so playback does not start dry, and a drift policy that watches the
fill level and decides when to skip a block (overflow) or serve silence
(underrun) to keep the two clocks in step. The hot path does no logging or I/O,
it *returns* a `PlaybackAction` (`Warmup` / `Normal` / `Skipped` / `Underrun`)
so the caller can observe drift handling without touching the audio thread.

### `sw::RedundancyPacketizer` (network FEC)

Packs each audio burst together with copies of the previous N bursts plus a
sequence number, so a receiver (`sw::RedundancyDepacketizer`) can rebuild lost
packets from redundant copies. A burst is only lost if `redundancy` packets in a
row are dropped; an unrecoverable gap is emitted as silence so the stream stays
positionally aligned. Pure logic, no sockets, which keeps it deterministic and
easy to test against simulated loss and reordering.

## Usage

```cpp
#include <sw/spsc_ring_buffer.hpp>

sw::SpscRingBuffer<float> rb(1024); // rounded up to a power of two

// Producer thread
float sample = 0.5f;
if (!rb.try_push(sample)) {
    // buffer full, apply your overflow policy
}

// Consumer thread (e.g. the audio callback)
float out{};
if (rb.try_pop(out)) {
    // got a sample
}

// Bulk paths for block-based audio
std::array<float, 256> block{ /* ... */ };
std::size_t written = rb.write(block);     // returns how many fit
std::array<float, 256> dst{};
std::size_t read = rb.read(dst);           // returns how many were available
```

## Build and test

Requires CMake 3.21+ and a C++20 compiler. The test dependency (Catch2) is
fetched automatically.

```bash
# Configure and build (uses the preset matching your platform)
cmake --preset windows-msvc
cmake --build build --config Debug

# Run the tests
ctest --test-dir build -C Debug --output-on-failure
```

On Linux, swap the preset for `ci-gcc` or `ci-clang`, and use the `asan` / `tsan`
presets to run the suite under sanitizers.

## Status

- [x] `SpscRingBuffer<T>` core with single-threaded test coverage
- [x] Two-thread stress test (run under the `tsan` preset in CI)
- [x] `JitterBuffer` (prebuffer + drift)
- [x] `RedundancyPacketizer` (FEC) with loss/reorder recovery tests
- [ ] Benchmarks vs mutex queue
- [ ] CI matrix (gcc / clang / msvc) and sanitizer job

## License

MIT. See [LICENSE](LICENSE).
