#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 PSBBN-Nexus contributors
# Adapted for PSBBN Definitive from Nexus tools/LoaderBypassWriter.cs.
# Full notice: scripts/licenses/third-party-adapter-loader
"""Inspect, apply or restore the exact Nexus early-Ethernet loader hook.

Only ordinary loader files are accepted. No disks, partitions, kernel changes
or installer integration are performed by this component.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import sys

LOADER_BYTES = 806908
ORIGINAL_SHA256 = "8867ee33c4ecb79ace6fe88b0b4f0148239102544385f0dcfba5bc7737fe1f18"
PATCHED_SHA256 = "3baad7a3c024ca0da6f10e58026c1943dea00d45f7e0f0390ee91f4ae0f6ab32"
HOOK_OFFSET = 840
HOOK_ADDRESS = 0x018002C8
EXEC_STUB = 0x018002C0
SMAP_CALL_SITES = (0x01001E28, 0x01002340)
SMAP_TARGET = 0x01005008
SUCCESS_PATCH = 0x01009F80
ORIGINAL_WORDS = (
    0x3C020100, 0x3C08FC00, 0x34429F80, 0x8C430000, 0x00681824,
    0x3C080C00, 0x14680004, 0x00000000, 0x3C032402, 0x24630001,
    0xAC430000, 0x086000B0, 0x00000000,
)
PATCHED_WORDS = (
    0x3C020100, 0x3C030C40, 0x34631402, 0x8C481E28, 0x15030006,
    0x3C082402, 0xAC481E28, 0xAC482340, 0x3C020101, 0x35080001,
    0xAC489F80, 0x086000B0, 0x00000000,
)
HOOK_BYTES = len(ORIGINAL_WORDS) * 4


class PatchError(ValueError):
    """The file is not an exact supported loader or an output check failed."""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def _hook_file_offset(data):
    if data[:7] != b"\x7fELF\x01\x01\x01":
        raise PatchError("Expected a little-endian ELF32 loader")
    if struct.unpack_from("<HHI", data, 16) != (2, 8, 1):
        raise PatchError("Expected a MIPS executable")
    phoff = struct.unpack_from("<I", data, 28)[0]
    stride, count = struct.unpack_from("<HH", data, 42)
    if stride != 32 or not 0 < count <= 128 or phoff + stride * count > len(data):
        raise PatchError("Invalid ELF program headers")
    matches = []
    for index in range(count):
        kind, offset, address, _, size, _, flags, _ = struct.unpack_from(
            "<8I", data, phoff + index * stride
        )
        if kind != 1 or not flags & 1:
            continue
        if offset + size > len(data):
            raise PatchError("ELF segment exceeds the file")
        if address <= HOOK_ADDRESS and HOOK_ADDRESS + HOOK_BYTES <= address + size:
            matches.append(offset + HOOK_ADDRESS - address)
    if matches != [HOOK_OFFSET]:
        raise PatchError("Hook does not map to the supported executable segment")
    return matches[0]


def inspect_loader(data):
    if len(data) != LOADER_BYTES:
        raise PatchError("Unsupported loader length: expected {} bytes".format(LOADER_BYTES))
    digest = sha256(data)
    if digest == ORIGINAL_SHA256:
        state, words = "original", ORIGINAL_WORDS
    elif digest == PATCHED_SHA256:
        state, words = "patched", PATCHED_WORDS
    else:
        raise PatchError("Unsupported loader SHA-256; no patch was applied")
    offset = _hook_file_offset(data)
    if data[offset:offset + HOOK_BYTES] != struct.pack("<13I", *words):
        raise PatchError("Hook instructions differ from the supported version")
    return {"state": state, "sha256": digest, "bytes": len(data), "hook_offset": offset}


def transform_loader(data, action):
    if action not in ("apply", "restore"):
        raise PatchError("Expected apply or restore")
    current = inspect_loader(data)
    desired, words, expected = (
        ("patched", PATCHED_WORDS, PATCHED_SHA256) if action == "apply"
        else ("original", ORIGINAL_WORDS, ORIGINAL_SHA256)
    )
    if current["state"] == desired:
        return bytes(data)
    result = bytes(data[:HOOK_OFFSET]) + struct.pack("<13I", *words) + bytes(data[HOOK_OFFSET + HOOK_BYTES:])
    if len(result) != len(data) or sha256(result) != expected:
        raise PatchError("Complete output checksum does not match the supported loader")
    return result


def _reject_device_path(path):
    name = str(path).replace("/", "\\")
    if name.startswith(("\\\\.\\", "\\\\?\\")):
        raise PatchError("Windows device paths are not accepted")


def read_loader(path):
    path = Path(path)
    _reject_device_path(path)
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_size != LOADER_BYTES:
        raise PatchError("Input must be an ordinary loader file of the supported size")
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(path, flags), "rb") as source:
        opened = os.fstat(source.fileno())
        if (not stat.S_ISREG(opened.st_mode) or opened.st_size != LOADER_BYTES
                or (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino)):
            raise PatchError("Input file changed while opening it")
        data = source.read(LOADER_BYTES + 1)
    if len(data) != LOADER_BYTES:
        raise PatchError("Incomplete or changed loader file")
    return data


def write_new_loader(path, data):
    path = Path(path)
    _reject_device_path(path)
    # Exclusive creation also rejects an existing file, symlink or device.
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0)
    descriptor = os.open(path, flags, 0o644)
    identity = os.fstat(descriptor)
    try:
        with os.fdopen(descriptor, "wb") as output:
            if output.write(data) != len(data):
                raise PatchError("Short output write")
            output.flush()
            os.fsync(output.fileno())
        if read_loader(path) != data:
            raise PatchError("Output readback differs from the prepared loader")
    except BaseException:
        # Remove only the fresh output created by this call, never an input or
        # a pre-existing file. A replacement by another process is left alone.
        try:
            current = path.lstat()
            if (current.st_dev, current.st_ino) == (identity.st_dev, identity.st_ino):
                path.unlink()
        except OSError:
            pass
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("inspect", "apply", "restore"))
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    args = parser.parse_args(argv)
    if (args.action == "inspect") != (args.output is None):
        parser.error("inspect accepts only an input; apply/restore require a new output path")
    try:
        source = read_loader(args.input)
        before = inspect_loader(source)
        if args.action == "inspect":
            report = before
        else:
            result = transform_loader(source, args.action)
            after = inspect_loader(result)
            write_new_loader(args.output, result)
            report = {"action": args.action, "input_state": before["state"],
                      "output_state": after["state"], "input_sha256": before["sha256"],
                      "output_sha256": after["sha256"], "bytes": len(result),
                      "changed": source != result, "output": str(args.output)}
    except (OSError, PatchError) as error:
        print("error: {}".format(error), file=sys.stderr)
        return 2
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
