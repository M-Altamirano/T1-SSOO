#ifndef EVENTS_H
#define EVENTS_H

#include <stddef.h>

typedef struct ForcedCpuEvent {
  unsigned int event_process_id;
  unsigned int event_time_tick;
  unsigned int input_order_index_for_stable_sorting;
} ForcedCpuEvent;

// Ordena por event_time_tick ascendente y, en empate, por input_order_index_for_stable_sorting ascendente.
void sort_forced_cpu_events_by_time_then_input_order(
  ForcedCpuEvent* array_of_events,
  size_t number_of_events
);

#endif // EVENTS_H