#ifndef PROCESS_H
#define PROCESS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum {        // https://stackoverflow.com/questions/34323130/the-importance-of-c-enumeration-typedef-enum
  PROCESS_STATE_RUNNING,
  PROCESS_STATE_READY,
  PROCESS_STATE_WAITING,
  PROCESS_STATE_FINISHED,
  PROCESS_STATE_DEAD
} ProcessState;

typedef enum {
  PROCESS_QUEUE_AFFINITY_NONE,
  PROCESS_QUEUE_AFFINITY_HIGH,
  PROCESS_QUEUE_AFFINITY_LOW
} ProcessQueueAffinity;

typedef struct Process {
  char* process_name;
  unsigned int process_id;

  // Campos de especificación del enunciado:
  ProcessState current_process_state;
  unsigned int cpu_burst_duration_per_burst;
  unsigned int total_number_of_cpu_bursts;
  unsigned int input_output_wait_time_between_bursts;
  unsigned int absolute_execution_deadline;

  // Campos auxiliares para simular:
  unsigned int number_of_completed_cpu_bursts;
  unsigned int remaining_time_in_current_cpu_burst;
  unsigned int remaining_time_in_current_input_output_wait;
  long long last_time_tick_when_left_cpu;            // Tiempo_LCPU
  long long first_time_tick_when_entered_cpu;
  long long time_tick_when_finished_or_dead_for_sorting;
  long long start_time;        // Para response time
  long long response_time;
  unsigned long long turnaround_time;
  unsigned int remaining_quantum;

  unsigned long long accumulated_time_in_ready_or_waiting_states; // waiting
  unsigned int number_of_preemption_interruptions;

  bool has_ever_entered_cpu_at_least_once;
  bool is_priority_forced_to_maximum_due_to_event;

  ProcessQueueAffinity current_queue_affinity;

  // Cache de prioridad efectiva (se recalcula por tick cuando corresponda)
  double last_computed_effective_priority_value;
  long long last_tick_when_priority_was_computed;
  double priority;
} Process;

typedef struct ProcessPool {
  char* queue_human_readable_name;

  Process** internal_dynamic_array_of_process_pointers;
  size_t internal_dynamic_array_size;
  size_t internal_dynamic_array_capacity;
  size_t current_merge_index;
} ProcessPool;

// Constructor básico a partir de los valores del input:
Process* create_process_from_input_line(
  const char* input_process_name,
  unsigned int input_process_id,
  unsigned int input_start_time_tick,
  unsigned int input_cpu_burst_duration_per_burst,
  unsigned int input_total_number_of_cpu_bursts,
  unsigned int input_input_output_wait_time_between_bursts,
  unsigned int input_absolute_execution_deadline
);

// Liberación:
void destroy_process(Process* process_pointer);

void destroy_process_pool(ProcessPool* pool);

// Hook para inicialización posterior (antes de simular):
void initialize_process_simulation_fields(Process* process_pointer);

// Cálculo de prioridad efectiva (con manejo de casos límite):
double compute_effective_priority_value_for_process(
  const Process* process_pointer,
  long long current_simulation_tick
);

bool push_process_into_process_pool(
  ProcessPool* q,
  Process* p
);

#endif // PROCESS_H