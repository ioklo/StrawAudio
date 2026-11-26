#include "strawaudio/Converter.hpp"
#include <algorithm>
#include <limits>

namespace straw {
namespace {
constexpr OSStatus temporarilyEmpty = 'wait';
constexpr size_t inputFrames = 4096;
} // namespace

Converter::Converter(AudioStreamBasicDescription interleavedInput, FrameRing& ring)
    : ring{ring}, stride{interleavedInput.mBytesPerFrame}
{
    input.resize(inputFrames * stride);
    auto output = OutputFormat();
    auto status = AudioConverterNew(&interleavedInput, &output, &converter);
    CheckAudio(status, "Create PCM converter");
}

Converter::~Converter()
{
    if (converter)
        AudioConverterDispose(converter);
}

void Converter::Reset()
{
    auto status = AudioConverterReset(converter);
    CheckAudio(status, "Reset PCM converter");
}

OSStatus Converter::Supply(
    AudioConverterRef, UInt32* packets, AudioBufferList* buffers, AudioStreamPacketDescription**, void* context)
{
    auto& self = *static_cast<Converter*>(context);
    auto requested = std::min(size_t(*packets), inputFrames);
    auto bytes = std::span<char>{self.input.data(), requested * self.stride};
    auto frames = self.ring.Pop(bytes);
    *packets = static_cast<UInt32>(frames);
    buffers->mNumberBuffers = 1;
    buffers->mBuffers[0] = {2, static_cast<UInt32>(frames * self.stride), self.input.data()};

    if (!frames)
        return temporarilyEmpty;

    return noErr;
}

size_t Converter::Read(std::span<int16_t> output)
{
    auto frames = output.size() / 2;
    if (frames > std::numeric_limits<UInt32>::max() / 4)
        throw std::invalid_argument("PCM output block is too large");

    auto packets = static_cast<UInt32>(frames);
    AudioBufferList buffers{1, {{2, packets * 4, output.data()}}};
    auto status = AudioConverterFillComplexBuffer(converter, Supply, this, &packets, &buffers, nullptr);

    // 임시 입력 고갈은 EOF와 구별한다. 함께 반환된 출력 프레임은 그대로 사용한다.
    if (status != temporarilyEmpty)
        CheckAudio(status, "Convert PCM");

    return packets;
}
} // namespace straw
