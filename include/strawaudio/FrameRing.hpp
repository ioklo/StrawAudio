#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace straw {
// HAL은 write만, 송신 스레드는 read만 갱신한다. 인덱스로 데이터 소유권을 넘긴다.
class FrameRing
{
public:
    FrameRing(size_t capacity, size_t bytesPerFrame) : capacity{capacity}, stride{bytesPerFrame}
    {
        if (!capacity || !bytesPerFrame || capacity > std::numeric_limits<size_t>::max() / bytesPerFrame)
            throw std::invalid_argument("Invalid audio ring size");

        data.resize(capacity * bytesPerFrame);
    }

    size_t Capacity() const
    {
        return capacity;
    }

    size_t BytesPerFrame() const
    {
        return stride;
    }

    // 소비자 전용 조회다. 생산자가 아직 공개하지 않은 프레임은 포함하지 않는다.
    size_t Available() const noexcept
    {
        auto produced = write.load(std::memory_order_acquire);
        auto consumed = read.load(std::memory_order_relaxed);
        return produced - consumed;
    }

    // OS가 제공하는 읽기 전용 버퍼를 복사한다. 초과분은 버리고 소비자 영역은 덮어쓰지 않는다.
    size_t Push(std::span<const char> input) noexcept
    {
        auto produced = write.load(std::memory_order_relaxed);
        auto consumed = read.load(std::memory_order_acquire);
        auto frames = input.size() / stride;
        auto count = std::min(frames, capacity - size_t(produced - consumed));
        auto offset = size_t(produced % capacity);
        auto first = std::min(count, capacity - offset);

        if (count)
        {
            std::memcpy(data.data() + offset * stride, input.data(), first * stride);
            std::memcpy(data.data(), input.data() + first * stride, (count - first) * stride);
            write.store(produced + count, std::memory_order_release);
        }

        return count;
    }

    // 샘플 변환 없이 채널 바이트만 교차 복사한다. 콜백에서 할당·락·시스템 호출은 없다.
    size_t PushStereoPlanes(std::span<const char> left, std::span<const char> right) noexcept
    {
        if (stride < 2 || stride % 2)
            return 0;

        auto produced = write.load(std::memory_order_relaxed);
        auto consumed = read.load(std::memory_order_acquire);
        auto sampleBytes = stride / 2;
        auto frames = std::min(left.size(), right.size()) / sampleBytes;
        auto count = std::min(frames, capacity - size_t(produced - consumed));

        for (size_t i = 0; i < count; ++i)
        {
            auto offset = ((produced + i) % capacity) * stride;
            auto* destination = data.data() + offset;
            std::memcpy(destination, left.data() + i * sampleBytes, sampleBytes);
            std::memcpy(destination + sampleBytes, right.data() + i * sampleBytes, sampleBytes);
        }

        write.store(produced + count, std::memory_order_release);
        return count;
    }

    size_t Pop(std::span<char> output) noexcept
    {
        auto consumed = read.load(std::memory_order_relaxed);
        auto produced = write.load(std::memory_order_acquire);
        auto frames = output.size() / stride;
        auto count = std::min(frames, size_t(produced - consumed));
        auto offset = size_t(consumed % capacity);
        auto first = std::min(count, capacity - offset);

        if (count)
        {
            std::memcpy(output.data(), data.data() + offset * stride, first * stride);
            std::memcpy(output.data() + first * stride, data.data(), (count - first) * stride);
            read.store(consumed + count, std::memory_order_release);
        }

        return count;
    }

    // 정체 후에는 소비자가 오래된 프레임을 버린다. 생산자 인덱스는 건드리지 않는다.
    size_t KeepNewest(size_t frames) noexcept
    {
        auto consumed = read.load(std::memory_order_relaxed);
        auto produced = write.load(std::memory_order_acquire);
        size_t dropped{};

        if (produced - consumed > frames)
            dropped = size_t(produced - consumed) - frames;

        read.store(consumed + dropped, std::memory_order_release);
        return dropped;
    }

private:
    size_t capacity{};
    size_t stride{};
    std::vector<char> data{};
    alignas(64) std::atomic<uint64_t> write{0};
    alignas(64) std::atomic<uint64_t> read{0};
};

static_assert(std::atomic<uint64_t>::is_always_lock_free);
} // namespace straw
