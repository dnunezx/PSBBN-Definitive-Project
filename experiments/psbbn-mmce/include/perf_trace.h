/* SPDX-License-Identifier: MIT
 * Opt-in timings. No timer calls or output in ordinary builds.
 * Include after report(); PSBBN_PERF_NOW/HZ allow an independent host clock.
 */
#ifndef PSBBN_PERF_TRACE_H
#define PSBBN_PERF_TRACE_H
#include <stdint.h>
#ifdef PSBBN_PERF_TRACE
#ifndef PSBBN_PERF_NOW
#include <timer.h>
#define PSBBN_PERF_NOW() GetTimerSystemTime()
#define PSBBN_PERF_HZ kBUSCLK
#endif
static uint64_t perf_origin;
static inline uint64_t perf_ms(uint64_t ticks)
{
    return ticks / PSBBN_PERF_HZ * 1000 + ticks % PSBBN_PERF_HZ * 1000 / PSBBN_PERF_HZ;
}
static inline void perf_init(const char *scope)
{
    perf_origin = PSBBN_PERF_NOW();
    report("PERF v1 scope=%s event=start clock=ee_bus\n", scope);
}
static inline uint64_t perf_begin(const char *stage)
{
    uint64_t now = PSBBN_PERF_NOW();
    report("PERF v1 stage=%s event=begin total_ms=%llu\n", stage,
           (unsigned long long)perf_ms(now - perf_origin));
    return now;
}
static inline void perf_end(const char *stage, uint64_t start, int ok)
{
    uint64_t now = PSBBN_PERF_NOW();
    report("PERF v1 stage=%s event=end total_ms=%llu elapsed_ms=%llu status=%s\n", stage,
           (unsigned long long)perf_ms(now - perf_origin),
           (unsigned long long)perf_ms(now - start), ok ? "ok" : "failed");
}
static inline void perf_count(const char *key, unsigned int value)
{
    report("PERF v1 counter=%s value=%u\n", key, value);
}
static inline void perf_checkpoint(const char *stage)
{
    report("PERF v1 stage=%s event=checkpoint total_ms=%llu\n", stage,
           (unsigned long long)perf_ms(PSBBN_PERF_NOW() - perf_origin));
}
#else
static inline void perf_init(const char *scope) { (void)scope; }
static inline uint64_t perf_begin(const char *stage) { (void)stage; return 0; }
static inline void perf_end(const char *stage, uint64_t start, int ok)
{ (void)stage; (void)start; (void)ok; }
static inline void perf_count(const char *key, unsigned int value) { (void)key; (void)value; }
static inline void perf_checkpoint(const char *stage) { (void)stage; }
#endif
#endif
