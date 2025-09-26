#include "events.h"
#include <stdlib.h>


static int compare_events_by_time_then_order(const void* a, const void* b) {
  const ForcedCpuEvent* ea = (const ForcedCpuEvent*)a;
  const ForcedCpuEvent* eb = (const ForcedCpuEvent*)b;

  if (ea->event_time_tick < eb->event_time_tick) return -1;
  if (ea->event_time_tick > eb->event_time_tick) return 1;

  if (ea->input_order_index_for_stable_sorting < eb->input_order_index_for_stable_sorting) return -1;
  if (ea->input_order_index_for_stable_sorting > eb->input_order_index_for_stable_sorting) return 1;

  return 0;
}


void sort_forced_cpu_events_by_time_then_input_order(
  ForcedCpuEvent* array_of_events,
  size_t number_of_events
) {
  if (!array_of_events || number_of_events == 0) return;
  qsort(array_of_events, number_of_events, sizeof(ForcedCpuEvent), compare_events_by_time_then_order);
}
