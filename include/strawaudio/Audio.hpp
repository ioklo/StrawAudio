#pragma once
#include <AudioToolbox/AudioToolbox.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <stdexcept>

namespace straw {
inline void CheckAudio(OSStatus status, const char* action)
{
    if (status)
        throw std::runtime_error(std::string(action) + " (OSStatus " + std::to_string(status) + ")");
}

inline AudioStreamBasicDescription OutputFormat()
{
    return {
        48000, kAudioFormatLinearPCM, kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked, 4, 1, 4, 2, 16, 0};
}

struct Counters
{
    std::atomic<uint64_t> captured{0}, overruns{0}, droppedInput{0}, staleInput{0};
    std::atomic<uint64_t> sentBytes{0}, sentPackets{0}, droppedPackets{0}, sendErrors{0};
    std::atomic<uint64_t> callbacks{0}, nonSilentFrames{0};
    std::atomic<bool> invalidFormat{false};
};

struct Stats
{
    uint64_t capturedFrames{}, overruns{}, droppedFrames{}, staleFrames{};
    uint64_t sentBytes{}, sentPackets{}, droppedPackets{}, sendErrors{}, callbacks{}, nonSilentFrames{};
};
} // namespace straw
