# StrawAudio

macOS의 시스템 오디오를 Windows FFplay 등으로 보내는 경량 네이티브 CLI입니다.
BlackHole·SoX·netcat·Electron 없이 Core Audio Process Tap으로 캡처합니다.
C++20/Objective-C++와 Apple 시스템 프레임워크만 사용합니다. macOS 14.2 이상이 필요합니다.

## 빌드

Xcode 또는 최신 Command Line Tools와 CMake가 필요합니다. 런타임 외부 의존성은 없습니다.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_OBJCXX_COMPILER=/usr/bin/clang++
cmake --build build -j 4
./build/strawaudio --help
```

실행 파일에 오디오 캡처 사용 설명을 내장하고 `dev.strawaudio.cli` 식별자로 ad-hoc 서명합니다.
다른 맥에 배포하기 위한 Developer ID 서명·공증은 포함하지 않습니다.

## 실행

Windows에서 먼저 수신기를 실행합니다. FFplay가 PATH에 있어야 합니다.

```powershell
ffplay -nodisp -noinfbuf -fflags nobuffer -probesize 32 -analyzeduration 1 -f s16le -sample_rate 48000 -ch_layout stereo -i "udp://0.0.0.0:5000"
```

Windows 방화벽에서 수신 UDP 5000 포트를 허용해야 합니다.
맥에서는 수신 PC의 실제 IP 주소를 지정합니다. 아래 주소는 문서용 예시이므로 바꿔야 합니다.

```sh
./build/strawaudio 192.0.2.10:5000
```

목적지를 생략하면 이 맥의 `127.0.0.1:5000`을 사용합니다. 원격 PC로 보내려면 목적지를 명시하세요. Ctrl+C 또는 SIGTERM으로 종료합니다.
캡처 중에는 `CATapMutedWhenTapped`로 원래 출력을 음소거하고, 종료 시 Tap을 해제합니다.
기본 출력 장치나 기기의 볼륨·mute 속성을 변경하지 않습니다.

**BlackHole로 이미 스트리밍 중이라면** 기존 경로를 유지하는 병행 시험에는 반드시 아래처럼 실행하세요.
기본 음소거 모드는 BlackHole로 가는 원래 출력도 음소거할 수 있습니다. 동일 Windows 포트에 두 송신기를
동시에 보내면 오디오가 섞이므로 병행 테스트는 다른 포트 또는 localhost를 사용하세요.

```sh
./build/strawaudio 127.0.0.1:5001 --keep-local
```

BlackHole을 대체해 사용할 때는 기존 SoX/netcat 송신을 중지하고, macOS 출력을 내장 스피커 등 정상 출력 장치로
직접 돌린 뒤 StrawAudio를 실행합니다. 이 프로그램은 출력 장치를 자동으로 바꾸지 않습니다.

### 시스템 오디오 권한

첫 캡처 시 macOS의 시스템 오디오 녹음 권한을 허용해야 합니다. 권한을 허용한 뒤 프로그램을 다시 실행하세요.
권한 설정은 시스템 설정 → 개인정보 보호 및 보안 → 화면 및 시스템 오디오 녹음에서 확인합니다.
macOS 버전과 실행 방식에 따라 StrawAudio 또는 실행한 터미널/Codex에 연결되어 표시될 수 있습니다.
권한을 거부하면 오류가 발생하거나 무음이 전달될 수 있습니다. 무음만으로 권한 거부를 확정하지 않습니다.
재빌드·서명·실행 경로 변경 후 재승인이 필요할 수도 있습니다.

### 옵션

| 옵션 | 기본값 | 동작 |
| --- | --- | --- |
| `--keep-local` | 꺼짐 | 맥 원래 출력을 유지 |
| `--packet-bytes N` | 1024 | 64~1400, 4의 배수인 UDP payload |
| `--buffer-ms N` | 100 | 고정 입력 링 용량, 20~1000ms |
| `--stats N` | 5 | 통계 출력 간격(초), 0이면 주기 출력 안 함 |
| `--duration N` | 무제한 | 지정 초 후 종료 |
| `--test-tone HZ` | 꺼짐 | 캡처·음소거 없이 -18dBFS 테스트음 송신 |

IPv4·호스트명과 `[IPv6]:port` 목적지를 지원합니다.
출력은 항상 raw PCM signed 16-bit little-endian, 48000Hz, stereo interleaved이며 패킷 헤더가 없습니다.
기본 패킷은 약 5.33ms의 오디오입니다. PCM payload 대역폭은 192000bytes/s(1.536Mbps),
기본 패킷 수는 약 187.5개/s이며 네트워크 헤더 비용은 별도입니다.

## 구조와 메모리

```text
Core Audio Tap → HAL callback → bounded SPSC ring
                                   ↓
                         sender worker thread
                     AudioConverter → packetizer → UDP
