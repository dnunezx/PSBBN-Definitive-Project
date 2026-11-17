/* SPDX-License-Identifier: MIT
 * First failed card operation, returned by collection RPC 14 before reset.
 */
#ifndef PSBBN_COLLECTION_IO_H
#define PSBBN_COLLECTION_IO_H
#include <stdint.h>
enum collection_io_operation {
    COLLECTION_IO_NONE, COLLECTION_IO_SEEK, COLLECTION_IO_READ,
    COLLECTION_IO_WRITE, COLLECTION_IO_LENGTH, COLLECTION_IO_CLOSE,
    COLLECTION_IO_OPEN
};
struct collection_io_diag {
    uint32_t operation, file, offset_lo, offset_hi, requested;
    int32_t result;
    uint32_t records, pages, failed;
    uint32_t recovery_attempted, recovered;
};
static inline void collection_io_failure(struct collection_io_diag *diag,
    unsigned int operation, unsigned int file, uint64_t offset,
    unsigned int requested, int result)
{
    if (diag->operation) return;
    diag->operation = operation; diag->file = file;
    diag->offset_lo = offset; diag->offset_hi = offset >> 32;
    diag->requested = requested; diag->result = result;
}
#endif
