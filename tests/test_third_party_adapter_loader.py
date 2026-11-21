# SPDX-License-Identifier: MIT
# Copyright (c) 2026 PSBBN-Nexus contributors
"""Synthetic file and actual-hook instruction tests; no Sony binary fixtures."""

import contextlib
import io
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/helper"))
import third_party_adapter_loader as loader


def synthetic_loader():
    data = bytearray(loader.LOADER_BYTES)
    data[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHIIIIIHHHHHH", data, 16, 2, 8, 1, 0x018000F0,
                     52, 0, 0, 52, 32, 1, 40, 0, 0)
    struct.pack_into("<8I", data, 52, 1, 128, 0x01800000, 0x01800000,
                     len(data) - 128, len(data) - 128, 7, 128)
    struct.pack_into("<13I", data, loader.HOOK_OFFSET, *loader.ORIGINAL_WORDS)
    data[-8:] = b"TESTONLY"
    return bytes(data)


class LoaderFiles(unittest.TestCase):
    def setUp(self):
        self.original = synthetic_loader()
        modified = bytearray(self.original)
        struct.pack_into("<13I", modified, loader.HOOK_OFFSET, *loader.PATCHED_WORDS)
        self.patched = bytes(modified)
        # CI recognizes only these generated fixtures. Production fingerprints
        # remain fixed in the command-line tool; there is no skip-hash option.
        self.profile = mock.patch.multiple(loader,
            ORIGINAL_SHA256=loader.sha256(self.original),
            PATCHED_SHA256=loader.sha256(self.patched))
        self.profile.start()
        self.addCleanup(self.profile.stop)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.source = self.folder / "source.elf"
        self.source.write_bytes(self.original)

    def invoke(self, *args):
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            result = loader.main(list(map(str, args)))
        return result, stdout.getvalue(), stderr.getvalue()

    def test_apply_changes_only_the_exact_hook(self):
        result = loader.transform_loader(self.original, "apply")
        self.assertEqual(result, self.patched)
        changed = [i for i, (a, b) in enumerate(zip(self.original, result)) if a != b]
        self.assertEqual(len(changed), 31)
        self.assertTrue(all(loader.HOOK_OFFSET <= i < loader.HOOK_OFFSET + 52 for i in changed))
        self.assertEqual(result[:loader.HOOK_OFFSET], self.original[:loader.HOOK_OFFSET])
        self.assertEqual(result[loader.HOOK_OFFSET + 52:], self.original[loader.HOOK_OFFSET + 52:])

    def test_restore_and_repeated_actions_are_byte_exact(self):
        self.assertEqual(loader.transform_loader(self.patched, "restore"), self.original)
        self.assertEqual(loader.transform_loader(self.original, "restore"), self.original)
        self.assertEqual(loader.transform_loader(self.patched, "apply"), self.patched)

    def test_truncated_extended_and_changed_files_are_rejected(self):
        changed = bytearray(self.original)
        changed[-1] ^= 1
        for data in (b"", self.original[:-1], self.original + b"x", bytes(changed)):
            with self.subTest(length=len(data)), self.assertRaises(loader.PatchError):
                loader.transform_loader(data, "apply")

    def test_elf_mapping_and_hook_have_independent_guards(self):
        for at in (0, 18, 42, 60, loader.HOOK_OFFSET):
            changed = bytearray(self.original)
            changed[at] ^= 1
            with mock.patch.object(loader, "ORIGINAL_SHA256", loader.sha256(changed)):
                with self.subTest(offset=at), self.assertRaises(loader.PatchError):
                    loader.transform_loader(bytes(changed), "apply")

    def test_complete_output_digest_is_checked(self):
        with mock.patch.object(loader, "PATCHED_SHA256", "0" * 64):
            with self.assertRaises(loader.PatchError):
                loader.transform_loader(self.original, "apply")

    def test_inspect_never_creates_an_output(self):
        status, output, _ = self.invoke("inspect", self.source)
        self.assertEqual(status, 0)
        self.assertEqual(json.loads(output)["state"], "original")
        self.assertEqual(list(self.folder.iterdir()), [self.source])

    def test_file_apply_restore_and_readback(self):
        output, restored = self.folder / "patched.elf", self.folder / "restored.elf"
        self.assertEqual(self.invoke("apply", self.source, output)[0], 0)
        self.assertEqual(output.read_bytes(), self.patched)
        self.assertEqual(self.invoke("restore", output, restored)[0], 0)
        self.assertEqual(restored.read_bytes(), self.original)
        self.assertEqual(self.source.read_bytes(), self.original)

    def test_existing_output_and_in_place_use_are_rejected(self):
        output = self.folder / "existing.elf"
        output.write_bytes(b"keep me")
        self.assertEqual(self.invoke("apply", self.source, output)[0], 2)
        self.assertEqual(output.read_bytes(), b"keep me")
        self.assertEqual(self.invoke("apply", self.source, self.source)[0], 2)
        self.assertEqual(self.source.read_bytes(), self.original)

    def test_unknown_input_creates_no_output(self):
        self.source.write_bytes(b"x" * loader.LOADER_BYTES)
        output = self.folder / "absent.elf"
        self.assertEqual(self.invoke("apply", self.source, output)[0], 2)
        self.assertFalse(output.exists())

    def test_windows_device_paths_are_rejected_before_open(self):
        for path in (r"\\.\PhysicalDrive0", r"\\?\GLOBALROOT\Device\Harddisk0"):
            with mock.patch.object(Path, "lstat") as probe:
                with self.assertRaises(loader.PatchError):
                    loader.read_loader(path)
                probe.assert_not_called()

    def test_directory_input_is_rejected(self):
        with self.assertRaises(loader.PatchError):
            loader.read_loader(self.folder)

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO files are a POSIX test")
    def test_fifo_is_rejected_without_waiting_for_a_writer(self):
        path = self.folder / "fifo"
        os.mkfifo(path)
        with self.assertRaises(loader.PatchError):
            loader.read_loader(path)

    def test_symlink_input_and_output_are_rejected(self):
        link = self.folder / "link.elf"
        try:
            link.symlink_to(self.source)
        except OSError:
            self.skipTest("Creating symlinks is unavailable for this account")
        self.assertEqual(self.invoke("inspect", link)[0], 2)
        self.assertEqual(self.invoke("apply", self.source, link)[0], 2)
        self.assertEqual(self.source.read_bytes(), self.original)

    def test_failed_output_flush_removes_only_its_fresh_file(self):
        output = self.folder / "failed.elf"
        with mock.patch.object(loader.os, "fsync", side_effect=OSError("test flush failure")):
            self.assertEqual(self.invoke("apply", self.source, output)[0], 2)
        self.assertFalse(output.exists())
        self.assertEqual(self.source.read_bytes(), self.original)

    def test_real_cli_rejects_unknown_binary_without_creating_output(self):
        output = self.folder / "absent.elf"
        # Synthetic fixture is deliberately not recognized by the unmodified
        # production CLI in this separate process.
        result = subprocess.run([sys.executable, str(ROOT / "scripts/helper/third_party_adapter_loader.py"),
                                 "apply", str(self.source), str(output)], capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertFalse(output.exists())


def execute_hook(words, registers, memory):
    registers = list(registers)
    memory = dict(memory)
    pc, pending, writes = loader.HOOK_ADDRESS, None, []
    for _ in range(64):
        if pc == loader.EXEC_STUB:
            return registers, memory, writes
        index = (pc - loader.HOOK_ADDRESS) // 4
        if not 0 <= index < len(words):
            raise AssertionError("Hook escaped its bounded code")
        word = words[index]
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        imm = word & 65535
        signed = imm - 65536 if imm & 32768 else imm
        previous, pending = pending, None
        if op == 15:
            registers[rt] = imm << 16
        elif op == 13:
            registers[rt] = registers[rs] | imm
        elif op == 9:
            registers[rt] = registers[rs] + signed
        elif op == 35:
            registers[rt] = memory[(registers[rs] + signed) & 0xFFFFFFFF]
        elif op == 43:
            address = (registers[rs] + signed) & 0xFFFFFFFF
            memory[address] = registers[rt]
            writes.append((address, registers[rt]))
        elif op == 5:
            pending = pc + 4 + signed * 4 if registers[rs] != registers[rt] else pc + 8
        elif op == 2:
            pending = (pc + 4) & 0xF0000000 | (word & 0x3FFFFFF) << 2
        elif word == 0:
            pass
        else:
            raise AssertionError("Unexpected hook instruction: {:08x}".format(word))
        registers = [value & 0xFFFFFFFF for value in registers]
        registers[0] = 0
        pc = previous if previous is not None else pc + 4
    raise AssertionError("Hook did not return to ExecPS2")


class HookInstructions(unittest.TestCase):
    def test_256_register_sets_and_runtime_guard_with_delay_slots(self):
        random_source = random.Random(0x4E45585553)
        expected_call = 0x0C000000 | loader.SMAP_TARGET >> 2
        for case in range(256):
            registers = [0] + [random_source.getrandbits(32) for _ in range(31)]
            matches = case % 2 == 0
            memory = {loader.SMAP_CALL_SITES[0]: expected_call if matches else expected_call ^ 1,
                      loader.SMAP_CALL_SITES[1]: expected_call,
                      loader.SUCCESS_PATCH: 0x0C000001, 0x01004000: 0x0100}
            after, changed, writes = execute_hook(loader.PATCHED_WORDS, registers, memory)
            with self.subTest(case=case):
                self.assertEqual(after[4:8], registers[4:8])
                for index in set(range(32)) - {2, 3, 8}:
                    self.assertEqual(after[index], registers[index])
                if matches:
                    self.assertEqual(writes, [(loader.SMAP_CALL_SITES[0], 0x24020000),
                                              (loader.SMAP_CALL_SITES[1], 0x24020000),
                                              (loader.SUCCESS_PATCH, 0x24020001)])
                    expected = dict(memory)
                    expected.update(writes)
                    self.assertEqual(changed, expected)
                else:
                    self.assertFalse(writes)
                    self.assertEqual(changed, memory)


if __name__ == "__main__":
    unittest.main()