```

HAL 콜백은 미리 할당한 링으로 복사만 수행합니다. 콜백에서 할당·락·로깅·네트워크 호출을 하지 않습니다.
변환과 리샘플링은 Apple AudioConverter가 처리합니다. packed stereo PCM의 실제 포맷을 조회·검증하며
interleaved/planar 입력을 처리합니다. 지원하지 않는 포맷은 오류로 종료합니다.

48kHz Float32 stereo 입력과 기본 100ms 링은 38400바이트입니다. 입력 staging은 4096프레임,
출력 블록은 256프레임이고 패킷 버퍼와 커널 송신 큐도 유한합니다. 이 값은 전체 프로세스 메모리와 다릅니다.
링이 가득 차면 새 입력을 버리고, 소비자가 적체·오버런·지연을 감지하면 최신 약 10ms만 남기고
변환기와 패킷 꼬리를 초기화합니다. 전송 지연을 쌓는 대신 일시적인 오디오 손실을 허용합니다.
idle 시 송신 스레드는 2ms 간격으로 확인합니다. 콜백을 깨우기 위한 락·시스템 호출은 추가하지 않았습니다.

`Session`은 CLI와 독립적으로 리소스와 스레드 수명을 소유하므로 후속 AppKit 메뉴바에서 재사용할 수 있습니다.
메뉴바 UI는 아직 구현하지 않았습니다.

## 검증

```sh
./scripts/format.sh --check
ctest --test-dir build --output-on-failure
```

코어 테스트는 SPSC 순서·wrap/overflow·planar 복사·동시 접근·인자 오류를 검사합니다.
44.1/48/96kHz Float32 입력을 작은 청크로 넣고 임시 입력 고갈 이후에도 피치·좌우 관계·길이가
유지되는지 검사합니다. UDP 테스트는 실제 실행 파일을 IPv4/IPv6 localhost에 연결해 패킷 크기,
PCM 주파수·진폭·interleaving과 Ctrl+C/SIGTERM 종료를 검사합니다. 일반 CTest는 캡처 권한을 요청하지 않습니다.

실제 캡처 테스트는 별도로 실행합니다. **3초 테스트음을 재생하며 기본 실행은 원래 출력을 음소거합니다.**
다른 오디오가 재생 중이면 분석에 섞일 수 있습니다. 기존 스트리밍을 보존하려면 `--keep-local`을 붙이세요.
이 경우 테스트음도 기존 스트림에서 들립니다.

```sh
python3 scripts/test-capture.py
python3 scripts/test-capture.py --keep-local
```

이 검증은 테스트음 생성 모드를 사용하지 않고 `afplay → 실제 시스템 출력 → Process Tap → PCM → UDP`를
통과한 좌우 997/1499Hz 성분을 확인합니다. 실제 스피커 음소거·해제와 Windows 재생의 청취 확인은 별도입니다.

기존 출력을 유지하고 테스트음을 추가하지 않는 짧은 메모리/CPU 측정:

```sh
python3 scripts/profile.py --capture --seconds 10
```

프로파일 수신기는 localhost에서 받은 오디오를 즉시 폐기하고 파일로 저장하지 않습니다.
권한 없이 변환·송신 경로만 측정하려면 `--capture`를 생략하세요.

## 현재 확인 범위와 제한

- 2026-11-26 Apple Silicon macOS에서 Release 빌드·서식 검사·코어/UDP CTest 2개를 통과했습니다.
- BlackHole이 기본 스트리밍 경로로 사용 중인 환경에서 실제 Tap 캡처를 확인했습니다.
  6초 테스트에서 좌우 알려진 주파수를 검출했고 input/UDP drop과 overrun은 0이었습니다.
- 기존 출력을 유지한 10초 캡처/송신 측정에서 RSS는 16.23~16.38MiB, CPU는 1.3~1.6%였습니다.
  이 환경의 짧은 관찰값이며 메모리 상한이나 장기 성능 보장은 아닙니다.
- 내장 스피커를 출력 장치로 설정하고 BlackHole을 사용하지 않는 구성에서도 정상 동작이 보고됐습니다.
  간헐적 끊김을 보고했으며 원인은 아직 진단 중입니다. 물리적 음소거/복구의 개별 검증과
  장시간 클럭 차이·누수·배터리·종단 지연 검증은 아직 완료하지 않았습니다.
- UDP는 암호화·수신자 인증·수신 확인·재전송이 없습니다. 신뢰할 수 있는 LAN/VPN에서 사용하세요.
- 네트워크 손실·재정렬·서로 다른 오디오 클럭으로 끊김이나 시간 경과에 따른 지연이 생길 수 있습니다.
  sender buffer 상한은 Windows 수신기의 버퍼와 종단 지연까지 제한하지 않습니다.
- `sent`는 커널이 받은 전송량입니다. 상대가 듣고 있는지는 확인하지 못합니다.
- 장치 변경으로 캡처 포맷이 바뀌면 정리 후 오류로 종료합니다. 장치 변경·절전 복귀 후에는 재시작하세요.
- DRM 등으로 보호된 콘텐츠는 캡처되지 않을 수 있습니다. SIGKILL·시스템 종료 등 강제 중단은
  사용자 코드의 RAII 경로로 정리할 수 없습니다. 비영구 private 리소스의 해제는 macOS에 의존합니다.

코드 위치는 [코드 지도](docs/code-map.md), 작업 규칙은 [AGENTS.md](AGENTS.md)를 참고하세요.
Apple API 기준은 [Capturing system audio with Core Audio taps](https://developer.apple.com/documentation/coreaudio/capturing-system-audio-with-core-audio-taps)입니다.
