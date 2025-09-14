#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

static char* duplicate_string_or_null(const char* s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  char* out = (char*)malloc(n + 1);
  if (out) memcpy(out, s, n + 1);
  return out;
}

void initialize_process_queue(
  ProcessQueue* q,
  const char* queue_human_readable_name,
  bool is_high_priority_queue,
  unsigned int associated_queue_quantum_in_ticks
) {
  if (!q) return;
  q->queue_human_readable_name = duplicate_string_or_null(queue_human_readable_name);
  q->is_high_priority_queue = is_high_priority_queue;
  q->associated_queue_quantum_in_ticks = associated_queue_quantum_in_ticks;
  q->internal_dynamic_array_of_process_pointers = NULL;
  q->internal_dynamic_array_size = 0;
  q->internal_dynamic_array_capacity = 0;
}

void destroy_process_queue(ProcessQueue* q) {
  if (!q) return;
  free(q->queue_human_readable_name);
  free(q->internal_dynamic_array_of_process_pointers);
  q->queue_human_readable_name = NULL;
  q->internal_dynamic_array_of_process_pointers = NULL;
  q->internal_dynamic_array_size = 0;
  q->internal_dynamic_array_capacity = 0;
}

bool push_ready_process_into_process_queue(
  ProcessQueue* q,
  Process* p
) {
  if (!q || !p) return false;
  if (q->internal_dynamic_array_size == q->internal_dynamic_array_capacity) {
    size_t new_cap = (q->internal_dynamic_array_capacity == 0) ? 8 : (q->internal_dynamic_array_capacity * 2);
    Process** new_arr = (Process**)realloc(q->internal_dynamic_array_of_process_pointers, new_cap * sizeof(Process*));
    if (!new_arr) return false;
    q->internal_dynamic_array_of_process_pointers = new_arr;
    q->internal_dynamic_array_capacity = new_cap;
  }
  q->internal_dynamic_array_of_process_pointers[q->internal_dynamic_array_size++] = p;
  return true;
}

static int compare_process_pointers_by_effective_priority_then_pid(
  const void* a,
  const void* b,
  void* thunk // current tick passed via qsort_r (GNU/BSD) fallback below
) {
  long long current_simulation_tick = *(long long*)thunk;
  Process* pa = *(Process**)a;
  Process* pb = *(Process**)b;

  double prio_a = compute_effective_priority_value_for_process(pa, current_simulation_tick);
  double prio_b = compute_effective_priority_value_for_process(pb, current_simulation_tick);

  if (prio_a > prio_b) return -1; // mayor prioridad primero
  if (prio_a < prio_b) return 1;

  // Desempate por menor PID
  if (pa->process_id < pb->process_id) return -1;
  if (pa->process_id > pb->process_id) return 1;
  return 0;
}

#if defined(__APPLE__) || defined(__MACH__) || defined(__GLIBC__)
  // qsort_r con argumento "thunk" estilo BSD/GLIBC
  static void qsort_with_tick(Process** base, size_t nmemb, long long* tick_ptr) {
    qsort_r(base, nmemb, sizeof(Process*), compare_process_pointers_by_effective_priority_then_pid, tick_ptr);
  }
#else
  // Fallback: empaquetamos tick en variable global estática (aceptable para el esqueleto)
  static long long g_current_tick_for_sort = 0;
  static int compare_fallback(const void* a, const void* b) {
    return compare_process_pointers_by_effective_priority_then_pid(a, b, &g_current_tick_for_sort);
  }
  static void qsort_with_tick(Process** base, size_t nmemb, long long* tick_ptr) {
    g_current_tick_for_sort = *tick_ptr;
    qsort(base, nmemb, sizeof(Process*), compare_fallback);
  }
#endif

Process* pop_best_ready_process_from_process_queue(
  ProcessQueue* q,
  long long current_simulation_tick
) {
  if (!q || q->internal_dynamic_array_size == 0) return NULL;

  // Ordena según prioridad efectiva y PID
  qsort_with_tick(q->internal_dynamic_array_of_process_pointers,
                  q->internal_dynamic_array_size,
                  &current_simulation_tick);

  // Toma el primero (mayor prioridad):
  Process* best = q->internal_dynamic_array_of_process_pointers[0];

  // Compacta
  for (size_t i = 1; i < q->internal_dynamic_array_size; ++i) {
    q->internal_dynamic_array_of_process_pointers[i - 1] =
        q->internal_dynamic_array_of_process_pointers[i];
  }
  q->internal_dynamic_array_size--;

  return best;
}

bool is_process_queue_empty(const ProcessQueue* q) {
  return !q || q->internal_dynamic_array_size == 0;
}

void accumulate_one_tick_of_waiting_time_for_all_ready_processes_in_queue(
  ProcessQueue* q
) {
  if (!q) return;
  for (size_t i = 0; i < q->internal_dynamic_array_size; ++i) {
    Process* p = q->internal_dynamic_array_of_process_pointers[i];
    if (p && p->current_process_state == PROCESS_STATE_READY) {
      p->accumulated_time_in_ready_or_waiting_states += 1ull;
    }
  }
}