"""Generate an 8 MiB diagnostic disk; no console or copyrighted data required."""
import hashlib
from pathlib import Path
import sys

SECTORS = 16384


def sector(lba):
    # Each group of four bytes carries the full LBA, XORed with its byte offset.
    return bytes(((lba >> ((offset & 3) * 8)) ^ offset ^ 0xA5) & 255
                 for offset in range(512))


def main(path):
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("wb") as stream:
        for lba in range(SECTORS):
            stream.write(sector(lba))
    data = target.read_bytes()
    assert len(data) == 8 * 1024 * 1024
    assert data[:4] == bytes.fromhex("a5a4a7a6")
    assert data[-512:] == sector(SECTORS - 1)
    assert data[:512] != data[512:1024]
    print("Diagnostic disk SHA256:", hashlib.sha256(data).hexdigest())


if __name__ == "__main__":
    main(sys.argv[1])
