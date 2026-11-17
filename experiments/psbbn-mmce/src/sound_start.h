/* SPDX-License-Identifier: MIT
 * Keep the EE responsive while an IOP sound module starts. A pending RPC's
 * storage must stay alive on timeout; never cancel it, reset or enter Linux.
 */
extern void *_gp;
static unsigned char sound_start_stack[32768] __attribute__((aligned(16)));
static struct {
    unsigned char *data;
    unsigned int size;
    volatile int phase, done;
    int id, result;
} sound_start_work;
static void sound_start_worker(void *unused)
{
    SifDmaTransfer_t transfer;
    void *address;
    unsigned int bytes = (sound_start_work.size + 15) & ~15U;
    unsigned int dma;
    (void)unused;
    sound_start_work.phase = 1;
    address = SifAllocIopHeap(bytes);
    if (address) {
        sound_start_work.phase = 2;
        memset(&transfer, 0, sizeof(transfer));
        transfer.src = sound_start_work.data; transfer.dest = address; transfer.size = bytes;
        sceSifWriteBackDCache(sound_start_work.data, bytes);
        dma = SifSetDma(&transfer, 1);
        if (dma) {
            while (SifDmaStat(dma) >= 0) {}
            sound_start_work.phase = 3;
            sound_start_work.id = SifLoadStartModuleBuffer(address, 0, NULL, &sound_start_work.result);
        }
        sound_start_work.phase = 4;
        SifFreeIopHeap(address);
    }
    __asm__ __volatile__("" ::: "memory");
    sound_start_work.done = 1;
    ExitThread();
}
static int sound_start_module(const char *name, unsigned char *data, unsigned int size)
{
    static const char *phases[] = {"queued", "IOP allocation", "module transfer", "driver entry", "IOP cleanup"};
    ee_thread_t thread;
    ee_thread_status_t status;
    int id, i, phase = -1, current;
    memset(&sound_start_work, 0, sizeof(sound_start_work));
    sound_start_work.data = data; sound_start_work.size = size;
    sound_start_work.id = sound_start_work.result = -1;
    memset(&thread, 0, sizeof(thread));
    if (ReferThreadStatus(GetThreadId(), &status) < 0 || status.current_priority >= 126) return 0;
    thread.func = sound_start_worker; thread.stack = sound_start_stack;
    thread.stack_size = sizeof(sound_start_stack); thread.gp_reg = &_gp;
    /* The waiting main thread must be able to preempt even a spinning loader. */
    thread.initial_priority = status.current_priority + 1;
    id = CreateThread(&thread);
    if (id < 0) return 0;
    report("Starting sound module %s (20-second limit)...\n", name);
    if (StartThread(id, NULL) < 0) { DeleteThread(id); return 0; }
    for (i = 0; i < 200 && !sound_start_work.done; ++i) {
        current = sound_start_work.phase;
        if (current != phase && current >= 0 && current < 5) {
            report("Sound %s: %s\n", name, phases[current]); phase = current;
        }
        storage_wait(100000);
    }
    if (!sound_start_work.done) {
        report("Sound %s timed out during %s.\n", name, phases[sound_start_work.phase]);
        report("Linux not started. Restart the console; do not wait at Loading.\n");
        /* RPC may still reference the worker stack. Keep it and its IOP alive. */
        for (;;) storage_wait(1000000);
    }
    /* The worker has finished all RPC/DMA; stopping a not-yet-dormant tail is safe. */
    TerminateThread(id); DeleteThread(id);
    report("Module %s: id=%d start=%d\n", name, sound_start_work.id, sound_start_work.result);
    return sound_start_work.id >= 0 && (sound_start_work.result == 0 || sound_start_work.result == 2);
}
