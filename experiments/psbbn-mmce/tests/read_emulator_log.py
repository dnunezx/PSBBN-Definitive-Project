"""Read Linux printk through PCSX2's localhost PINE API; no UI interaction."""
from pathlib import Path
import socket
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
symbols = {}
for line in (ROOT / 'build/linux/System.map').read_text().splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0], 16)

def receive(sock, count):
    data = bytearray()
    while len(data) < count:
        part = sock.recv(count - len(data))
        if not part:
            raise ConnectionError('PCSX2 disconnected')
        data.extend(part)
    return bytes(data)

def read(sock, address, count):
    # KSEG0 is essential: unmapped user addresses can fault inside PINE.
    assert 0x80000000 <= address < 0x82000000
    packets = b''.join(bytes([3]) + struct.pack('<I', address + offset)
                       for offset in range(0, count, 8))
    sock.sendall(struct.pack('<I', len(packets) + 4) + packets)
    length = struct.unpack('<I', receive(sock, 4))[0]
    result = receive(sock, length - 4)
    if result[0] != 0:
        raise RuntimeError('PINE read rejected')
    return result[1:1 + count]

with socket.create_connection(('127.0.0.1', 28011), timeout=10) as sock:
    if '--debug' in sys.argv:
        for name, address in [('sbios_signature', 0x80001004),
                              ('sk_cachep', symbols['sk_cachep']),
                              ('early_active', symbols['early_active'])]:
            print(f'{name}={struct.unpack("<I", read(sock, address, 4))[0]:08x}')
    if '--watch' in sys.argv:
        previous = None
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                value = struct.unpack('<I', read(sock, symbols['sk_cachep'], 4))[0]
            except RuntimeError:
                time.sleep(0.05)
                continue
            if value != previous:
                end = struct.unpack('<I', read(sock, symbols['log_end'], 4))[0]
                data = read(sock, symbols['log_buf'], 16384)
                if end < 1048576:
                    context = bytes(data[i % len(data)] for i in range(max(0, end - 300), end)).decode('latin1')
                    print(f'sk_cachep={value:08x} log_end={end}: {context}', flush=True)
                previous = value
            time.sleep(0.003)
    end = struct.unpack('<I', read(sock, symbols['log_end'], 4))[0]
    data = read(sock, symbols['log_buf'], 16384)
    start = max(0, end - len(data))
    text = bytes(data[i % len(data)] for i in range(start, end)).decode('latin1')
    output = ROOT / 'build/emulator/linux-boot.log'
    output.write_text(text, encoding='utf-8')
    print(text if '--tail' not in sys.argv else '\n'.join(text.splitlines()[-25:]))
    if '--snapshot' in sys.argv:
        sock.sendall(struct.pack('<I', 6) + bytes([9, 0]))
        length = struct.unpack('<I', receive(sock, 4))[0]
        result = receive(sock, length - 4)
        if result[0] != 0:
            raise RuntimeError('PINE save state rejected')
        print('Requested emulator state snapshot in slot 0')
