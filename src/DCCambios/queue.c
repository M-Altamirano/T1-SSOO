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
  //for (size_t i = 0; i < q->internal_dynamic_array_size; i++) destroy_process(q->internal_dynamic_array_of_process_pointers[i]);
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
  q->internal_dynamic_array_of_process_pointers[q->internal_dynamic_array_size] = p;
  q->internal_dynamic_array_size += (size_t) 1;
  printf("%s, %zu\n", p->process_name, q->internal_dynamic_array_size);
  return true;
}

// Elimina un proceso de la lista
// void remove_process_from_queue(
//   ProcessQueue* queue_pointer,
//   size_t index
// ) {
//   for (size_t i = index; i + 1 < queue_pointer->internal_dynamic_array_size; i++) {
//     if (queue_pointer->internal_dynamic_array_size > i) {
//       queue_pointer->internal_dynamic_array_of_process_pointers[i] =
//           queue_pointer->internal_dynamic_array_of_process_pointers[i+1];
//     }
//     else queue_pointer->internal_dynamic_array_of_process_pointers[i] = NULL;
//   }
//   queue_pointer->internal_dynamic_array_size--;
// }
void remove_process_from_queue(ProcessQueue* q, size_t index) {
  if (!q) return;
  if (index >= q->internal_dynamic_array_size) return; // bounds check

  size_t n = q->internal_dynamic_array_size;

  // Shift everything left one slot (only if index is not the last slot)
  for (size_t i = index; i + 1 < n; ++i) {
    q->internal_dynamic_array_of_process_pointers[i] =
      q->internal_dynamic_array_of_process_pointers[i + 1];
  }

  // Clear the now-unused last slot and decrement size
  q->internal_dynamic_array_of_process_pointers[n - 1] = NULL;
  q->internal_dynamic_array_size = n - 1;
}

static int compare_process_pointers_by_effective_priority_then_pid(
  const void* a,
  const void* b
) {
  Process* pa = *(Process**)a;
  Process* pb = *(Process**)b;

  double prio_a = pa->priority;
  double prio_b = pb->priority;

  if (pa->current_process_state == PROCESS_STATE_WAITING && pb->current_process_state == PROCESS_STATE_READY) return -1;
  if (pb->current_process_state == PROCESS_STATE_WAITING && pa->current_process_state == PROCESS_STATE_READY) return 1;

  if (prio_a > prio_b) return 1; // menor prioridad primero
  if (prio_a < prio_b) return -1;

  // Desempate por menor PID
  if (pa->process_id < pb->process_id) return 1;
  if (pa->process_id > pb->process_id) return -1;
  return 0;
}

#if defined(__APPLE__) || defined(__MACH__) || defined(__GLIBC__)
  // qsort_r con argumento "thunk" estilo BSD/GLIBC
  void qsort_with_tick(Process** base, size_t nmemb) {
    qsort(base, nmemb, sizeof(Process*), compare_process_pointers_by_effective_priority_then_pid);
  }
#else
  // Fallback: empaquetamos tick en variable global estática (aceptable para el esqueleto)
  static int compare_fallback(const void* a, const void* b) {
    return compare_process_pointers_by_effective_priority_then_pid(a, b);
  }
  void qsort_with_tick(Process** base, size_t nmemb) {
    qsort(base, nmemb, sizeof(Process*), compare_fallback);
  }
#endif

Process* pop_best_ready_process_from_process_queue(
  ProcessQueue* q
) {
  if (!q || q->internal_dynamic_array_size == 0) return NULL;


  // Toma el primero (mayor prioridad):
  Process* best = q->internal_dynamic_array_of_process_pointers[(q->internal_dynamic_array_size)-1];

  // Elimina el último puntero
  q->internal_dynamic_array_of_process_pointers[(q->internal_dynamic_array_size) - 1] = NULL;
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
  // printf("\n################\n%zu\n", q->internal_dynamic_array_size);
  // for (size_t i = 0; i < q->internal_dynamic_array_capacity; i++) printf("%s, ", q->internal_dynamic_array_of_process_pointers[i] ? q->internal_dynamic_array_of_process_pointers[i]->process_name : "NULL");
  for (size_t i = 0; i < q->internal_dynamic_array_size; i++) {
    Process* p = q->internal_dynamic_array_of_process_pointers[i];
    if (p) {
      p->accumulated_time_in_ready_or_waiting_states += 1ull;
    }
  }
}

static size_t partition_queue (ProcessQueue* queue, size_t beginning, size_t end) {
  size_t p = (size_t) beginning + (end - beginning) / 2;
  Process* pivot = queue->internal_dynamic_array_of_process_pointers[p];
  queue->internal_dynamic_array_of_process_pointers[p] = queue->internal_dynamic_array_of_process_pointers[end];
  queue->internal_dynamic_array_of_process_pointers[end] = pivot;
  size_t current_position = beginning;
  for (size_t i = beginning; i < end - 1; i++) {
    if (queue->internal_dynamic_array_of_process_pointers[i]->last_computed_effective_priority_value < pivot->last_computed_effective_priority_value) {
      Process* placeholder = queue->internal_dynamic_array_of_process_pointers[current_position];
      queue->internal_dynamic_array_of_process_pointers[current_position] = queue->internal_dynamic_array_of_process_pointers[i];
      queue->internal_dynamic_array_of_process_pointers[i] = placeholder;
      current_position++;
    }
  }
  queue->internal_dynamic_array_of_process_pointers[end] = queue->internal_dynamic_array_of_process_pointers[current_position];
  queue->internal_dynamic_array_of_process_pointers[current_position] = pivot;
  return current_position;
}

void reorder_queue_by_priority(ProcessQueue* queue, size_t beginning, size_t end) {
  if (beginning < end) {
    size_t pivot = partition_queue(queue, beginning, end);
    reorder_queue_by_priority(queue, beginning, pivot - 1);
    reorder_queue_by_priority(queue, pivot + 1, end);
  }
}


bool remove_if_present_from_queue(ProcessQueue* queue, Process* p) {
  if (!queue || !p) return false;
  for (size_t i = 0; i < queue->internal_dynamic_array_size; ++i) {
    if (queue->internal_dynamic_array_of_process_pointers[i] == p) {
      remove_process_from_queue(queue, i);
      return true;
    }
  }
  return false;
}


bool push_ready_unique(ProcessQueue* queue, Process* p) {
  if (!queue || !p) return false;
  if (p->current_process_state != PROCESS_STATE_READY) return false;
  remove_if_present_from_queue(queue, p);
  return push_ready_process_into_process_queue(queue, p);
}