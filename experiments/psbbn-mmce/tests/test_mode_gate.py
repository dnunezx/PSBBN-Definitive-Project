"""Reject a missing gate and unsafe compiled privilege-entry sequences."""
from pathlib import Path
import struct
import sys
import zipfile
from check_boot_build import check_mode_gate, symbol_code

data = Path(sys.argv[1]).read_bytes()
address, gate = symbol_code(data, 'psbbn_enter_linux')
assert data.count(gate) == 1
check_mode_gate(data)
words = list(struct.unpack('<7I', gate))
mutations = [(0, 0x8c820000),       # load before privilege
             (2, words[2] ^ 1),   # wrong Status value
             (3, 0),              # missing Status write
             (5, 0x08000000 | ((address >> 2) & 0x3ffffff))]  # loop instead of C entry
for index, value in mutations:
    modified = words.copy()
    modified[index] = value
    corrupt = data.replace(gate, struct.pack('<7I', *modified), 1)
    try:
        check_mode_gate(corrupt)
    except AssertionError:
        pass
    else:
        raise AssertionError(('Unsafe gate accepted', index, hex(value)))
if len(sys.argv) > 2:
    with zipfile.ZipFile(sys.argv[2]) as archive:
        previous = archive.read('psbbn_boot.elf')
    try:
        check_mode_gate(previous)
    except AssertionError:
        pass
    else:
        raise AssertionError('Previous unguarded launcher accepted')
print('Mode gate: compiled fix accepted; early load, wrong/missing Status and bad jump rejected')
if len(sys.argv) > 2:
    print('Previous unguarded launcher rejected')
