// SPDX-License-Identifier: MIT
//
// sw::RedundancyPacketizer / sw::RedundancyDepacketizer
// ----------------------------------------------------
// Forward error correction (FEC) for audio carried over an unreliable datagram
// transport such as UDP.
//
// Why redundancy instead of retransmission: in real-time audio you cannot ask
// for a lost packet again, the replacement would arrive after its playout
// deadline and be useless. The defense that works is to send each audio burst
// more than once. Every outgoing packet carries the newest burst plus copies of
// the previous (redundancy - 1) bursts, each tagged with a sequence number. A
// receiver only loses a burst if `redundancy` packets in a row are dropped.
//
// This layer is pure logic: it builds and consumes packet structs, it never
// touches a socket. That keeps it deterministic and lets the recovery behavior
// be tested against simulated loss patterns.

#ifndef SW_REDUNDANCY_PACKETIZER_HPP
#define SW_REDUNDANCY_PACKETIZER_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <vector>

namespace sw {

// One burst worth of interleaved int16_t audio.
using Burst = std::vector<std::int16_t>;

// A packet as it would go on the wire: the sequence number of the newest burst,
// how many bursts it actually carries (fewer than `redundancy` while the stream
// is warming up), and the burst payloads laid out oldest-first.
struct AudioPacket {
    std::uint32_t newest_seq = 0;
    std::uint32_t count = 0;
    std::vector<std::int16_t> samples; // count * burst_samples, oldest-first
};

// Sender side: turn a stream of bursts into a stream of redundant packets.
class RedundancyPacketizer {
public:
    RedundancyPacketizer(std::size_t burst_samples, std::size_t redundancy)
        : burst_samples_(burst_samples), redundancy_(redundancy == 0 ? 1 : redundancy) {}

    // Take the next burst (must be exactly burst_samples long) and produce the
    // packet to send. The returned packet repeats up to `redundancy` recent
    // bursts so the receiver can survive isolated losses.
    AudioPacket packetize(std::span<const std::int16_t> burst) {
        const std::uint32_t seq = next_seq_++;

        history_.emplace_back(burst.begin(), burst.end());
        while (history_.size() > redundancy_) {
            history_.pop_front();
        }

        AudioPacket packet;
        packet.newest_seq = seq;
        packet.count = static_cast<std::uint32_t>(history_.size());
        packet.samples.reserve(history_.size() * burst_samples_);
        for (const Burst& b : history_) { // oldest-first
            packet.samples.insert(packet.samples.end(), b.begin(), b.end());
        }
        return packet;
    }

    [[nodiscard]] std::uint32_t next_sequence() const noexcept { return next_seq_; }

private:
    std::size_t burst_samples_;
    std::size_t redundancy_;
    std::uint32_t next_seq_ = 0;
    std::deque<Burst> history_;
};

// Receiver side: reassemble the ordered burst stream from packets that may
// arrive with gaps (loss) or out of order. A burst that no surviving packet
// carried is emitted as silence and counted, so the output stays positionally
// aligned with the original stream.
class RedundancyDepacketizer {
public:
    RedundancyDepacketizer(std::size_t burst_samples, std::size_t redundancy)
        : burst_samples_(burst_samples), redundancy_(redundancy == 0 ? 1 : redundancy) {}

    // Feed one received packet. Returns the bursts that became deliverable on
    // this call, in stream order. Lost bursts appear as all-zero bursts.
    std::vector<Burst> receive(const AudioPacket& packet) {
        // Unpack each carried burst into the recovery store, keyed by its own
        // sequence number. Oldest-first layout means slot i has sequence
        // newest_seq - (count - 1) + i.
        for (std::uint32_t i = 0; i < packet.count; ++i) {
            const std::uint32_t seq = packet.newest_seq - (packet.count - 1) + i;
            if (seq < next_deliver_) {
                continue; // already delivered or already given up on
            }
            if (!store_.contains(seq)) {
                const auto offset = static_cast<std::size_t>(i) * burst_samples_;
                store_.emplace(seq, Burst(packet.samples.begin() + offset,
                                          packet.samples.begin() + offset + burst_samples_));
            }
        }

        if (!any_seen_ || packet.newest_seq > highest_seen_) {
            highest_seen_ = packet.newest_seq;
            any_seen_ = true;
        }

        // Deliver everything now contiguous from next_deliver_ onward. A gap is
        // declared unrecoverable once we have seen a packet beyond the last one
        // that could ever have carried it (redundancy consecutive losses).
        std::vector<Burst> delivered;
        while (true) {
            auto it = store_.find(next_deliver_);
            if (it != store_.end()) {
                delivered.push_back(std::move(it->second));
                store_.erase(it);
                ++next_deliver_;
            } else if (any_seen_ && highest_seen_ >= next_deliver_ + redundancy_) {
                delivered.emplace_back(burst_samples_, std::int16_t{0}); // silence
                ++lost_count_;
                ++next_deliver_;
            } else {
                break; // still within the recovery window; wait for more packets
            }
        }
        return delivered;
    }

    [[nodiscard]] std::uint32_t lost_count() const noexcept { return lost_count_; }

private:
    std::size_t burst_samples_;
    std::size_t redundancy_;
    std::uint32_t next_deliver_ = 0;
    std::uint32_t highest_seen_ = 0;
    bool any_seen_ = false;
    std::uint32_t lost_count_ = 0;
    std::map<std::uint32_t, Burst> store_;
};

} // namespace sw

#endif // SW_REDUNDANCY_PACKETIZER_HPP
