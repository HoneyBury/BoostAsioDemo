// Boost Gateway — FragmentAssembler 模糊测试
// 生成乱序/重复/缺失的 fragment 序列，测试 FragmentAssembler::feed() 的健壮性
//
// 构建需求：-fsanitize=fuzzer,address,undefined

#include "net/packet_fragment.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// 最大 fragment 数量
static constexpr std::size_t kMaxFragments = 64;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // 最小输入：至少 8 字节来生成有意义的测试
    if (size < 8) {
        return 0;
    }

    // 使用输入的前 4 字节作为随机种子
    std::uint32_t seed = 0;
    for (int i = 0; i < 4 && i < static_cast<int>(size); ++i) {
        seed = (seed << 8) | static_cast<std::uint32_t>(data[i]);
    }
    std::mt19937 rng(seed);

    // 从输入中读取剩余字节作为 fragment body 数据
    const std::uint8_t* fragment_data = data + 4;
    const std::size_t fragment_size = size - 4;

    // 场景 1：正常 fragment 序列（有序）
    {
        net::packet::FragmentAssembler assembler;

        constexpr std::uint16_t kMessageId = 42;
        constexpr std::uint32_t kRequestId = 100;
        constexpr std::int32_t kErrorCode = 0;

        // 生成 2-8 个 fragments
        const auto total = std::max<std::uint8_t>(2, static_cast<std::uint8_t>(2 + (data[0] % 7)));
        if (total > kMaxFragments) {
            return 0;
        }

        // 为每个 fragment 分配 body，尽量从输入数据中取
        const auto per_fragment = fragment_size / total;
        if (per_fragment == 0) {
            return 0;
        }

        for (std::uint8_t i = 0; i < total; ++i) {
            const auto offset = static_cast<std::size_t>(i) * per_fragment;
            const auto len = (i == total - 1) ? (fragment_size - offset) : per_fragment;
            const auto body = std::string(
                reinterpret_cast<const char*>(fragment_data + offset),
                len);

            std::uint8_t flags = net::packet::fragment_flags::kFragment | (total << 4) | (i & 0x03);
            if (i == total - 1) {
                flags |= net::packet::fragment_flags::kLastFragment;
            }

            net::packet::DecodedPacket fragment{
                0, kMessageId, kRequestId, 0, kErrorCode, flags, body};

            try {
                const auto result = assembler.feed(fragment);
                // 最后一个 fragment 应返回完整的组装包
                if (i == total - 1) {
                    // 应该能成功组装
                }
                (void)result;
            } catch (const std::exception&) {
                // FragmentAssembler 不应抛异常
                // 如果抛了，说明有 bug
                __builtin_trap();
            }
        }

        // 验证 assembler 内部状态已清理
        // 再次 feed 一个非 fragment 包应正常工作
        net::packet::DecodedPacket non_fragment{
            0, kMessageId, kRequestId + 1, 0, kErrorCode, 0, "non-fragment"};
        try {
            const auto result = assembler.feed(non_fragment);
            // 应返回完整的包
            if (!result.has_value()) {
                // 非 fragment 包应该总是立即返回
                __builtin_trap();
            }
        } catch (const std::exception&) {
            __builtin_trap();
        }
    }

    // 场景 2：乱序 fragment 序列
    {
        net::packet::FragmentAssembler assembler;

        constexpr std::uint16_t kMessageId = 43;
        constexpr std::uint32_t kRequestId = 200;

        const auto total = std::max<std::uint8_t>(2, static_cast<std::uint8_t>(2 + (data[1] % 5)));
        if (total > kMaxFragments) {
            return 0;
        }

        // 创建然后打乱顺序
        std::vector<std::uint8_t> indices(total);
        for (std::uint8_t i = 0; i < total; ++i) indices[i] = i;
        std::shuffle(indices.begin(), indices.end(), rng);

        for (const auto idx : indices) {
            const std::string body(static_cast<std::size_t>(data[2] % 64) + 1, static_cast<char>(idx + 'A'));

            std::uint8_t flags = net::packet::fragment_flags::kFragment | (total << 4) | (idx & 0x03);
            if (idx == total - 1) {
                flags |= net::packet::fragment_flags::kLastFragment;
            }

            net::packet::DecodedPacket fragment{
                0, kMessageId, kRequestId, 0, 0, flags, body};

            try {
                const auto result = assembler.feed(fragment);
                (void)result;
            } catch (const std::exception&) {
                __builtin_trap();
            }
        }

        // 乱序下如果 lastFragment 先到达，assembler 可能会提前完成
        // 这不是 bug — 但我们验证不会崩溃
    }

    // 场景 3：重复 fragment
    {
        net::packet::FragmentAssembler assembler;

        constexpr std::uint16_t kMessageId = 44;
        constexpr std::uint32_t kRequestId = 300;

        const auto total = std::max<std::uint8_t>(2, static_cast<std::uint8_t>(2 + (data[3] % 4)));
        if (total > kMaxFragments) {
            return 0;
        }

        // 正常发送完整序列
        for (std::uint8_t i = 0; i < total; ++i) {
            const std::string body(10, static_cast<char>('A' + i));
            std::uint8_t flags = net::packet::fragment_flags::kFragment | (total << 4) | (i & 0x03);
            if (i == total - 1) flags |= net::packet::fragment_flags::kLastFragment;

            net::packet::DecodedPacket fragment{
                0, kMessageId, kRequestId, 0, 0, flags, body};
            assembler.feed(fragment);
        }

        // 重复发送第 0 个 fragment
        {
            const std::string body(10, 'A');
            std::uint8_t flags = net::packet::fragment_flags::kFragment | (total << 4) | 0;
            net::packet::DecodedPacket fragment{
                0, kMessageId, kRequestId, 0, 0, flags, body};
            try {
                // 第 0 个 fragment 的重复应该重置 assembly
                const auto result = assembler.feed(fragment);
                (void)result;
            } catch (const std::exception&) {
                __builtin_trap();
            }
        }
    }

    // 场景 4：缺失中间 fragment（只有第一和最后一个）
    {
        net::packet::FragmentAssembler assembler;

        constexpr std::uint16_t kMessageId = 45;
        constexpr std::uint32_t kRequestId = 400;

        const auto total = 3;

        // 发送第 0 个
        {
            std::uint8_t flags = net::packet::fragment_flags::kFragment | (total << 4) | 0;
            net::packet::DecodedPacket frag{
                0, kMessageId, kRequestId, 0, 0, flags, "first"};
            assembler.feed(frag);
        }

        // 发送最后一个（没有中间的）
        {
            std::uint8_t flags = net::packet::fragment_flags::kFragment |
                                 net::packet::fragment_flags::kLastFragment |
                                 (total << 4) | 2;
            net::packet::DecodedPacket frag{
                0, kMessageId, kRequestId, 0, 0, flags, "last"};
            try {
                // 缺失 fragment 到达 lastFragment 应触发组装（尽管不完整）
                const auto result = assembler.feed(frag);
                (void)result;
            } catch (const std::exception&) {
                __builtin_trap();
            }
        }
    }

    // 场景 5：同时跟踪多个 request_id
    {
        net::packet::FragmentAssembler assembler;

        for (int rid = 0; rid < 4; ++rid) {
            const auto req_id = static_cast<std::uint32_t>(500 + rid);
            const auto total = 2;

            for (std::uint8_t i = 0; i < total; ++i) {
                std::uint8_t flags = net::packet::fragment_flags::kFragment | (total << 4) | (i & 0x03);
                if (i == total - 1) flags |= net::packet::fragment_flags::kLastFragment;

                net::packet::DecodedPacket frag{
                    0, 46, req_id, 0, 0, flags, std::string(5, 'X')};
                try {
                    const auto result = assembler.feed(frag);
                    (void)result;
                } catch (const std::exception&) {
                    __builtin_trap();
                }
            }
        }
    }

    return 0;
}
