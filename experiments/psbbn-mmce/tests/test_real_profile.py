"""Exercise the fingerprint generator against a sparse, non-Sony 7 GiB disk."""
from pathlib import Path
import struct
import sys
import tempfile
import zlib
from make_real_checks import build


def main(output):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        image, loader, kernel = (root / name for name in ('disk.img', 'loader', 'kernel'))
        loader.write_bytes(b'Public test loader fixture')
        kernel.write_bytes(b'Public test kernel fixture')
        words = [0] * 256
        words[1], words[17] = 0x00415041, 14680064
        words[0] = sum(words[1:]) & 0xffffffff
        with image.open('wb') as f:
            f.truncate(7 * 1024**3)
            f.seek(0)
            f.write(struct.pack('<256I', *words))
            f.seek(4 * 1024**3)
            f.write(bytes(range(256)) * 2)
            f.seek(7 * 1024**3 - 512)
            f.write(b'T' * 512)
        payload = build(image, loader, kernel)
        count = struct.unpack_from('<I', payload, 24)[0]
        samples = [struct.unpack_from('<III', payload, 32 + i * 12) for i in range(count)]
        assert (8388607, 3, zlib.crc32(bytes(512) + bytes(range(256)) * 2 + bytes(512))) in samples
        assert any(lba == 14680000 and count == 64 for lba, count, crc in samples)
        assert zlib.crc32(payload[16:]) == struct.unpack_from('<I', payload, 12)[0]
        if output:
            Path(output).write_bytes(payload)
        with image.open('r+b') as f:
            f.write(bytes(4))
        try:
            build(image, loader, kernel)
        except ValueError:
            pass
        else:
            raise AssertionError('Corrupt APA checksum accepted')
    print('Fingerprint generator: sparse 7 GiB, 4 GiB crossing, tail and corruption passed')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else None)
