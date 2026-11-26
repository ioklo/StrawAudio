#pragma once
#include "Audio.hpp"
#include "FrameRing.hpp"
#include <CoreAudio/CoreAudio.h>

namespace straw {
class Capture
{
public:
    Capture(bool mute, Counters& counters);
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;

    AudioStreamBasicDescription Format() const
    {
        return format;
    }

    void Start(FrameRing& ring);
    void Stop() noexcept;

private:
    static OSStatus
    Io(AudioDeviceID,
       const AudioTimeStamp*,
       const AudioBufferList*,
       const AudioTimeStamp*,
       AudioBufferList*,
       const AudioTimeStamp*,
       void*);
    static OSStatus Changed(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void*);
    void Cleanup() noexcept;
    AudioObjectID tap = kAudioObjectUnknown;
    AudioDeviceID device = kAudioObjectUnknown;
    AudioDeviceIOProcID proc = nullptr;
    AudioStreamBasicDescription format{};
    FrameRing* ring = nullptr;
    Counters& counters;
    bool running = false, listening = false, planar = false;
};
} // namespace straw
