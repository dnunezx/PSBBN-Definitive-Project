#include <assert.h>
#define _IOP
#define PSBBN_READ_ONLY
#define _start test_module_start
#define _exit test_module_exit
#include "../vendor/atad/src/ps2atad.c"
#undef _start
#undef _exit
struct irx_export_table _exp_atad;
static unsigned int calls;
static int read_result = 1;
int RegisterLibraryEntries(struct irx_export_table *table) { (void)table; return 0; }
u32 fhi_size(int handle) { (void)handle; return 14680064; }
int fhi_read(int handle, void *buffer, unsigned int lba, unsigned int count)
{
    (void)handle; (void)buffer; (void)lba; (void)count;
    ++calls; return read_result;
}
int fhi_write(int handle, const void *buffer, unsigned int lba, unsigned int count)
{
    (void)handle; (void)buffer; (void)lba; (void)count;
    assert(!"Read-only ATAD submitted a write"); return 0;
}
int main(void)
{
    unsigned char data[512];
    assert(ata_device_sector_io(0, data, 8388608, 1, ATA_DIR_READ) == 0);
    assert(calls == 1);
    read_result = 0;
    assert(ata_device_sector_io(0, data, 0, 1, ATA_DIR_READ) == -5);
    assert(calls == 2);
    assert(ata_device_sector_io(0, data, 0, 1, ATA_DIR_WRITE) == -30);
    assert(ata_device_sector_io(1, data, 0, 1, ATA_DIR_READ) == -22);
    assert(ata_device_sector_io(0, data, 14680064, 1, ATA_DIR_READ) == -22);
    assert(ata_device_sector_io(0, data, 14680063, 2, ATA_DIR_READ) == -22);
    assert(ata_device_sector_io(0, data, 0, 0, ATA_DIR_READ) == -22);
    assert(ata_device_sector_io(0, NULL, 0, 1, ATA_DIR_READ) == -22);
    assert(ata_device_sector_io(0, data, 0, 1, 2) == -22);
    assert(calls == 2);
    puts("Read-only ATAD: high LBA, transport errors, rejected writes and bounds passed");
    return 0;
}
