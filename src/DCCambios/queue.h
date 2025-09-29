#ifndef QUEUE_H
#define QUEUE_H

#include "process.h"
#include <stddef.h>
#include <stdbool.h>

typedef struct ProcessQueue {
  char* queue_human_readable_name;
  bool is_high_priority_queue;
  unsigned int associated_queue_quantum_in_ticks;

  Process** internal_dynamic_array_of_process_pointers;
  size_t internal_dynamic_array_size;
  size_t internal_dynamic_array_capacity;
} ProcessQueue;

void initialize_process_queue(
  ProcessQueue* queue_pointer,
  const char* queue_human_readable_name,
  bool is_high_priority_queue,
  unsigned int associated_queue_quantum_in_ticks
);

void destroy_process_queue(ProcessQueue* queue_pointer);

// Inserta un proceso READY al final (reordenación se hará al seleccionar)
bool push_ready_process_into_process_queue(
  ProcessQueue* queue_pointer,
  Process* process_pointer
);

// Elimina un proceso de la lista
void remove_process_from_queue(
  ProcessQueue* q,
  size_t index
);

// Devuelve y elimina el proceso de mayor prioridad (según fórmula y PID),
// o NULL si la cola está vacía.
Process* pop_best_ready_process_from_process_queue(
  ProcessQueue* queue_pointer
);

// Devuelve true si está vacía.
bool is_process_queue_empty(const ProcessQueue* queue_pointer);

// Recorre la cola y acumula +1 tick de waiting para procesos READY en esta cola.
// (Ayuda con las métricas en el esqueleto de simulación)
void accumulate_one_tick_of_waiting_time_for_all_ready_processes_in_queue(
  Process** process_array, size_t array_size
);

void qsort_with_tick(Process** base, size_t nmemb);

void reorder_queue_by_priority(ProcessQueue* queue, size_t beginning, size_t end);

#endif // QUEUE_H