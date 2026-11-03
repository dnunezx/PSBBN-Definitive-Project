#ifndef PSBBN_SUITE_RPC_H
#define PSBBN_SUITE_RPC_H
#include <tamtypes.h>
#include <stddef.h>
#define SUITE_RPC_ID 0x50534243
#include "suite_logic.h"
struct suite_request { u32 lba, count, reserved[2]; };
struct suite_reply {
    s32 result, exists, packet;
    u32 sectors, security, fhi_sectors, lba, count;
    u32 reserved[8];
    unsigned char data[SUITE_MAX_SECTORS * 512];
};
_Static_assert(sizeof(struct suite_request) == 16, "request ABI");
_Static_assert(offsetof(struct suite_reply, data) == 64, "DMA alignment");
_Static_assert(sizeof(struct suite_reply) == 32832, "reply ABI");
#endif
