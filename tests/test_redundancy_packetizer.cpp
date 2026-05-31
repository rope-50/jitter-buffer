// Deterministic coverage of the FEC packetizer/depacketizer round trip under
// simulated UDP loss. No sockets: packets are passed as structs, and "loss" is
// simply not handing a packet to the depacketizer.

#include <sw/redundancy_packetizer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

constexpr std::size_t kBurstSamples = 4;

sw::Burst marker_burst(std::int16_t marker) {
    return sw::Burst(kBurstSamples, marker);
}

bool is_silence(const sw::Burst& b) {
    for (std::int16_t s : b) {
        if (s != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("a packet repeats up to `redundancy` recent bursts", "[fec]") {
    sw::RedundancyPacketizer tx(kBurstSamples, 3);

    auto p0 = tx.packetize(marker_burst(10));
    REQUIRE(p0.newest_seq == 0);
    REQUIRE(p0.count == 1); // nothing to repeat yet
    REQUIRE(p0.samples.size() == kBurstSamples);

    (void)tx.packetize(marker_burst(20));
    auto p2 = tx.packetize(marker_burst(30));
    REQUIRE(p2.newest_seq == 2);
    REQUIRE(p2.count == 3); // window now holds seqs 0, 1, 2
    REQUIRE(p2.samples.size() == 3 * kBurstSamples);
    REQUIRE(p2.samples.front() == 10); // oldest in window (seq 0)
    REQUIRE(p2.samples.back() == 30);  // newest in window (seq 2)
}

TEST_CASE("lossless transport delivers every burst in order", "[fec]") {
    sw::RedundancyPacketizer tx(kBurstSamples, 3);
    sw::RedundancyDepacketizer rx(kBurstSamples, 3);

    std::vector<sw::Burst> out;
    for (std::int16_t i = 0; i < 5; ++i) {
        auto pkt = tx.packetize(marker_burst(static_cast<std::int16_t>(i + 1)));
        for (auto& b : rx.receive(pkt)) {
            out.push_back(std::move(b));
        }
    }

    REQUIRE(out.size() == 5);
    for (std::int16_t i = 0; i < 5; ++i) {
        REQUIRE(out[static_cast<std::size_t>(i)].front() == static_cast<std::int16_t>(i + 1));
    }
    REQUIRE(rx.lost_count() == 0);
}

TEST_CASE("an isolated loss is recovered from a redundant copy", "[fec]") {
    sw::RedundancyPacketizer tx(kBurstSamples, 3);
    sw::RedundancyDepacketizer rx(kBurstSamples, 3);

    std::vector<sw::AudioPacket> packets;
    for (std::int16_t i = 0; i < 5; ++i) {
        packets.push_back(tx.packetize(marker_burst(static_cast<std::int16_t>(i + 1))));
    }

    std::vector<sw::Burst> out;
    for (std::size_t i = 0; i < packets.size(); ++i) {
        if (i == 2) {
            continue; // drop the packet whose newest_seq == 2
        }
        for (auto& b : rx.receive(packets[i])) {
            out.push_back(std::move(b));
        }
    }

    // Burst with seq 2 (marker 3) is also carried by packets 3 and 4, so it is
    // recovered intact and nothing is lost.
    REQUIRE(rx.lost_count() == 0);
    REQUIRE(out.size() == 5);
    REQUIRE(out[2].front() == 3);
}

TEST_CASE("redundancy consecutive losses leave one unrecoverable gap", "[fec]") {
    sw::RedundancyPacketizer tx(kBurstSamples, 3);
    sw::RedundancyDepacketizer rx(kBurstSamples, 3);

    std::vector<sw::AudioPacket> packets;
    for (std::int16_t i = 0; i < 7; ++i) {
        packets.push_back(tx.packetize(marker_burst(static_cast<std::int16_t>(i + 1))));
    }

    // Drop packets 2, 3 and 4. Seq 2 is carried only by those three, so it is
    // gone for good; every other burst survives in some delivered packet.
    std::vector<sw::Burst> out;
    for (std::size_t i = 0; i < packets.size(); ++i) {
        if (i == 2 || i == 3 || i == 4) {
            continue;
        }
        for (auto& b : rx.receive(packets[i])) {
            out.push_back(std::move(b));
        }
    }

    REQUIRE(out.size() == 7);     // stream stays positionally aligned
    REQUIRE(rx.lost_count() == 1); // exactly the one gap
    REQUIRE(is_silence(out[2]));   // seq 2 came out as silence
    REQUIRE(out[0].front() == 1);
    REQUIRE(out[1].front() == 2);
    REQUIRE(out[3].front() == 4);
    REQUIRE(out[6].front() == 7);
}

TEST_CASE("out-of-order arrival within the window still recovers", "[fec]") {
    sw::RedundancyPacketizer tx(kBurstSamples, 3);
    sw::RedundancyDepacketizer rx(kBurstSamples, 3);

    std::vector<sw::AudioPacket> packets;
    for (std::int16_t i = 0; i < 4; ++i) {
        packets.push_back(tx.packetize(marker_burst(static_cast<std::int16_t>(i + 1))));
    }

    // Deliver in a shuffled order: 1, 0, 3, 2.
    std::vector<sw::Burst> out;
    for (std::size_t idx : {std::size_t{1}, std::size_t{0}, std::size_t{3}, std::size_t{2}}) {
        for (auto& b : rx.receive(packets[idx])) {
            out.push_back(std::move(b));
        }
    }

    REQUIRE(rx.lost_count() == 0);
    REQUIRE(out.size() == 4);
    for (std::int16_t i = 0; i < 4; ++i) {
        REQUIRE(out[static_cast<std::size_t>(i)].front() == static_cast<std::int16_t>(i + 1));
    }
}
