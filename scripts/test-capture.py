#!/usr/bin/env python3
"""Opt-in actual Process Tap test. Plays a known stereo signal through afplay."""
import argparse
import math
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from loopback import amplitude, decode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/strawaudio')
    parser.add_argument('--keep-local', action='store_true', help='Also play from Mac speakers')
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    with tempfile.TemporaryDirectory(prefix='strawaudio-capture-') as folder:
        source = Path(folder) / 'known-stereo.wav'
        with wave.open(str(source), 'wb') as wav:
            wav.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
            samples = bytearray()
            for i in range(48000 * 3):
                left = round(8192 * math.sin(2 * math.pi * 997 * i / 48000))
                right = round(8192 * math.sin(2 * math.pi * 1499 * i / 48000))
                samples.extend(struct.pack('<hh', left, right))
            wav.writeframes(samples)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
            receiver.bind(('127.0.0.1', 0))
            receiver.settimeout(0.1)
            destination = f'127.0.0.1:{receiver.getsockname()[1]}'
            command = [binary, destination, '--duration', '6', '--stats', '0']
            if args.keep_local:
                command.append('--keep-local')
            print('Starting actual system capture. Allow the macOS audio recording prompt if shown.', flush=True)
            print('A 3-second stereo tone will play. Default capture mode should mute Mac playback.', flush=True)
            capture = subprocess.Popen(command, stderr=subprocess.PIPE)
            player = None
            started = time.monotonic()
            packets = []
            try:
                while time.monotonic() - started < 12:
                    if player is None and time.monotonic() - started > 1:
                        player = subprocess.Popen(['/usr/bin/afplay', str(source)], stderr=subprocess.DEVNULL)
                    try:
                        packets.append(receiver.recv(65536))
                    except socket.timeout:
                        if capture.poll() is not None:
                            break
                try:
                    _, error = capture.communicate(timeout=2)
                except subprocess.TimeoutExpired as timeout:
                    partial = timeout.stderr or b''
                    print(partial.decode(), end='')
                    raise RuntimeError('Capture startup timed out. If you just granted audio recording permission, rerun the test.') from None
                print(error.decode(), end='')
                if capture.returncode:
                    raise RuntimeError('Actual capture failed; check recording permission and rerun')
            finally:
                for process in (capture, player):
                    if process is not None and process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
            if not packets:
                raise RuntimeError('No UDP audio. Grant system audio recording permission, then rerun.')
            pcm = decode(packets)
            # 전체 무음 길이에 의존하지 않도록 1초 창에서 좌우의 알려진 주파수를 확인한다.
            left, right = pcm[::2], pcm[1::2]
            levels = []
            for offset in range(0, len(left) - 48000 + 1, 12000):
                l = amplitude(left[offset:offset + 48000], 48000, 997)
                r = amplitude(right[offset:offset + 48000], 48000, 1499)
                levels.append((l, r))
            if not levels or not any(l > 0.005 and r > 0.005 for l, r in levels):
                raise RuntimeError(f'Known stereo tones not detected. Check permission, output volume and other playback. Levels: {levels}')
            print(f'PASS actual Process Tap -> PCM -> UDP: {len(packets)} packets, stereo tones detected.')
            print('Physical speaker mute/unmute and Windows FFplay listening still require human confirmation.')


if __name__ == '__main__':
    main()
