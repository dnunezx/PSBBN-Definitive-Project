#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PSBBN_BLOCK_HOST_TEST
#define ATA_DIR_READ 0
#define ATA_DIR_WRITE 1
typedef struct { int exists, has_packet; unsigned int total_sectors, security_status; } ata_devinfo_t;
static ata_devinfo_t device = {1, 0, 14680064, 0};
static int calls, writes, backend_result;
static ata_devinfo_t *sceAtaInit(int dev) { assert(dev == 0); return &device; }
static int sceAtaDmaTransfer(int dev, void *buffer, unsigned int lba, unsigned int count, int dir)
{
    assert(dev == 0 && lba < device.total_sectors && count);
    ++calls;
    if (dir == ATA_DIR_WRITE) ++writes;
    else memset(buffer, 0x5a, count * 512);
    return backend_result;
}
#include "../iop/block.c"
static struct psbbn_block_packet input;
static void prepare(unsigned int lba, unsigned int count)
{
    memset(&input, 0, sizeof(input));
    input.magic = PSBBN_BLOCK_MAGIC; input.version = PSBBN_BLOCK_VERSION;
    input.sequence = 123; input.lba = lba; input.count = count; input.bytes = count * 512;
}
int main(void)
{
    struct psbbn_block_packet *out;
    prepare(0, 0);
    out = handle(PSBBN_BLOCK_INFO, &input, 64);
    assert(!out->result && out->capacity == device.total_sectors && !out->flags && out->sequence == 123);
    prepare(8388608, 64);
    out = handle(PSBBN_BLOCK_READ, &input, 64);
    assert(!out->result && out->bytes == 32768 && out->data[32767] == 0x5a && calls == 1);
    backend_result = -5;
    out = handle(PSBBN_BLOCK_READ, &input, 64);
    assert(out->result == -5 && !out->bytes && !out->data[32767]);
    backend_result = 0;
    prepare(1, 1);
    assert(((struct psbbn_block_packet *)handle(PSBBN_BLOCK_WRITE, &input, 576))->result == -30);
    assert(!writes);
    writable = 1;
    assert(!((struct psbbn_block_packet *)handle(PSBBN_BLOCK_WRITE, &input, 576))->result && writes == 1);
    assert(((struct psbbn_block_packet *)handle(PSBBN_BLOCK_WRITE, &input, 575))->result == -22);
    prepare(device.total_sectors - 1, 2);
    assert(((struct psbbn_block_packet *)handle(PSBBN_BLOCK_READ, &input, 64))->result == -22);
    prepare(0, 65);
    assert(((struct psbbn_block_packet *)handle(PSBBN_BLOCK_READ, &input, 64))->result == -22);
    prepare(0, 1); input.bytes++;
    assert(((struct psbbn_block_packet *)handle(PSBBN_BLOCK_READ, &input, 64))->result == -22);
    prepare(0, 1); input.version++;
    assert(((struct psbbn_block_packet *)handle(PSBBN_BLOCK_READ, &input, 64))->result == -22);
    prepare(0, 1);
    assert(((struct psbbn_block_packet *)handle(PSBBN_BLOCK_READ, &input, 63))->result == -22);
    assert(((struct psbbn_block_packet *)handle(PSBBN_BLOCK_FLUSH, &input, 64))->result == -95);
#ifdef PSBBN_BLOCK_CAPACITY
    device.total_sectors = PSBBN_BLOCK_CAPACITY + 2048;
    prepare(PSBBN_BLOCK_CAPACITY, 1);
    out = handle(PSBBN_BLOCK_READ, &input, 64);
    assert(out->capacity == PSBBN_BLOCK_CAPACITY && out->result == -22);
    prepare(PSBBN_BLOCK_CAPACITY - 1, 1);
    assert(!((struct psbbn_block_packet *)handle(PSBBN_BLOCK_READ, &input, 64))->result);
    assert(calls == 4);
    --calls;
#endif
    device.total_sectors = 0;
    assert(((struct psbbn_block_packet *)handle(PSBBN_BLOCK_INFO, &input, 64))->result == -19);
    assert(calls == 3 && writes == 1);
    puts("Block RPC: high offsets, errors, stale data, write gate, malformed requests and bounds passed");
    return 0;
}
