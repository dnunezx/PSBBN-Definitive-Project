#ifndef PSBBN_PROBE_RPC_H
#define PSBBN_PROBE_RPC_H
#include <tamtypes.h>
#include <stddef.h>
#define PSBBN_PROBE_RPC 0x50534242
/* Only the stable first four fields of ATA devinfo are used. */
struct probe_reply {
    s32 result, exists, has_packet;
    u32 total_sectors, security_status;
    u32 reserved[11];
    unsigned char sector[512];
};
_Static_assert(sizeof(struct probe_reply) == 576, "RPC reply ABI");
_Static_assert(offsetof(struct probe_reply, sector) == 64, "sector alignment");
#endif
