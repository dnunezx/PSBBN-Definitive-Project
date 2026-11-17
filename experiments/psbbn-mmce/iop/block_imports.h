#include <irx.h>
#include <loadcore.h>
#include <atad.h>
#include <sifrpc.h>
#include <sysclib.h>
#include <thbase.h>
#ifdef PSBBN_BLOCK_FHI_STATS
int fhi_get_io_stats(void *buffer, unsigned int bytes);
#define fhi_IMPORTS_start DECLARE_IMPORT_TABLE(fhi, 1, 1)
#define fhi_IMPORTS_end END_IMPORT_TABLE
#define I_fhi_get_io_stats DECLARE_IMPORT(8, fhi_get_io_stats)
#endif
