# ADR 0001: 네이티브 시스템 오디오 UDP CLI

날짜: 2026-11-26 (KST)
상태: 채택

## 요구사항

- macOS에서 사용할 저메모리 오디오 전송 프로그램. Electron 없이 CLI 우선.
- BlackHole 대신 Core Audio Process Tap. 로컬 출력은 캡처 중 음소거하고 기본 출력 장치는 바꾸지 않는다.
- Windows FFplay와 호환되는 UDP raw PCM S16LE, 48kHz, stereo, 기본 1024바이트 패킷.
- 목적지는 명령줄에서 지정한다. 생략 시 로컬 루프백 127.0.0.1:5000을 사용한다.
- 저장소의 코드 지침과 formatter 설정을 따른다.

## 구현상 채택

- C++20 + Objective-C++, CMake, Apple CoreAudio/AudioToolbox/Foundation과 BSD 소켓만 사용한다.
  프로젝트 작성 기준에 맞춰 C++20을 사용한다.
- private global stereo Process Tap과 private tap-only Aggregate Device를 사용한다.
  음소거는 CATapMutedWhenTapped이므로 기기 볼륨·mute·기본 출력 속성을 쓰지 않는다.
- HAL 콜백은 샘플 바이트를 고정 용량 SPSC 링에 복사한다. planar 입력은 바이트 단위로 교차 복사한다.
- 변환·리샘플링·패킷화·비차단 송신은 별도 단일 스레드에서 한다.
- 링 기본 용량 100ms. 생산자는 초과 입력을 버리고 소비자는 적체·오버런·진행 중단 시 오래된 입력을 버린다.
  이때 변환기와 패킷 꼬리를 초기화한다. 종료 시 대기 오디오를 밀어 보내지 않는다.
- AudioConverter 입력의 일시 고갈은 사용자 OSStatus로 알리고 EOF로 처리하지 않는다.
- 장치 변경으로 tap 포맷이 바뀌면 오류와 함께 종료한다. 자동 재연결은 후속이다.
- Session이 장치·버퍼·송신 스레드 수명을 소유하며 CLI는 인자·신호·통계만 담당한다.
- 실행 파일에 NSAudioCaptureUsageDescription을 내장하고 고정 식별자로 ad-hoc 서명한다.

## 제한과 후속

UDP는 암호화·수신 확인·재전송·타임스탬프·수신 클럭 보정이 없다. 신뢰할 수 있는 LAN/VPN에서 사용한다.
송신 성공은 커널에 전달한 바이트 수이며 원격 재생 성공이 아니다.
전체 프로세스 메모리·장시간 지연·실제 장치 권한 동작은 측정 결과와 별도로 판단한다.
메뉴바 AppKit UI, 장치 자동 재연결, 수신기·압축 코덱은 아직 구현하지 않는다.

## 공개 저장소의 기본 설정

개인 환경 주소를 기본값에 포함하지 않도록 목적지 기본값은 localhost로 정한다.
문서의 원격 예시는 문서용 예약 주소를 사용하며 실제 수신 주소는 실행할 때 지정한다.
