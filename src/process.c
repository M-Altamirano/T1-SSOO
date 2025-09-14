#include "process.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <math.h>

static char* duplicate_string_or_null(const char* source) {
  if (!source) return NULL;
  size_t length = strlen(source);
  char* copy = (char*)malloc(length + 1);
  if (copy) memcpy(copy, source, length + 1);
  return copy;
}

Process* create_process_from_input_line(
  const char* input_process_name,
  unsigned int input_process_id,
  unsigned int input_start_time_tick,
  unsigned int input_cpu_burst_duration_per_burst,
  unsigned int input_total_number_of_cpu_bursts,
  unsigned int input_input_output_wait_time_between_bursts,
  unsigned int input_absolute_execution_deadline
) {
  (void)input_start_time_tick; // El start time se guarda en la capa de IO (ver io.h)
  Process* p = (Process*)calloc(1, sizeof(Process));
  if (!p) return NULL;

  p->process_name = duplicate_string_or_null(input_process_name);
  p->process_id = input_process_id;

  p->current_process_state = PROCESS_STATE_READY; // Será ajustado al llegar T_INICIO
  p->cpu_burst_duration_per_burst = input_cpu_burst_duration_per_burst;
  p->total_number_of_cpu_bursts = input_total_number_of_cpu_bursts;
  p->input_output_wait_time_between_bursts = input_input_output_wait_time_between_bursts;
  p->absolute_execution_deadline = input_absolute_execution_deadline;

  initialize_process_simulation_fields(p);
  return p;
}

void initialize_process_simulation_fields(Process* p) {
  if (!p) return;
  p->number_of_completed_cpu_bursts = 0u;
  p->remaining_time_in_current_cpu_burst = p->cpu_burst_duration_per_burst;
  p->remaining_time_in_current_input_output_wait = 0u;

  p->last_time_tick_when_left_cpu = -1;
  p->first_time_tick_when_entered_cpu = -1;
  p->time_tick_when_finished_or_dead_for_sorting = -1;

  p->accumulated_time_in_ready_or_waiting_states = 0ull;
  p->number_of_preemption_interruptions = 0u;

  p->has_ever_entered_cpu_at_least_once = false;
  p->is_priority_forced_to_maximum_due_to_event = false;

  p->current_queue_affinity = PROCESS_QUEUE_AFFINITY_NONE;

  p->last_computed_effective_priority_value = 0.0;
  p->last_tick_when_priority_was_computed = -1;
}

void destroy_process(Process* p) {
  if (!p) return;
  free(p->process_name);
  free(p);
}

double compute_effective_priority_value_for_process(
  const Process* p,
  long long current_simulation_tick
) {
  if (!p) return 0.0;

  if (p->is_priority_forced_to_maximum_due_to_event) {
    // Prioridad "máxima" para asegurar desempate
    return DBL_MAX / 2.0;
  }

  long long time_until_deadline =
      (long long)p->absolute_execution_deadline - current_simulation_tick;

  // Evitar división por cero o negativos: si ya está vencido,
  // devolver un valor alto para que se atienda (o lo marcará DEAD en su paso).
  double protective_time_until_deadline =
      (time_until_deadline <= 0) ? 1.0 : (double)time_until_deadline;

  int remaining_bursts = (int)p->total_number_of_cpu_bursts -
                         (int)p->number_of_completed_cpu_bursts;
  if (remaining_bursts < 0) remaining_bursts = 0;

  double priority_value =
      (1.0 / protective_time_until_deadline) + (double)remaining_bursts;

  return priority_value;
}