#!/usr/bin/env python3
"""Measure sender RSS/CPU while receiving and discarding local UDP audio."""
import argparse
from pathlib import Path
import socket
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/strawaudio')
    parser.add_argument('--seconds', type=float, default=10)
    parser.add_argument('--capture', action='store_true', help='Capture existing audio with --keep-local; no test sound')
    args = parser.parse_args()
    if not 2 <= args.seconds <= 300:
        parser.error('--seconds must be between 2 and 300')
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
        receiver.bind(('127.0.0.1', 0))
        receiver.settimeout(0.1)
        destination = f'127.0.0.1:{receiver.getsockname()[1]}'
        command = [str(Path(args.binary).resolve()), destination, '--duration', str(args.seconds), '--stats', '0']
        if args.capture:
            command.append('--keep-local')
        else:
            command.extend(['--test-tone', '997'])
        child = subprocess.Popen(command, stderr=subprocess.PIPE)
        started = time.monotonic()
        sample_at = started + 1
        samples = []
        received = 0
        try:
            while time.monotonic() - started < args.seconds + 15:
                try:
                    received += len(receiver.recv(65536))
                except socket.timeout:
                    if child.poll() is not None:
                        break
                now = time.monotonic()
                if now >= sample_at and child.poll() is None:
                    output = subprocess.run(['/bin/ps', '-o', 'rss=,%cpu=', '-p', str(child.pid)],
                                            capture_output=True, text=True, check=True).stdout.split()
                    if output:
                        rss, cpu = map(float, output)
                        samples.append((now - started, rss, cpu))
                        print(f'{now - started:.1f}s RSS={rss / 1024:.2f} MiB CPU={cpu:.1f}%', flush=True)
                    sample_at = now + 1
            _, error = child.communicate(timeout=2)
            print(error.decode(), end='')
            if child.returncode:
                raise RuntimeError(f'Sender exited with {child.returncode}')
            if not samples or not received:
                raise RuntimeError('No memory samples or UDP packets received')
            print(f'Received={received} bytes; sampled RSS range={min(s[1] for s in samples)/1024:.2f}..{max(s[1] for s in samples)/1024:.2f} MiB')
            print('Short-run RSS/CPU observations; not a long-run leak, battery, or end-to-end latency benchmark.')
        finally:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()


if __name__ == '__main__':
    main()
