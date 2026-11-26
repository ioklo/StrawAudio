# 코드 지도

| 역할 | 시작점 | 구현·검증 |
| --- | --- | --- |
| CLI 인자·종료·통계 | `src/main.cpp`, `include/strawaudio/Options.hpp` | `src/Options.cpp` |
| UI 독립 세션·스레드 소유 | `include/strawaudio/Session.hpp` | `src/Session.mm` |
| 시스템 캡처·음소거·리소스 | `include/strawaudio/Capture.hpp` | `src/Capture.mm` |
| SPSC 고정 크기 버퍼 | `include/strawaudio/FrameRing.hpp` | `tests/CoreTests.cpp` |
| PCM 변환·리샘플링 | `include/strawaudio/Converter.hpp` | `src/Converter.cpp`, `tests/CoreTests.cpp` |
| UDP·패킷화 | `include/strawaudio/UdpSender.hpp` | `src/UdpSender.cpp`, `tests/loopback.py` |

소유 흐름: CLI → Session → Capture / FrameRing / Converter / UdpSender.
HAL 콜백은 생산자, 송신 스레드는 소비자다. Session 제어는 단일 소유 스레드에서 한다.
실제 동작·한계·실행법은 `README.md`, 코딩 기준은 `coding-style.md`를 참고한다.
