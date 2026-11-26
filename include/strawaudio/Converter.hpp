#pragma once
#include "Audio.hpp"
#include "FrameRing.hpp"
#include <vector>

namespace straw {
class Converter
{
public:
    Converter(AudioStreamBasicDescription interleavedInput, FrameRing& ring);
    ~Converter();
    Converter(const Converter&) = delete;
    Converter& operator=(const Converter&) = delete;
    size_t Read(std::span<int16_t> output);
    void Reset();

private:
    static OSStatus Supply(AudioConverterRef, UInt32*, AudioBufferList*, AudioStreamPacketDescription**, void*);
    AudioConverterRef converter{};
    FrameRing& ring;
    UInt32 stride{};
    std::vector<char> input{};
};
} // namespace straw
