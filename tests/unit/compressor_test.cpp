#include "net/packet_compressor.h"

#include <gtest/gtest.h>

TEST(PacketCompressorTest, SmallBodyNotCompressed) {
    EXPECT_FALSE(net::packet::should_compress(100));
    EXPECT_TRUE(net::packet::should_compress(1000));
}

TEST(PacketCompressorTest, CompressDecompressRoundTrip) {
    std::string body(2000, 'Z');
    auto compressed = net::packet::compress_body(body);
    EXPECT_GT(compressed.size(), 4u);

    auto decompressed = net::packet::decompress_body(compressed);
    EXPECT_EQ(decompressed, body);
}

TEST(PacketCompressorTest, DecompressEmptyAndShort) {
    EXPECT_EQ(net::packet::decompress_body(""), "");
    EXPECT_EQ(net::packet::decompress_body("ab"), "ab");
}

TEST(PacketCompressorTest, FullPipelineEncodeWithCompressFlag) {
    // Simulate what Session does: compress body, set flag, encode, decode, decompress
    std::string body(2000, 'X');
    ASSERT_TRUE(net::packet::should_compress(body.size()));

    auto compressed = net::packet::compress_body(body);
    EXPECT_NE(compressed, body);

    auto encoded = net::packet::encode(42, 1, 0, compressed, net::packet::flags::kCompressed);
    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    auto decoded = net::packet::decode_payload(payload);

    EXPECT_TRUE(decoded.flags & net::packet::flags::kCompressed);
    auto decompressed = net::packet::decompress_body(decoded.body);
    EXPECT_EQ(decompressed, body);
}

TEST(PacketCompressorTest, RoundTripPreservesNonAscii) {
    std::string body;
    for (int i = 0; i < 1000; ++i) body.push_back(static_cast<char>(i % 256));
    auto compressed = net::packet::compress_body(body);
    auto decompressed = net::packet::decompress_body(compressed);
    EXPECT_EQ(decompressed, body);
}

// v1.1.2 / T04: is_compression_available() must reflect HAS_ZLIB at build time.
// When false, callers MUST NOT set kCompressed (the fallback compress_body is
// length-prefix passthrough, not real compression — the flag would be a lie).
TEST(PacketCompressorTest, IsCompressionAvailableMatchesBuildBackend) {
#ifdef HAS_ZLIB
    EXPECT_TRUE(net::packet::is_compression_available());
#else
    EXPECT_FALSE(net::packet::is_compression_available());
#endif
}
