/* SPDX-License-Identifier: GPL-2.0-only OR AFL-3.0
 * Shared Linux/IOP protocol. No pointers or host-sized integers on the wire.
 */
#ifndef PSBBN_BLOCK_RPC_H
#define PSBBN_BLOCK_RPC_H
#include <stddef.h>
#define PSBBN_BLOCK_RPC_ID 0x50534244U
#define PSBBN_BLOCK_MAGIC 0x4d4d4345U
#define PSBBN_BLOCK_VERSION 1U
#define PSBBN_BLOCK_INFO 0
#define PSBBN_BLOCK_READ 1
#define PSBBN_BLOCK_WRITE 2
#define PSBBN_BLOCK_FLUSH 3
#define PSBBN_BLOCK_WRITABLE 1U
#define PSBBN_BLOCK_MAX_SECTORS 64U
#define PSBBN_BLOCK_HEADER_SIZE 64U
struct psbbn_block_packet {
    unsigned int magic, version, sequence, lba, count, bytes, capacity, flags;
    int result;
    unsigned int reserved[7];
    unsigned char data[PSBBN_BLOCK_MAX_SECTORS * 512];
};
/* Compatible with GCC 2.95.2, current PS2SDK and host tests. */
typedef char psbbn_block_uint_must_be_32[(sizeof(unsigned int) == 4) ? 1 : -1];
typedef char psbbn_block_header_must_be_64[
    (offsetof(struct psbbn_block_packet, data) == 64) ? 1 : -1];
typedef char psbbn_block_packet_size[(sizeof(struct psbbn_block_packet) == 32832) ? 1 : -1];
#endif
