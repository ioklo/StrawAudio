#include "strawaudio/Session.hpp"
#include "strawaudio/Capture.hpp"
#include "strawaudio/Converter.hpp"
#include "strawaudio/UdpSender.hpp"
#import <Foundation/Foundation.h>
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <numbers>
#include <thread>

namespace straw {
namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t blockFrames = 256;
constexpr double toneAmplitude = 0.125892541;
} // namespace

struct Session::Impl
{
    explicit Impl(Options options) : options{std::move(options)}
    {
    }

    Options options{};
    Counters counters{};
    std::unique_ptr<Capture> capture{};
    std::unique_ptr<FrameRing> ring{};
    std::unique_ptr<Converter> converter{};
    std::unique_ptr<UdpSender> sender{};
    AudioStreamBasicDescription inputFormat{};
    std::thread worker{};
    std::atomic<bool> stopping{false};
    mutable std::mutex errorMutex{};
    std::string error{};
    bool started{};

    void Work() noexcept
    {
        @autoreleasepool
        {
            try
            {
                Run();
            }
            catch (const std::exception& exception)
            {
                std::lock_guard lock{errorMutex};
                error = exception.what();
                stopping.store(true, std::memory_order_relaxed);
            }
            catch (...)
            {
                std::lock_guard lock{errorMutex};
                error = "Unexpected sender failure";
                stopping.store(true, std::memory_order_relaxed);
            }
        }
    }

    void Run()
    {
        std::array<int16_t, blockFrames * 2> pcm{};
        std::array<float, blockFrames * 2> tone{};
        uint64_t toneFrame{};
        auto nextTone = Clock::now();
        auto lastProgress = Clock::now();
        auto previousDrops = counters.droppedInput.load(std::memory_order_relaxed);
        auto keepFrames = static_cast<size_t>(inputFormat.mSampleRate * 0.010);

        while (!stopping.load(std::memory_order_relaxed))
        {
            if (options.toneHz > 0)
            {
                auto now = Clock::now();

                // 정지·절전 복귀 후 밀린 테스트음을 한꺼번에 생성하지 않는다.
                if (now - nextTone > std::chrono::milliseconds{20})
                    nextTone = now;

                if (now < nextTone)
                    std::this_thread::sleep_until(nextTone);

                for (size_t i = 0; i < blockFrames; ++i)
                {
                    auto phase = 2.0 * std::numbers::pi * options.toneHz * static_cast<double>(toneFrame++) / 48000;
                    auto sample = static_cast<float>(toneAmplitude * std::sin(phase));
                    tone[i * 2] = sample;
                    tone[i * 2 + 1] = sample;
                }

                auto bytes = std::span<const char>{reinterpret_cast<const char*>(tone.data()), sizeof(tone)};
                ring->Push(bytes);
                counters.captured.fetch_add(blockFrames, std::memory_order_relaxed);
                nextTone += std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>{double(blockFrames) / 48000});
            }

            auto now = Clock::now();
            auto drops = counters.droppedInput.load(std::memory_order_relaxed);
            auto nearFull = ring->Available() >= ring->Capacity() * 3 / 4;
            auto stalled = now - lastProgress > std::chrono::milliseconds{options.bufferMs};

            if (nearFull || stalled || drops != previousDrops)
            {
                auto discarded = ring->KeepNewest(keepFrames);
                counters.staleInput.fetch_add(discarded, std::memory_order_relaxed);

                if (discarded || drops != previousDrops || stalled)
                {
                    converter->Reset();
                    sender->DiscardPartial();
                }

                previousDrops = drops;
                lastProgress = now;
            }

            auto frames = converter->Read(pcm);

            if (!frames)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
                continue;
            }

            lastProgress = now;
            uint64_t nonSilent{};

            for (size_t i = 0; i < frames; ++i)
                if (pcm[i * 2] || pcm[i * 2 + 1])
                    ++nonSilent;

            counters.nonSilentFrames.fetch_add(nonSilent, std::memory_order_relaxed);
            auto bytes = std::span<char>{reinterpret_cast<char*>(pcm.data()), frames * 4};
            sender->Append(bytes);
        }

        sender->DiscardPartial();
    }
};

Session::Session(Options options) : impl{std::make_unique<Impl>(std::move(options))}
{
}

Session::~Session()
{
    Stop();
}

void Session::Start()
{
    auto& state = *impl;

    if (state.started)
        throw std::logic_error("Create a new Session for each transmission");

    state.started = true;

    try
    {
        // 주소 오류는 캡처·음소거 전에 확인한다.
        state.sender = std::make_unique<UdpSender>(
            state.options.host, state.options.port, state.options.packetBytes, state.counters);

        if (state.options.toneHz > 0)
            state.inputFormat = {
                48000, kAudioFormatLinearPCM, kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked, 8, 1, 8, 2, 32, 0};
        else
        {
            state.capture = std::make_unique<Capture>(state.options.mute, state.counters);
            state.inputFormat = state.capture->Format();

            if (state.inputFormat.mFormatFlags & kAudioFormatFlagIsNonInterleaved)
            {
                state.inputFormat.mFormatFlags &= ~kAudioFormatFlagIsNonInterleaved;
                state.inputFormat.mBytesPerFrame *= 2;
                state.inputFormat.mBytesPerPacket *= 2;
            }
        }

        auto capacity = static_cast<size_t>(std::ceil(state.inputFormat.mSampleRate * state.options.bufferMs / 1000));
        state.ring = std::make_unique<FrameRing>(capacity, state.inputFormat.mBytesPerFrame);
        state.converter = std::make_unique<Converter>(state.inputFormat, *state.ring);
        state.worker = std::thread{[&state] {
            state.Work();
        }};

        if (state.capture)
            state.capture->Start(*state.ring);
    }
    catch (...)
    {
        Stop();
        throw;
    }
}

void Session::Stop() noexcept
{
    auto& state = *impl;

    // 생산자를 먼저 제거해야 링이 더 이상 변경되지 않는다. Tap 제거로 로컬 음소거도 끝낸다.
    if (state.capture)
    {
        state.capture->Stop();
        state.capture.reset();
    }

    state.stopping.store(true, std::memory_order_relaxed);

    if (state.worker.joinable())
        state.worker.join();

    state.converter.reset();
    state.sender.reset();
}

Stats Session::GetStats() const
{
    auto& counters = impl->counters;
    return {
        counters.captured.load(std::memory_order_relaxed),
        counters.overruns.load(std::memory_order_relaxed),
        counters.droppedInput.load(std::memory_order_relaxed),
        counters.staleInput.load(std::memory_order_relaxed),
        counters.sentBytes.load(std::memory_order_relaxed),
        counters.sentPackets.load(std::memory_order_relaxed),
        counters.droppedPackets.load(std::memory_order_relaxed),
        counters.sendErrors.load(std::memory_order_relaxed),
        counters.callbacks.load(std::memory_order_relaxed),
        counters.nonSilentFrames.load(std::memory_order_relaxed)};
}

std::string Session::Failure() const
{
    if (impl->counters.invalidFormat.load(std::memory_order_relaxed))
        return "Capture format changed; restart StrawAudio after switching audio devices or sample rates";

    std::lock_guard lock{impl->errorMutex};
    return impl->error;
}

double Session::InputSampleRate() const
{
    return impl->inputFormat.mSampleRate;
}

size_t Session::RingBytes() const
{
    if (!impl->ring)
        return 0;

    return impl->ring->Capacity() * impl->ring->BytesPerFrame();
}
} // namespace straw
