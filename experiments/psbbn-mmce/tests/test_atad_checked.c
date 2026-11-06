#include <assert.h>
#define _IOP
#define PSBBN_CHECKED_IO
#define _start test_module_start
#define _exit test_module_exit
#include "../vendor/atad/src/ps2atad.c"
#undef _start
#undef _exit
struct irx_export_table _exp_atad;
static unsigned int reads, writes;
static int backend_result = 1;
int RegisterLibraryEntries(struct irx_export_table *table) { (void)table; return 0; }
u32 fhi_size(int handle) { (void)handle; return 14680064; }
int fhi_read(int h, void *b, unsigned int l, unsigned int c)
{ (void)h; (void)b; (void)l; (void)c; ++reads; return backend_result; }
int fhi_write(int h, const void *b, unsigned int l, unsigned int c)
{ (void)h; (void)b; (void)l; (void)c; ++writes; return backend_result; }
int main(void)
{
    unsigned char data[512];
    assert(!ata_device_sector_io(0, data, 8388608, 1, ATA_DIR_READ));
    assert(!ata_device_sector_io(0, data, 8388608, 1, ATA_DIR_WRITE));
    backend_result = 0;
    assert(ata_device_sector_io(0, data, 0, 1, ATA_DIR_READ) == -5);
    assert(ata_device_sector_io(0, data, 0, 1, ATA_DIR_WRITE) == -5);
    assert(ata_device_sector_io(0, data, 14680063, 2, ATA_DIR_WRITE) == -22);
    assert(reads == 2 && writes == 2);
    puts("Checked ATAD: read/write results propagated and invalid writes blocked");
    return 0;
}
