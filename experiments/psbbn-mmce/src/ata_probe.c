/* v0.2: standalone MMCE file reads, or --inherit for an existing HDD stack.
 * No storage write, format, repair or mount requests. No ATA emulator here.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <debug.h>
/* All file descriptors below are raw fileXio descriptors, never libc fds. */
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include "probe_rpc.h"

#define MODULE(n) extern unsigned char n##_irx[]; extern unsigned int size_##n##_irx
MODULE(iomanX);
MODULE(fileXio);
MODULE(sio2man);
MODULE(mmceman);
MODULE(ata_read_rpc);
static struct fileXioDevice devices[32] __attribute__((aligned(64)));
static unsigned char sector[512] __attribute__((aligned(64)));
static SifRpcClientData_t rpc __attribute__((aligned(64)));
static struct probe_reply reply __attribute__((aligned(64)));

static unsigned int crc32(const unsigned char *p, int count)
{
    unsigned int crc = ~0U;
    int i, bit;
    for (i = 0; i < count; ++i) {
        crc ^= p[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static int load_module(const char *name, unsigned char *data, unsigned int size)
{
    int rv = -1;
    int id = SifExecModuleBuffer(data, size, 0, NULL, &rv);
    scr_printf("%s: id=%d start=%d\n", name, id, rv);
    return id >= 0 && rv == 0;
}

static int standalone_init(void)
{
    scr_printf("Resetting IOP for standalone MMCE test...\n");
    while (!SifIopReset(NULL, 0)) {}
    while (!SifIopSync()) {}
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    return load_module("iomanX", iomanX_irx, size_iomanX_irx) &&
           load_module("fileXio", fileXio_irx, size_fileXio_irx) &&
           load_module("sio2man", sio2man_irx, size_sio2man_irx) &&
           load_module("mmceman", mmceman_irx, size_mmceman_irx);
}

static int rpc_available(int service)
{
    int i, rv;
    /* fileXioInit itself waits forever if FILEXIO is absent. Check first. */
    for (i = 0; i < 100; ++i) {
        rv = SifBindRpc(&rpc, service, 0);
        if (rv < 0) return 0;
        if (rpc.server != NULL) return 1;
        usleep(50000);
    }
    return 0;
}

static void show_sector(void)
{
    int i;
    scr_printf("First sector CRC32: %08x\n", crc32(sector, sizeof(sector)));
    for (i = 0; i < 32; ++i) {
        scr_printf("%02x ", sector[i]);
        if ((i & 15) == 15) scr_printf("\n");
    }
}

static int read_file(const char *path, int fixture)
{
    int fd, count = 0, rv, i;
    s64 size;
    scr_printf("Read-only open: %s\n", path);
    fd = fileXioOpen(path, O_RDONLY, 0);
    if (fd < 0) {
        scr_printf("  open failed: %d\n", fd);
        return 0;
    }
    size = fileXioLseek64(fd, 0, SEEK_END);
    scr_printf("Size bytes (hex): %08x%08x\n",
               (unsigned int)((u64)size >> 32), (unsigned int)size);
    rv = fileXioLseek(fd, 0, SEEK_SET);
    memset(sector, 0, sizeof(sector));
    if (size >= 0 && rv == 0) {
        while (count < (int)sizeof(sector)) {
            rv = fileXioRead(fd, sector + count, sizeof(sector) - count);
            if (rv <= 0) break;
            count += rv;
        }
    }
    fileXioClose(fd);
    scr_printf("First sector: %d/512 bytes\n", count);
    if (count != (int)sizeof(sector)) return 0;
    show_sector();
    if (fixture) {
        for (i = 0; i < (int)sizeof(sector); ++i)
            if (sector[i] != (unsigned char)i) break;
        scr_printf("Fixture comparison: %s\n", i == (int)sizeof(sector) ? "PASS" : "FAIL");
        return i == (int)sizeof(sector);
    }
    scr_printf("Compare CRC with image on PC; no auto-verification.\n");
    return 1;
}

static void inherited_hdd(void)
{
    /* PS2SDK APA reads into the devctl OUTPUT buffer, not arg->data. */
    hddAtaTransfer_t request;
    int rv;
    memset(&request, 0, sizeof(request));
    request.lba = 0;
    request.size = 1;
    scr_printf("hdd0 status: %d\n", fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0));
    scr_printf("hdd0 sector count response: %d\n",
               fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0));
    memset(sector, 0, sizeof(sector));
    rv = fileXioDevctl("hdd0:", HDIOC_READSECTOR, &request, sizeof(request), sector, sizeof(sector));
    scr_printf("hdd0 sector 0 read: %d\n", rv);
    if (rv == 0) show_sector();
    else scr_printf("Needs compatible HDD/APA + ATAD environment.\n");
    scr_printf("This does not identify the physical/virtual backend.\n");
}

static void inherited_atad(void)
{
    int rv, i;
    scr_printf("Preserving IOP; testing existing ATAD exports.\n");
    sbv_patch_enable_lmb();
    if (!load_module("ATAD read RPC", ata_read_rpc_irx, size_ata_read_rpc_irx)) {
        scr_printf("Load failed; ATAD exports may be unavailable.\n");
        return;
    }
    if (!rpc_available(PSBBN_PROBE_RPC)) {
        scr_printf("ATAD read RPC service unavailable.\n");
        return;
    }
    scr_printf("Reading ATAD device 0, LBA 0...\n");
    rv = SifCallRpc(&rpc, 1, SIF_RPC_M_NOWAIT, NULL, 0, &reply, sizeof(reply), NULL, NULL);
    if (rv >= 0) {
        for (i = 0; i < 200 && SifCheckStatRpc(&rpc); ++i) usleep(50000);
        if (SifCheckStatRpc(&rpc)) {
            scr_printf("ATAD read still pending after ~10s.\n");
            return; /* Static reply stays alive for any late DMA completion. */
        }
    }
    scr_printf("RPC: %d ATAD read: %d\n", rv, reply.result);
    if (rv < 0) return;
    scr_printf("exists=%d packet=%d sectors=%u security=%08x\n",
               reply.exists, reply.has_packet, reply.total_sectors, reply.security_status);
    if (reply.result == 0 && reply.exists) {
        memcpy(sector, reply.sector, sizeof(sector));
        show_sector();
    }
    scr_printf("Compare CRC with image; backend not identified.\n");
}

int main(int argc, char **argv)
{
    int i, inherit = 0, hdd = 0, rv;
    const char *path = NULL;
    init_scr();
    scr_printf("PSBBN MMCE DIAGNOSTIC v0.2\n");
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--inherit")) inherit = 1;
        else if (!strcmp(argv[i], "--hdd")) { inherit = 1; hdd = 1; }
        else if (!strncmp(argv[i], "--file=mmce", 11)) path = argv[i] + 7;
        else {
            scr_printf("Usage: --inherit | --hdd | --file=mmce0:/path\n");
            goto done;
        }
    }
    if (inherit && path) {
        scr_printf("Use --inherit or --file, separately.\n");
        goto done;
    }
    SifInitRpc(0);
    if (inherit && !hdd) {
        inherited_atad();
        goto done;
    }
    if (!inherit && !standalone_init()) goto done;
    if (!rpc_available(FILEXIO_IRX)) {
        scr_printf("FILEXIO service absent after ~5s.\n");
        goto done;
    }
    rv = fileXioInit();
    scr_printf("fileXioInit: %d\n", rv);
    if (rv < 0) goto done;
    rv = fileXioGetDeviceList(devices, 32);
    scr_printf("IOP devices (%d):", rv);
    for (i = 0; i < rv && i < 32; ++i) scr_printf(" %.15s", devices[i].name);
    scr_printf("\n");
    if (inherit) inherited_hdd();
    else if (path) read_file(path, 0);
    else if (!read_file("mmce0:/PSBBN/probe.dat", 1))
        read_file("mmce1:/PSBBN/probe.dat", 1);
done:
    scr_printf("\nFinished. Photograph results; power off to exit.\n");
    scr_printf("PSBBN boot and virtual ATA are not proven by this test.\n");
    SleepThread();
    return 0;
}
