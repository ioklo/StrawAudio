#pragma once
#include "Options.hpp"
#include "Audio.hpp"
#include <memory>

namespace straw {
// UI 없이 전송 수명을 소유한다. 후속 AppKit 메뉴바에서도 같은 세션을 사용한다.
// 제어 함수와 통계 조회는 하나의 소유 스레드에서 호출한다.
class Session
{
public:
    explicit Session(Options options);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    void Start();
    void Stop() noexcept;
    Stats GetStats() const;
    std::string Failure() const;
    double InputSampleRate() const;
    size_t RingBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace straw
