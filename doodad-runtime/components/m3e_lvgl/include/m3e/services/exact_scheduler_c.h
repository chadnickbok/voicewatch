#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* m3e_exact_scheduler_handle;

typedef struct {
    char id[49];
    uint64_t deadline_scenario_ms;
    uint64_t original_duration_ms;
    uint64_t revision;
    uint32_t fire_count;
    uint8_t state;
} m3e_schedule_record;

typedef struct {
    char id[49];
    uint64_t deadline_scenario_ms;
    uint64_t revision;
    uint32_t ordinal;
} m3e_due_delivery;

m3e_exact_scheduler_handle m3e_exact_scheduler_create(void);
void m3e_exact_scheduler_destroy(m3e_exact_scheduler_handle handle);
uint64_t m3e_exact_scheduler_schedule_after(
    m3e_exact_scheduler_handle handle,
    const char* id,
    uint64_t duration_ms,
    uint64_t scenario_now_ms);
int m3e_exact_scheduler_cancel(
    m3e_exact_scheduler_handle handle,
    const char* id);
int m3e_exact_scheduler_acknowledge(
    m3e_exact_scheduler_handle handle,
    const char* id);
size_t m3e_exact_scheduler_poll(
    m3e_exact_scheduler_handle handle,
    uint64_t scenario_now_ms,
    m3e_due_delivery* deliveries,
    size_t delivery_capacity);
size_t m3e_exact_scheduler_records(
    m3e_exact_scheduler_handle handle,
    m3e_schedule_record* records,
    size_t record_capacity,
    uint64_t scenario_now_ms);

#ifdef __cplusplus
}
#endif
