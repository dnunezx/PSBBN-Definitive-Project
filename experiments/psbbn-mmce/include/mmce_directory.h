/* SPDX-License-Identifier: MIT
 * Read-only diagnostics for the initial MMCE directory driver.
 */
#ifndef PSBBN_MMCE_DIRECTORY_H
#define PSBBN_MMCE_DIRECTORY_H
#define MMCE_DIRECTORY_DIAG 0x50424452
#define MMCE_SEEK_DIAG 0x5042534b
struct mmce_seek_diag {
    unsigned int fd, whence, offset_lo, offset_hi;
    int transport;
    unsigned int reply, returned_lo, returned_hi;
};
struct mmce_directory_diag {
    unsigned int revision, reads, stage, status, name_bytes, iterator, tail;
    int result;
};
#endif
