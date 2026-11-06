"""Generate fingerprints only, never image/boot-file payloads.

Usage: make_real_checks.py IMAGE OSDBOOT VMLINUX OUTPUT
OSDBOOT and VMLINUX must be verified exports of __system/p2lboot.
The PS2 reads OUTPUT as PSBBN/real-checks.bin next to PSBBN/psbbn.img.
"""
import argparse
from pathlib import Path
import struct
import zlib

MAGIC = b'PSBBN05\0'
MAX_SAMPLES = 128


def build(image, osdboot, kernel):
    size = image.stat().st_size
    if size != 7 * 1024**3:
        raise ValueError('This profile requires the verified 7 GiB compact image')
    sectors = size // 512
    requests = {(0, 2), (8, 1), (sectors - 64, 64),
                (4194303, 3), (8388607, 3), (8388608, 64)}
    with image.open('rb') as stream:
        lba, seen = 0, set()
        while True:
            if lba in seen or lba >= sectors or len(seen) >= 64:
                raise ValueError('Invalid APA chain')
            seen.add(lba)
            stream.seek(lba * 512)
            header = stream.read(1024)
            words = struct.unpack('<256I', header)
            if words[1] != 0x00415041 or words[0] != sum(words[1:]) & 0xffffffff:
                raise ValueError('Invalid APA checksum/magic')
            if words[16] != lba or not words[17] or words[17] > sectors - lba:
                raise ValueError('Invalid APA bounds')
            requests.add((lba, 2))
            if lba:
                requests.add((lba + 8192, 4))
            lba = words[2]
            if not lba:
                break
        seed = 0x12345678
        for i in range(32):
            seed = (seed * 1664525 + 1013904223) & 0xffffffff
            count = (1, 2, 3, 8, 32, 64)[i % 6]
            requests.add((seed % (sectors - count + 1), count))
        samples = []
        for lba, count in sorted(requests):
            stream.seek(lba * 512)
            data = stream.read(count * 512)
            if len(data) != count * 512:
                raise ValueError('Short image read')
            samples.append((lba, count, zlib.crc32(data)))
    if len(samples) > MAX_SAMPLES:
        raise ValueError('Too many samples')
    files = []
    for path in (osdboot, kernel):
        crc, length = 0, 0
        with path.open('rb') as stream:
            while block := stream.read(32768):
                crc = zlib.crc32(block, crc)
                length += len(block)
        if not 0 < length <= 32 * 1024**2:
            raise ValueError('Invalid boot-file size')
        files.append((length, crc))
    body = struct.pack('<QII', size, len(samples), 2)
    body += b''.join(struct.pack('<III', *sample) for sample in samples)
    body += b''.join(struct.pack('<II', *file) for file in files)
    return MAGIC + struct.pack('<II', len(body), zlib.crc32(body)) + body


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('image', 'osdboot', 'kernel', 'output'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    payload = build(args.image, args.osdboot, args.kernel)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(f'{args.output}: {len(payload)} fingerprint bytes; no Sony payloads')
