"""Audit the built ELF, Linux memory ranges and boot module library versions."""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent.parent


def symbol_code(data, name):
    """Get a defined ELF32 function without depending on an external objdump."""
    shoff = struct.unpack_from('<I', data, 32)[0]
    stride, count = struct.unpack_from('<HH', data, 46)
    sections = [struct.unpack_from('<10I', data, shoff + i * stride) for i in range(count)]
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            index, address, size, info, other, target = struct.unpack_from('<IIIBBH', data, offset)
            if target and names[index:].split(b'\0', 1)[0] == name.encode():
                code_section = sections[target]
                start = code_section[4] + address - code_section[3]
                return address, data[start:start + size]
    raise AssertionError(('ELF symbol missing', name))


def check_mode_gate(data):
    gate, code = symbol_code(data, 'psbbn_enter_linux')
    body, _ = symbol_code(data, 'psbbn_enter_linux_privileged')
    assert len(code) == 28, 'Mode gate must stay a seven-instruction assembly sequence'
    words = struct.unpack('<7I', code)
    # Before the Status write, permit only coprocessor control, sync and LUI.
    # No compiler-generated load/store, branch or call can precede privilege.
    registers = {0: 0}
    for word in words[:3]:
        opcode = word >> 26
        assert opcode in (0x10, 0x0f) or word in (0, 0x40f), ('Unsafe pre-mode instruction', hex(word))
        if opcode == 0x0f:
            registers[(word >> 16) & 31] = (word & 65535) << 16
    assert words[3] & 0xffe0ffff == 0x40806000, 'Fourth instruction must write CP0 Status'
    assert registers[(words[3] >> 16) & 31] == 0x30010000, 'Kernel mode and disabled IE are required'
    assert words[4] == 0x40f, 'Status change requires sync.p'
    assert words[5] >> 26 == 2 and words[6] == 0, 'Use a direct tail jump with a NOP delay slot'
    assert ((gate + 24) & 0xf0000000) | ((words[5] & 0x3ffffff) << 2) == body
    _, main = symbol_code(data, 'main')
    assert any(word >> 26 in (2, 3) and ((word & 0x3ffffff) << 2) == gate
               for word in struct.unpack('<%dI' % (len(main) // 4), main)), 'Main must enter through the assembly gate'


def segments(path):
    data = path.read_bytes()
    assert data[:7] == b'\x7fELF\x01\x01\x01', path
    entry, offset = struct.unpack_from('<II', data, 24)
    stride, count = struct.unpack_from('<HH', data, 42)
    loads = []
    for i in range(count):
        kind, fileoff, va, pa, fs, ms, flags, align = struct.unpack_from('<8I', data, offset + i * stride)
        if kind == 1:
            assert fs <= ms and fileoff + fs <= len(data), path
            loads.append((pa & 0x1fffffff, (pa & 0x1fffffff) + ms))
    return entry, loads


def main():
    entry, kernel = segments(ROOT / 'build/linux/vmlinux')
    kernel_flags = struct.unpack_from('<I', (ROOT / 'build/linux/vmlinux').read_bytes(), 36)[0]
    assert not (kernel_flags & 0x20), 'Kernel must not be tagged as N32'
    assert kernel_flags & 0xf000 in (0, 0x1000), 'Kernel must retain O32 calling convention'
    assert entry == 0x80010490
    assert all(0x10000 <= start < end <= 0x800000 for start, end in kernel)
    _, loader = segments(ROOT / 'psbbn_boot.elf')
    assert all(0x1000000 <= start < end < 0x1f7e000 for start, end in loader)
    assert 0x1f7e000 + 0x1000 < 0x1f80000  # Dedicated exception stack below SDK stack.
    assert 0x1f80000 + 0x20000 < 0x1fff000  # stack stays below bootpage
    symbols = (ROOT / 'build/linux/System.map').read_text()
    assert ' psbbn_mmce_init\n' in symbols and ' ps2_ide_select\n' not in symbols
    assert ' handleRPCIRQ\n' in symbols
    assert ' psbbn_trace_entry\n' in symbols and ' psbbn_trace_font\n' in symbols
    assert ' psbbn_early_console_init\n' in symbols and ' psbbn_early_console_stop\n' in symbols
    assert ' psbbn_early_console_graphics_handoff\n' in symbols
    assert ' ps2sd_init\n' not in symbols
    config = (ROOT / 'build/linux/.config').read_text()
    assert 'CONFIG_PSBBN_MMCE=y' in config and '\nCONFIG_IDE=y' not in config
    assert '\nCONFIG_PS2_SD=y' not in config and '\nCONFIG_SOUND=y' not in config
    loader_bytes = (ROOT / 'psbbn_boot.elf').read_bytes()
    check_mode_gate(loader_bytes)
    assert b'Sound disabled for this recovery test.' in loader_bytes
    assert b'H7 TLB CLEARED - JUMPING TO LINUX' in loader_bytes
    assert b'T0 Checking early screen output' in loader_bytes
    assert b'T2 Early screen transfer returned' in loader_bytes
    assert b'psbbn_exception_entry\0' in loader_bytes and b'psbbn_handoff_fault\0' in loader_bytes
    kernel_bytes = (ROOT / 'build/linux/vmlinux').read_bytes()
    assert b'K0 LINUX ENTRY REACHED' in kernel_bytes and b'K5 LINUX START_KERNEL' in kernel_bytes
    assert b'PSBBN EARLY LINUX LOG' in kernel_bytes and b'PSBBN: PS2 RTC initialization' in kernel_bytes
    assert b'PSBBN init: %s' in kernel_bytes and b'PSBBN done: %s = %d' in kernel_bytes
    assert b'K6 NATIVE GS CONSOLE TAKEOVER' in kernel_bytes
    assert b'K7 USERSPACE GRAPHICS DEVICE OPENED' in kernel_bytes
    assert b'root=/dev/hda1 rw console=tty0 graphics\0' in loader_bytes
    initcalls = [line.split()[2] for line in symbols.splitlines()
                 if len(line.split()) == 3 and line.split()[2].startswith('__initcall_')
                 and line.split()[2] not in ('__initcall_start', '__initcall_end')]
    assert initcalls
    for name in initcalls:
        _, slot = symbol_code(kernel_bytes, name)
        target, _ = symbol_code(kernel_bytes, 'psbbn_initcall_' + name[len('__initcall_'):])
        assert len(slot) == 4 and struct.unpack('<I', slot)[0] == target, ('Untraced initcall', name)
    assert b'console=tty0 romcons' not in loader_bytes
    boot_object = (ROOT / 'src/boot_main.o').read_bytes()
    assert b'FlushCache\0' not in boot_object, 'Launcher must not call SDK cache-flush syscalls'
    assert b'psbbn_flush_caches\0' in boot_object, 'Direct privileged cache routine is required'
    for name in ('libsd_irx', 'sdrdrv_irx'):
        assert name.encode() + b'\0' not in loader_bytes, ('Unexpected sound module embedded', name)
    paths = [ROOT / 'build/boot-sdk' / (n + '.irx') for n in
             ('sio2man', 'mcman-2000', 'mcserv-2000', 'padman-2000')]
    paths += [ROOT / 'build' / (n + '.irx') for n in
              ('mmcefhi', 'atad_checked', 'block_rpc', 'intrelay')]
    providers, imports = {}, []
    for path in paths:
        data = path.read_bytes()
        for offset in range(0, len(data) - 20, 4):
            magic = struct.unpack_from('<I', data, offset)[0]
            if magic not in (0x41e00000, 0x41c00000):
                continue
            name = data[offset + 12:offset + 20].split(b'\0')[0].decode('ascii')
            version = struct.unpack_from('<H', data, offset + 8)[0]
            if magic == 0x41c00000:
                assert name not in providers, ('Duplicate library', name)
                providers[name] = version
            else:
                assert name != 'dev9', ('Physical DEV9 import', path)
                imports.append((path.name, name, version))
    for module, name, version in imports:
        if name in providers:
            actual = providers[name]
            assert actual >> 8 == version >> 8 and (actual & 255) >= (version & 255), (module, name, version, actual)
    assert providers.get('sio2man') == 0x207 and providers.get('sio2man1') == 0x102
    for module in ('mcman-2000.irx', 'padman-2000.irx'):
        assert (module, 'sio2man', 0x203) in imports, ('Unexpected SIO2 interface', module)
    print('Boot audit: assembly privilege gate precedes C loads, protected memory disjoint, virtual HDD/markers/fault handler linked, Slim IRQ present, sound disabled, library versions match; no DEV9 imports')


if __name__ == '__main__':
    main()
