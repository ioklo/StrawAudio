#include "strawaudio/Capture.hpp"
#import <CoreAudio/CATapDescription.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <Foundation/Foundation.h>
#include <cmath>

namespace straw {
namespace {
AudioObjectPropertyAddress TapFormatAddress()
{
    return {kAudioTapPropertyFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
}
} // namespace

Capture::Capture(bool mute, Counters& counters) : counters{counters}
{
    @autoreleasepool
    {
        try
        {
            auto* description = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
            description.name = @"StrawAudio system audio";
            description.privateTap = YES;
            description.muteBehavior = mute ? CATapMutedWhenTapped : CATapUnmuted;
            auto status = AudioHardwareCreateProcessTap(description, &tap);
            CheckAudio(
                status,
                "Create system audio tap; check System Settings > Privacy & Security > Screen & System Audio "
                "Recording");

            auto address = TapFormatAddress();
            UInt32 size{sizeof(format)};
            status = AudioObjectGetPropertyData(tap, &address, 0, nullptr, &size, &format);
            CheckAudio(status, "Read tap PCM format");
            planar = (format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
            auto channelsPerBuffer = planar ? 1u : 2u;
            auto expectedStride = channelsPerBuffer * format.mBitsPerChannel / 8;

            if (format.mFormatID != kAudioFormatLinearPCM || format.mChannelsPerFrame != 2 ||
                format.mFramesPerPacket != 1 || !(format.mFormatFlags & kAudioFormatFlagIsPacked) ||
                !format.mBitsPerChannel || format.mBitsPerChannel > 64 || format.mBitsPerChannel % 8 ||
                format.mBytesPerFrame != expectedStride || !std::isfinite(format.mSampleRate) ||
                format.mSampleRate < 8000 || format.mSampleRate > 384000)
                throw std::runtime_error("Unsupported tap PCM layout; expected packed stereo PCM at 8..384 kHz");

            auto* uid = description.UUID.UUIDString;
            auto* aggregateUID = [@"dev.strawaudio.aggregate." stringByAppendingString:uid];
            NSDictionary* composition = @{
                @kAudioAggregateDeviceNameKey : @"StrawAudio (private)",
                @kAudioAggregateDeviceUIDKey : aggregateUID,
                @kAudioAggregateDeviceIsPrivateKey : @YES,
                @kAudioAggregateDeviceIsStackedKey : @NO,
                @kAudioAggregateDeviceTapAutoStartKey : @YES,
                @kAudioAggregateDeviceTapListKey : @[
                    @{@kAudioSubTapUIDKey : uid,
                      @kAudioSubTapDriftCompensationKey : @YES}
                ]
            };
            status = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)composition, &device);
            CheckAudio(status, "Create private tap aggregate device");
            status = AudioObjectAddPropertyListener(tap, &address, Changed, this);
            CheckAudio(status, "Watch capture format");
            listening = true;
        }
        catch (...)
        {
            Cleanup();
            throw;
        }
    }
}

Capture::~Capture()
{
    Cleanup();
}

void Capture::Start(FrameRing& ring)
{
    if (running || proc)
        throw std::logic_error("Capture already started");

    this->ring = &ring;
    auto status = AudioDeviceCreateIOProcID(device, Io, this, &proc);
    CheckAudio(status, "Create tap audio callback");
    status = AudioDeviceStart(device, proc);
    CheckAudio(
        status, "Start capture; allow system audio recording for StrawAudio or the launching terminal, then restart");
    running = true;
}

void Capture::Stop() noexcept
{
    if (running)
    {
        AudioDeviceStop(device, proc);
        running = false;
    }

    if (proc)
    {
        AudioDeviceDestroyIOProcID(device, proc);
        proc = nullptr;
    }

    // HAL 콜백을 제거한 뒤에만 버퍼 참조를 끊는다.
    ring = nullptr;
}

void Capture::Cleanup() noexcept
{
    Stop();

    if (listening)
    {
        auto address = TapFormatAddress();
        AudioObjectRemovePropertyListener(tap, &address, Changed, this);
        listening = false;
    }

    if (device != kAudioObjectUnknown)
    {
        AudioHardwareDestroyAggregateDevice(device);
        device = kAudioObjectUnknown;
    }

    if (tap != kAudioObjectUnknown)
    {
        AudioHardwareDestroyProcessTap(tap);
        tap = kAudioObjectUnknown;
    }
}

OSStatus Capture::Changed(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void* context)
{
    auto& self = *static_cast<Capture*>(context);
    self.counters.invalidFormat.store(true, std::memory_order_relaxed);
    return noErr;
}

OSStatus Capture::Io(
    AudioDeviceID,
    const AudioTimeStamp*,
    const AudioBufferList* input,
    const AudioTimeStamp*,
    AudioBufferList*,
    const AudioTimeStamp*,
    void* context)
{
    auto& self = *static_cast<Capture*>(context);
    auto& counters = self.counters;
    counters.callbacks.fetch_add(1, std::memory_order_relaxed);

    if (!input || !input->mNumberBuffers || !self.ring)
        return noErr;

    auto expectedBuffers = self.planar ? 2u : 1u;

    if (input->mNumberBuffers != expectedBuffers)
    {
        counters.invalidFormat.store(true, std::memory_order_relaxed);
        return noErr;
    }

    auto frames = input->mBuffers[0].mDataByteSize / self.format.mBytesPerFrame;

    for (UInt32 i = 0; i < expectedBuffers; ++i)
    {
        auto& buffer = input->mBuffers[i];
        auto channels = self.planar ? 1u : 2u;

        if (buffer.mNumberChannels != channels || buffer.mDataByteSize % self.format.mBytesPerFrame ||
            buffer.mDataByteSize / self.format.mBytesPerFrame != frames)
        {
            counters.invalidFormat.store(true, std::memory_order_relaxed);
            return noErr;
        }

        if (!buffer.mData)
            return noErr;
    }

    auto& first = input->mBuffers[0];
    auto left = std::span<const char>{static_cast<const char*>(first.mData), first.mDataByteSize};
    size_t accepted{};

    if (self.planar)
    {
        auto& second = input->mBuffers[1];
        auto right = std::span<const char>{static_cast<const char*>(second.mData), second.mDataByteSize};
        accepted = self.ring->PushStereoPlanes(left, right);
    }
    else
        accepted = self.ring->Push(left);

    counters.captured.fetch_add(frames, std::memory_order_relaxed);

    if (accepted < frames)
    {
        counters.overruns.fetch_add(1, std::memory_order_relaxed);
        counters.droppedInput.fetch_add(frames - accepted, std::memory_order_relaxed);
    }

    return noErr;
}
} // namespace straw
