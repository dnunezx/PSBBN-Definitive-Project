"""Validate the actual MIPS address stores at the observed CDVD-init fault.

Symbolically execute the straight-line table setup after blk_queue_headactive.
All caller-clobbered registers, including $at, start unknown. A correct binary
must materialize each global address, then store the expected table values.
"""
from pathlib import Path
import struct
import sys
import zipfile
from check_boot_build import symbol_code


def read_word(data, address):
    offset = struct.unpack_from('<I', data, 28)[0]
    stride, count = struct.unpack_from('<HH', data, 42)
    for i in range(count):
        kind, fileoff, va, pa, size, memsize, flags, align = struct.unpack_from('<8I', data, offset + i * stride)
        if kind == 1 and va <= address and address + 4 <= va + size:
            return struct.unpack_from('<I', data, fileoff + address - va)[0]
    raise AssertionError(('Address not in initialized ELF data', hex(address)))


def check_cdvd_tables(data):
    address, code = symbol_code(data, 'ps2cdvd_init')
    queue, _ = symbol_code(data, 'blk_queue_headactive')
    words = struct.unpack('<%dI' % (len(code) // 4), code)
    calls = [i for i, word in enumerate(words) if word >> 26 == 3 and
             (((address + i * 4 + 4) & 0xf0000000) | ((word & 0x3ffffff) << 2)) == queue]
    assert len(calls) == 1
    major_address, _ = symbol_code(data, 'ps2cdvd_major')
    major = read_word(data, major_address)
    assert 0 < major < 256
    ahead_address, _ = symbol_code(data, 'ps2cdvd_read_ahead')
    expected = {}
    for table in ('blksize_size', 'hardsect_size', 'read_ahead'):
        base, _ = symbol_code(data, table)
        expected[(base + major * 4) & 0xffffffff] = table
    registers = [None] * 32
    registers[0] = 0
    checked = set()
    for i in range(calls[0] + 2, min(calls[0] + 66, len(words))):
        word = words[i]
        op, rs, rt, rd = word >> 26, (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
        imm = word & 65535
        signed = imm if imm < 32768 else imm - 65536
        if op == 15:
            registers[rt] = imm << 16
        elif op == 9:
            registers[rt] = None if registers[rs] is None else (registers[rs] + signed) & 0xffffffff
        elif op == 13:
            registers[rt] = None if registers[rs] is None else registers[rs] | imm
        elif op == 0 and word & 63 in (0, 0x21, 0x25):
            function = word & 63
            if function == 0:
                registers[rd] = None if registers[rt] is None else (registers[rt] << ((word >> 6) & 31)) & 0xffffffff
            else:
                a, b = registers[rs], registers[rt]
                registers[rd] = None if a is None or b is None else ((a + b) if function == 0x21 else (a | b)) & 0xffffffff
        elif op == 35:
            registers[rt] = None if registers[rs] is None else read_word(data, (registers[rs] + signed) & 0xffffffff)
        elif op == 43:
            assert registers[rs] is not None, ('CDVD table store uses an uninitialized base', hex(address + i * 4), rs)
            target = (registers[rs] + signed) & 0xffffffff
            assert target in expected, ('CDVD store outside expected table slot', hex(target))
            value = registers[rt]
            table = expected[target]
            assert value is not None
            if table == 'read_ahead':
                assert value == read_word(data, ahead_address)
            else:
                assert read_word(data, value) == 2048, ('Wrong CDVD sector size', table, value)
            checked.add(table)
            if len(checked) == 3:
                return
        else:
            raise AssertionError(('Unexpected table-setup instruction', hex(address + i * 4), hex(word)))
        registers[0] = 0
    raise AssertionError(('Incomplete CDVD table setup', checked))


if __name__ == '__main__':
    check_cdvd_tables(Path(sys.argv[1]).read_bytes())
    print('CDVD binary regression: all three global table stores have initialized addresses and correct values')
    if len(sys.argv) > 2:
        with zipfile.ZipFile(sys.argv[2]) as archive:
            old_loader = archive.read('psbbn_boot.elf')
        _, old_kernel = symbol_code(old_loader, 'linux_kernel')
        try:
            check_cdvd_tables(old_kernel)
        except AssertionError as error:
            assert 'uninitialized base' in str(error), error
            print('Previous kernel rejected at the observed uninitialized-base store:', error)
        else:
            raise AssertionError('The known failing kernel was accepted')
