#include "strawaudio/Converter.hpp"
#include "strawaudio/Options.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <thread>

namespace {
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename T, size_t Extent> std::span<const char> Bytes(std::span<T, Extent> values)
{
    return {reinterpret_cast<const char*>(values.data()), values.size_bytes()};
}

template <typename T, size_t Extent> std::span<char> WritableBytes(std::span<T, Extent> values)
{
    return {reinterpret_cast<char*>(values.data()), values.size_bytes()};
}

void RingBoundaries()
{
    straw::FrameRing ring{5, sizeof(int)};
    std::array<int, 7> input{10, 11, 12, 13, 14, 15, 16};
    auto bytes = Bytes(std::span{input});
    Require(ring.Push(bytes) == 5, "Ring must drop new overflow frames");
    std::array<int, 3> first{};
    auto firstBytes = WritableBytes(std::span{first});
    Require(ring.Pop(firstBytes) == 3 && first[2] == 12, "Initial ring read");
    Require(ring.Push(bytes.subspan(5 * sizeof(int))) == 2, "Wrapped ring write");
    Require(ring.KeepNewest(2) == 2, "Consumer backlog discard");
    std::array<int, 5> last{};
    auto lastBytes = WritableBytes(std::span{last});
    Require(ring.Pop(lastBytes) == 2 && last[0] == 15 && last[1] == 16, "Newest frame ordering");
    Require(ring.Pop(lastBytes) == 0, "Empty ring");

    straw::FrameRing stereo{4, 2 * sizeof(float)};
    std::array<float, 3> left{0.1f, 0.2f, 0.3f};
    std::array<float, 3> right{-0.1f, -0.2f, -0.3f};
    auto leftBytes = Bytes(std::span{left});
    auto rightBytes = Bytes(std::span{right});
    Require(stereo.PushStereoPlanes(leftBytes, rightBytes) == 3, "Planar write");
    std::array<float, 6> output{};
    auto outputBytes = WritableBytes(std::span{output});
    Require(stereo.Pop(outputBytes) == 3, "Planar frame count");

    for (size_t i = 0; i < left.size(); ++i)
        Require(output[2 * i] == left[i] && output[2 * i + 1] == right[i], "Planar channel order");
}

void RingConcurrency()
{
    constexpr uint64_t total = 200000;
    straw::FrameRing ring{127, sizeof(uint64_t)};
    std::thread producer{[&ring] {
        for (uint64_t value = 0; value < total;)
        {
            auto bytes = std::span<const char>{reinterpret_cast<const char*>(&value), sizeof(value)};

            if (ring.Push(bytes))
                ++value;
            else
                std::this_thread::yield();
        }
    }};
    bool ordered{true};

    for (uint64_t expected = 0; expected < total;)
    {
        uint64_t value{};
        auto bytes = std::span<char>{reinterpret_cast<char*>(&value), sizeof(value)};

        if (ring.Pop(bytes))
        {
            ordered = ordered && value == expected;
            ++expected;
        }
        else
            std::this_thread::yield();
    }

    producer.join();
    Require(ordered, "Concurrent ring ordering/ownership");
}

void Resampling(double rate)
{
    auto inputFrames = static_cast<size_t>(rate);
    straw::FrameRing ring{inputFrames + 512, 8};
    AudioStreamBasicDescription format{
        rate, kAudioFormatLinearPCM, kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked, 8, 1, 8, 2, 32, 0};
    straw::Converter converter{format, ring};
    std::array<float, 274> input{};
    std::array<int16_t, 512> output{};
    std::vector<int16_t> result{};

    // 작은 불규칙 청크와 반복적인 임시 입력 고갈을 거쳐도 SRC 상태가 이어져야 한다.
    for (size_t offset = 0; offset < inputFrames;)
    {
        auto count = std::min(size_t{137}, inputFrames - offset);

        for (size_t i = 0; i < count; ++i)
        {
            auto phase = 2 * std::numbers::pi * 997 * double(offset + i) / rate;
            input[i * 2] = static_cast<float>(0.25 * std::sin(phase));
            input[i * 2 + 1] = -input[i * 2];
        }

        auto bytes = std::span<const char>{reinterpret_cast<const char*>(input.data()), count * 8};
        Require(ring.Push(bytes) == count, "Converter test input overflow");
        offset += count;

        for (;;)
        {
            auto frames = converter.Read(output);
            result.insert(result.end(), output.begin(), output.begin() + frames * 2);

            if (!frames)
                break;
        }
    }

    auto frames = result.size() / 2;
    Require(frames > 47000 && frames <= 48100, "Resampled duration/frame count");
    double error{};
    size_t crossings{};

    for (size_t i = 500; i + 1 < frames; ++i)
    {
        error += std::abs(int(result[i * 2]) + int(result[i * 2 + 1]));

        if (result[i * 2] <= 0 && result[(i + 1) * 2] > 0)
            ++crossings;
    }

    auto frequency = double(crossings) * 48000 / double(frames - 501);
    Require(std::abs(frequency - 997) < 3, "SRC pitch continuity");
    Require(error / double(frames - 501) < 2, "Stereo sign/channel integrity");
    converter.Reset();
    Require(converter.Read(output) == 0, "Temporary empty after reset");
}

void OptionsValidation()
{
    auto Parse = [](std::initializer_list<const char*> arguments) {
        std::vector<std::string> strings{};
        std::vector<char*> argv{};

        for (auto* argument : arguments)
            strings.emplace_back(argument);

        for (auto& argument : strings)
            argv.push_back(argument.data());

        return straw::ParseOptions(static_cast<int>(argv.size()), argv.data());
    };
    auto defaults = Parse({"strawaudio"});
    Require(defaults.host == "127.0.0.1" && defaults.mute && defaults.packetBytes == 1024, "Default options");
    auto ipv6 = Parse({"strawaudio", "[::1]:5001", "--keep-local", "--buffer-ms", "20"});
    Require(ipv6.host == "::1" && ipv6.port == "5001" && !ipv6.mute, "IPv6 parsing");

    for (auto arguments :
         {std::initializer_list<const char*>{"x", "--packet-bytes", "1023"},
          {"x", "--duration", "nan"},
          {"x", "--buffer-ms", "0"},
          {"x", "--stats"},
          {"x", "host:0"},
          {"x", "::1:5000"},
          {"x", "host:65536"},
          {"x", "--unknown"}})
    {
        bool rejected{};

        try
        {
            Parse(arguments);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }

        Require(rejected, "Invalid options must fail before capture");
    }
}
} // namespace

int main()
{
    try
    {
        RingBoundaries();
        RingConcurrency();
        OptionsValidation();
        Resampling(44100);
        Resampling(48000);
        Resampling(96000);
        std::cout << "Ring, concurrent ownership, options, and streaming SRC passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
