# Design notes

This document explains the reasoning behind `sw::SpscRingBuffer<T>` and records
what changed relative to the original `SWRingBuffer` it was extracted from.

## The single-writer-per-index rule

A lock-free SPSC ring buffer is only tractable to reason about if **each index
has exactly one writer**:

- The producer owns `head`. It reads `tail` but never writes it.
- The consumer owns `tail`. It reads `head` but never writes it.

With that rule, there is never a race to *write* an index, only a read of a value
the other side is publishing. Everything else in the design follows from keeping
this invariant true.

## Monotonic counters and power-of-two capacity

`head` and `tail` are counters that only ever increase. The physical slot for a
counter is `counter & (capacity - 1)`, which requires the capacity to be a power
of two (we round up with `std::bit_ceil`).

This buys two things:

1. **No modulo.** The mask is a single AND instruction instead of a division.
2. **An unambiguous full / empty test.** The number of stored elements is simply
   `head - tail`. Empty is `head == tail`; full is `head - tail == capacity`.
   Because the counters are distinct from the physical indices, the full state
   and the empty state are never represented the same way, so the entire
   capacity is usable. There is no sacrificial slot.

The counters are `std::size_t` and are allowed to wrap; unsigned subtraction
makes `head - tail` correct across a wrap.

## Memory ordering

The data write and the index publish are paired with `release` / `acquire`:

- Producer: write the payload into the slot, then `head_.store(..., release)`.
- Consumer: `head_.load(..., acquire)`, then read the payload.

The release store and the acquire load create a happens-before edge: any consumer
that observes the new `head` is guaranteed to also observe the bytes written
before it. The symmetric pair on `tail` lets the producer safely observe that the
consumer has freed space. We deliberately avoid `seq_cst`, which would add a
global ordering the algorithm does not need.

The producer's own load of `head` (and the consumer's own load of `tail`) is
`relaxed`, because each side is the sole writer of its own index and does not
need to synchronize with itself.

## False sharing

`head` and `tail` are each annotated with `alignas(cache_line_size)` so they land
on separate cache lines. Without that, the producer writing `head` and the
consumer writing `tail` would keep invalidating the same line in each other's
cache, turning an uncontended algorithm into a cache ping-pong.

`cache_line_size` uses `std::hardware_destructive_interference_size` when the
standard library exposes it, and falls back to 64 bytes otherwise.

## Why `T` must be trivially copyable

The bulk `write` / `read` paths copy ranges of `T` directly. Constraining `T` to
be trivially copyable (via a C++20 concept) keeps those copies valid and cheap,
and matches every audio sample type we care about (`int16_t`, `float`, small POD
frame structs).

## What changed from the original `SWRingBuffer`

The original lives in [`../legacy/`](../legacy/). The most important differences:

| Original `SWRingBuffer` | `sw::SpscRingBuffer<T>` |
|---|---|
| `head` advanced from several call sites (append, render, mixer) | One writer per index, enforced by API shape |
| `head == tail` meant "full" in one place and "empty" in another | Single unambiguous invariant via monotonic counters |
| `int` indices with `% bufferSize` | `std::size_t` counters with a power-of-two mask |
| Default `seq_cst` on every atomic op | Explicit, documented `acquire` / `release` (and `relaxed` self-loads) |
| `head` and `tail` adjacent in one cache line | Padded onto separate cache lines |
| Manual `new[]` / `delete[]`, copyable by default (double-free risk) | RAII storage, copy and move deleted |
| `printf` on the audio path | Hot path does no I/O |
| Specialized to `int16_t`, audio and UDP logic mixed in | Generic core; audio and FEC live in separate layers |

The audio-specific behavior (prebuffering, drift compensation) and the network
redundancy packetizer are not lost; they move up into `sw::JitterBuffer` and
`sw::RedundancyPacketizer`, where they can be tested on their own.
