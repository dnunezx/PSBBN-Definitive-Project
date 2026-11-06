"""Check the actual built modules for an accidental physical DEV9 dependency."""
from pathlib import Path
import struct

for name, required in (('apa_ro', {'atad', 'iomanx'}),
                       ('pfs_ro', {'iomanx'}), ('atad_ro', {'fhi'})):
    data = (Path('build') / (name + '.irx')).read_bytes()
    imports = set()
    for offset in range(0, len(data) - 20, 4):
        if struct.unpack_from('<I', data, offset)[0] == 0x41e00000:
            imports.add(data[offset + 12:offset + 20].split(b'\0')[0].decode('ascii'))
    assert required <= imports, (name, imports)
    assert 'dev9' not in imports, (name, imports)
    assert data[:4] == b'\x7fELF', name
    print(f'{name}: required imports present; no DEV9 import')
