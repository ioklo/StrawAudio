"""Real binary -> UDP -> s16le decoding; no audio permission or speaker output."""
import array
import math
import signal
import socket
import subprocess
import sys
import time


def amplitude(samples, rate, frequency):
    real = sum(value * math.cos(2 * math.pi * frequency * i / rate) for i, value in enumerate(samples))
    imag = sum(value * math.sin(2 * math.pi * frequency * i / rate) for i, value in enumerate(samples))
    return 2 * math.hypot(real, imag) / len(samples) / 32768


def decode(packets):
    pcm = array.array('h')
    pcm.frombytes(b''.join(packets))
    if sys.byteorder != 'little':
        pcm.byteswap()
    return pcm


def run(binary, packet_bytes, stop_signal=None, family=socket.AF_INET):
    with socket.socket(family, socket.SOCK_DGRAM) as receiver:
        host = '127.0.0.1' if family == socket.AF_INET else '::1'
        receiver.bind((host, 0))
        receiver.settimeout(0.1)
        port = receiver.getsockname()[1]
        destination = f'{host}:{port}' if family == socket.AF_INET else f'[{host}]:{port}'
        process = subprocess.Popen([binary, destination, '--test-tone', '997', '--duration', '0.7',
                                    '--stats', '0', '--packet-bytes', str(packet_bytes)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        packets = []
        started = time.monotonic()
        signaled = False
        try:
            while time.monotonic() - started < 10:
                if stop_signal and not signaled and time.monotonic() - started > 0.3:
                    process.send_signal(stop_signal)
                    signaled = True
                try:
                    packets.append(receiver.recv(65536))
                except socket.timeout:
                    if process.poll() is not None:
                        break
            _, stderr = process.communicate(timeout=3)
            assert process.returncode == 0, stderr.decode()
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
        assert len(packets) >= 10, (len(packets), stderr.decode())
        assert all(len(packet) == packet_bytes for packet in packets), 'Unexpected payload size'
        pcm = decode(packets)
        left, right = pcm[::2], pcm[1::2]
        assert left == right, 'Stereo interleaving mismatch'
        measured = amplitude(left, 48000, 997)
        assert abs(measured - 0.12589) < 0.004, ('Amplitude/pitch/endianness mismatch', measured)
        assert max(left) < 4200 and min(left) > -4200, 'Unexpected clipping'
        print(f'PASS UDP {destination}, {packet_bytes} bytes, {len(packets)} packets, amplitude={measured:.4f}, signal={stop_signal}')


if __name__ == '__main__':
    run(sys.argv[1], 1024)
    run(sys.argv[1], 1400, signal.SIGINT)
    run(sys.argv[1], 64, signal.SIGTERM)
    run(sys.argv[1], 1024, family=socket.AF_INET6)
