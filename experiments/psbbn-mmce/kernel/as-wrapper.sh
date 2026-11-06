#!/bin/bash
set -euo pipefail
# GCC 2.95.2 emits the old gas spelling -mcpu=r5900 and redundant -mips2.
# Current PS2 gas selects the actual EE ISA with -march=r5900.
as_args=()
for argument in "$@"; do
    case "$argument" in
        -mcpu=r5900|-mips2) ;;
        *) as_args+=("$argument") ;;
    esac
done
exec /opt/psbbn-binutils/as.bin -32 -march=r5900 "${as_args[@]}"
