#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* duplicate_string_or_null(const char* s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  char* out = (char*)malloc(n + 1);
  if (out) memcpy(out, s, n + 1);
  return out;
}

bool initialize_simulation_context_from_input(
  SimulationContext* c,
  const SimulationInputData* src
) {
  if (!c || !src) return false;
  memset(c, 0, sizeof(*c));
  c->current_simulation_tick = 0;
  c->next_forced_event_index_to_process = 0;

  // Copiamos SimulationInputData por valor (shallow).
  c->simulation_input_data = *src;

  // Inicializar colas con sus quantums:
  unsigned int base_q = c->simulation_input_data.base_quantum_parameter_q_from_first_line;
  initialize_process_queue(
    &c->high_priority_mlfq_queue,
    "High Priority MLFQ Queue",
    true,
    2u * base_q
  );
  initialize_process_queue(
    &c->low_priority_mlfq_queue,
    "Low Priority MLFQ Queue",
    false,
    base_q
  );

  // CPU libre al inicio:
  c->cpu_execution_unit.currently_running_process_pointer = NULL;
  c->cpu_execution_unit.remaining_quantum_time_in_ticks_for_current_process = 0u;

  return true;
}

void destroy_simulation_context(SimulationContext* c) {
  if (!c) return;

  // Destruir colas (no destruye procesos, eso lo hace destroy_simulation_input_data)
  destroy_process_queue(&c->high_priority_mlfq_queue);
  destroy_process_queue(&c->low_priority_mlfq_queue);

  // Destruir input (incluye procesos y eventos)
  destroy_simulation_input_data(&c->simulation_input_data);

  memset(c, 0, sizeof(*c));
}

void step1_move_processes_from_waiting_to_ready_if_io_completed(SimulationContext* c) {
  (void)c;
  // Falta: Implementar transición WAITING -> READY cuando se agote IO.
}

void step2_mark_processes_as_dead_if_deadline_reached_in_queues(SimulationContext* c) {
  (void)c;
  // Falta: Marcar como DEAD si venció deadline (para procesos en colas).
}

void step3_update_currently_running_process_with_ordered_rules(SimulationContext* c) {
  (void)c;
  // Falta: Implementar orden 3.1 a 3.5.
}

static void ingress_new_arrivals_into_high_queue_when_start_time_matches_tick(
  SimulationContext* c
) {
  // parte 4.2: cuando tick == T_INICIO, ingresar a High en READY.
  for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; ++i) {
    ProcessInputRecord* rec = &c->simulation_input_data.array_of_process_input_records[i];
    Process* p = rec->instantiated_process_pointer;
    if (!p) continue;

    if (rec->input_start_time_tick == (unsigned int)c->current_simulation_tick) {
      p->current_process_state = PROCESS_STATE_READY;
      p->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;
      push_ready_process_into_process_queue(&c->high_priority_mlfq_queue, p);
    }
  }
}

void step4_ingress_processes_into_queues_according_to_rules(SimulationContext* c) {
  // 4.1) El que salió de CPU: pendiente
  // 4.2) Ingresos por T_INICIO:
  ingress_new_arrivals_into_high_queue_when_start_time_matches_tick(c);
  // 4.3) Subir Low->High por condición de urgencia: pendiente
}

void step5_recompute_priorities_for_all_ready_processes(SimulationContext* c) {
  (void)c;
  // Falta: Recalcular prioridades (aquí, con colas basadas en pop ordenado, no es crítico aún).
}

void step6_select_next_process_for_cpu_according_to_priority_order(SimulationContext* c) {
  (void)c;
  // Falta: Selección por evento, High, luego Low. (Pendiente)
}

bool are_all_processes_in_terminal_state_and_no_work_left(const SimulationContext* c) {
  // Termina cuando:
  // - No hay proceso en CPU
  // - Colas vacías
  // - Todos los procesos están FINISHED o DEAD
  // - No hay más eventos en el futuro cuyo T_EVENTO >= current_tick (en esqueleto aceptamos que queden; no hacen nada aún)
  if (c->cpu_execution_unit.currently_running_process_pointer != NULL) return false;
  if (!is_process_queue_empty(&c->high_priority_mlfq_queue)) return false;
  if (!is_process_queue_empty(&c->low_priority_mlfq_queue)) return false;

  for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; ++i) {
    Process* p = c->simulation_input_data.array_of_process_input_records[i].instantiated_process_pointer;
    if (!p) continue;
    if (p->current_process_state != PROCESS_STATE_FINISHED &&
        p->current_process_state != PROCESS_STATE_DEAD) {
      // Si no está terminado ni muerto, pero tampoco está en cola/CPU,
      // ahora puede deberse a que aún no llega su T_INICIO.
      // Para evitar loop infinito, permitimos terminar cuando el tick ya superó todos los T_INICIO.
      unsigned int t_inicio = c->simulation_input_data.array_of_process_input_records[i].input_start_time_tick;
      if ((long long)t_inicio > c->current_simulation_tick) {
        return false;
      }
    }
  }
  return true;
}

void run_simulation_skeleton_main_loop(SimulationContext* c) {
  const long long maximum_safety_number_of_ticks_to_prevent_infinite_loops = 10LL * 1000LL * 1000LL;

  while (!are_all_processes_in_terminal_state_and_no_work_left(c) &&
         c->current_simulation_tick < maximum_safety_number_of_ticks_to_prevent_infinite_loops) {

    // (Métrica) acumular waiting por tick para procesos READY en colas:
    accumulate_one_tick_of_waiting_time_for_all_ready_processes_in_queue(&c->high_priority_mlfq_queue);
    accumulate_one_tick_of_waiting_time_for_all_ready_processes_in_queue(&c->low_priority_mlfq_queue);

    // Orden del scheduler (stubs por ahora):
    step1_move_processes_from_waiting_to_ready_if_io_completed(c);
    step2_mark_processes_as_dead_if_deadline_reached_in_queues(c);
    step3_update_currently_running_process_with_ordered_rules(c);
    step4_ingress_processes_into_queues_according_to_rules(c);
    step5_recompute_priorities_for_all_ready_processes(c);
    step6_select_next_process_for_cpu_according_to_priority_order(c);

    c->current_simulation_tick += 1;
  }

  if (c->current_simulation_tick >= maximum_safety_number_of_ticks_to_prevent_infinite_loops) {
    fprintf(stderr, "Precaución: se alcanzó el límite de ticks de seguridad en el esqueleto.\n");
  }
}