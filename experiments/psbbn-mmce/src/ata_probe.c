/* PSBBN-MMCE ATA probe v0.1: read-only, no IOP reset.
 * Requires an existing IOMANX/FILEXIO RPC environment.
 * A lack of hdd0: is NOT proof virtual ATA emulation failed.
 */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <sifrpc.h>
#include <debug.h>
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>

static struct fileXioDevice devices[32] __attribute__((aligned(64)));
static unsigned char sector_arg[8 + 512] __attribute__((aligned(64)));

int main(int argc, char **argv)
{
    int result, i, fd;
    (void)argc;
    (void)argv;
    init_scr();
    scr_printf("PSBBN-MMCE / ATA PROBE v0.1\n");
    scr_printf("Read-only. No IOP reset.\n\n");

    SifInitRpc(0);
    result = fileXioInit();
    scr_printf("fileXioInit: %d\n", result);
    if (result < 0) {
        scr_printf("FILEXIO RPC unavailable. Check IOP environment.\n");
        goto done;
    }

    memset(devices, 0, sizeof(devices));
    result = fileXioGetDeviceList(devices, 32);
    scr_printf("Device list result: %d\n", result);
    if (result > 0) {
        int count = result < 32 ? result : 32;
        for (i = 0; i < count; ++i)
            scr_printf("  %s: type=0x%x\n", devices[i].name, devices[i].type);
    }

    result = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
    scr_printf("hdd0 status: %d\n", result);

    result = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
    scr_printf("hdd0 total-sectors response: %d\n", result);

    fd = fileXioDopen("hdd0:");
    scr_printf("hdd0 partition dir open: %d\n", fd);
    if (fd >= 0) fileXioDclose(fd);

    /* HDIOC_READSECTOR expects {u32 lba, u32 count, u8 sector[512]}.
     * This only works when a compatible HDD.IRX is loaded. */
    memset(sector_arg, 0, sizeof(sector_arg));
    sector_arg[4] = 1; /* count=1, little-endian */
    result = fileXioDevctl("hdd0:", HDIOC_READSECTOR,
                           sector_arg, sizeof(sector_arg), NULL, 0);
    scr_printf("sector 0 read result: %d\n", result);
    if (result >= 0) {
        scr_printf("sector 0 first 32 bytes:\n");
        for (i = 0; i < 32; ++i) {
            scr_printf("%02x ", sector_arg[8 + i]);
            if ((i & 15) == 15) scr_printf("\n");
        }
        scr_printf("Compare with original image; not automatically verified.\n");
    } else {
        scr_printf("Raw sector API unavailable or read failed.\n");
    }

done:
    scr_printf("\nProbe finished. Power off to exit.\n");
    for (;;) { /* Leave results visible. */ }
    return 0;
}
