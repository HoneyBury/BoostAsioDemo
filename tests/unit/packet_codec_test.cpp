#include "net/packet_codec.h"
#include "net/protocol.h"

#include <gtest/gtest.h>

TEST(PacketCodecTest, EncodeAndDecodeRoundTrip) {
    const auto encoded = net::packet::encode(1001, 77, -3, "payload");

    net::packet::LengthHeader header{};
    std::copy(encoded.begin(), encoded.begin() + 4, header.begin());
    const auto length = net::packet::decode_length(header);

    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto decoded = net::packet::decode_payload(payload);

    EXPECT_EQ(length, payload.size());
    EXPECT_EQ(decoded.message_id, 1001);
    EXPECT_EQ(decoded.request_id, 77U);
    EXPECT_EQ(decoded.error_code, -3);
    EXPECT_EQ(decoded.body, "payload");
}

TEST(PacketCodecTest, DecodePayloadRejectsTooShortPacket) {
    std::vector<char> invalid_payload(1, '\0');
    EXPECT_THROW((void)net::packet::decode_payload(invalid_payload), std::invalid_argument);
}

// ─── Protocol version tests ──────────────────────────────────────────

TEST(PacketCodecTest, EncodeSetsDefaultProtocolVersion) {
    const auto encoded = net::packet::encode(2001, 1, 0, "test");
    // Version byte is at offset 4 (right after 4-byte length header)
    const auto version = static_cast<std::uint8_t>(encoded[4]);
    EXPECT_EQ(version, net::protocol::kProtocolVersion);
}

TEST(PacketCodecTest, DecodePreservesProtocolVersion) {
    const auto encoded = net::packet::encode(2001, 1, 0, "test",
                                             net::packet::flags::kNone, 0,
                                             net::protocol::kProtocolVersion);
    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto decoded = net::packet::decode_payload(payload);
    EXPECT_EQ(decoded.version, net::protocol::kProtocolVersion);
}

TEST(PacketCodecTest, ParsePacketViewExtractsVersion) {
    const auto encoded = net::packet::encode(100, 42, -5, "hello");
    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto view = net::packet::parse_packet_view(payload);
    EXPECT_EQ(view.version, net::protocol::kProtocolVersion);
    EXPECT_EQ(view.message_id, 100);
    EXPECT_EQ(view.request_id, 42U);
    EXPECT_EQ(view.error_code, -5);
}

TEST(PacketCodecTest, CheckVersionAcceptsCurrentVersion) {
    net::packet::PacketView view;
    view.version = net::protocol::kProtocolVersion;
    EXPECT_EQ(net::packet::check_version(view), 0);
}

TEST(PacketCodecTest, CheckVersionRejectsUnsupportedVersion) {
    net::packet::PacketView view;
    view.version = 99;  // Unsupported version
    EXPECT_NE(net::packet::check_version(view), 0);
}

TEST(PacketCodecTest, CheckVersionRejectsZeroVersion) {
    net::packet::PacketView view;
    view.version = 0;  // Below minimum
    EXPECT_NE(net::packet::check_version(view), 0);
}

// ─── Sequence number tests ───────────────────────────────────────────

TEST(PacketCodecTest, EncodeDecodeSequenceNumber) {
    const auto encoded = net::packet::encode(1001, 77, 0, "seq_test",
                                             net::packet::flags::kNone, 12345);
    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto decoded = net::packet::decode_payload(payload);
    EXPECT_EQ(decoded.sequence_number, 12345U);
}

TEST(PacketCodecTest, SequenceNumberDefaultsToZero) {
    const auto encoded = net::packet::encode(1001, 77, 0, "test");
    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto decoded = net::packet::decode_payload(payload);
    EXPECT_EQ(decoded.sequence_number, 0U);
}

TEST(PacketCodecTest, MaxSequenceNumberRoundTrip) {
    const auto encoded = net::packet::encode(1001, 77, 0, "test",
                                             net::packet::flags::kNone, 0xFFFFFFFF);
    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto decoded = net::packet::decode_payload(payload);
    EXPECT_EQ(decoded.sequence_number, 0xFFFFFFFF);
}

TEST(PacketCodecTest, RequestIdAndSequenceNumberAreIndependent) {
    const auto encoded = net::packet::encode(1001, 0xAAAA, 0, "test",
                                             net::packet::flags::kNone, 0xBBBB);
    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto decoded = net::packet::decode_payload(payload);
    EXPECT_EQ(decoded.request_id, 0xAAAAU);
    EXPECT_EQ(decoded.sequence_number, 0xBBBBU);
}

// ─── Fixed metadata size validation ──────────────────────────────────

TEST(PacketCodecTest, MinimumValidPayloadSize) {
    std::vector<char> payload(net::packet::kFixedMetadataSize, '\x00');
    EXPECT_NO_THROW({
        auto p = net::packet::decode_payload(payload);
        EXPECT_EQ(p.version, 0);
        EXPECT_EQ(p.body, "");
    });
}

TEST(PacketCodecTest, RejectsPayloadBelowFixedMetadataSize) {
    for (std::size_t i = 0; i < net::packet::kFixedMetadataSize; ++i) {
        std::vector<char> payload(i, '\x00');
        EXPECT_THROW(net::packet::decode_payload(payload), std::invalid_argument);
    }
}

// ─── Version negotiation payload ─────────────────────────────────────

TEST(PacketCodecTest, VersionHandshakeMessageEncodeDecode) {
    // Simulate a version request with protocol version info embedded in body
    std::string version_body = "v1";
    const auto encoded = net::packet::encode(
        net::protocol::kVersionRequest, 0, 0, version_body);
    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto decoded = net::packet::decode_payload(payload);
    EXPECT_EQ(decoded.message_id, net::protocol::kVersionRequest);
    EXPECT_EQ(decoded.body, "v1");
}
