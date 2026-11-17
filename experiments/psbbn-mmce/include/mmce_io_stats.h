/* SPDX-License-Identifier: MIT */
#ifndef PSBBN_MMCE_IO_STATS_H
#define PSBBN_MMCE_IO_STATS_H
/* Seven u32 words fit the existing block reply's reserved area. */
struct mmce_io_stats {
    unsigned int read_us, max_read_us, sharing_wait_us;
    unsigned int combined_reads, legacy_reads, cache_hits, errors;
};
#endif
