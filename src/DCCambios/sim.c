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

  return true;
}

void destroy_simulation_context(SimulationContext* c) {
  if (!c) return;

  // Destruir colas (no destruye procesos, eso lo hace destroy_simulation_input_data)
  destroy_process_queue(&c->high_priority_mlfq_queue);
  destroy_process_queue(&c->low_priority_mlfq_queue);

  destroy_process_pool(&c->dead_processes);
  destroy_process_pool(&c->finished_processes);

  // Destruir input (incluye procesos y eventos)
  destroy_simulation_input_data(&c->simulation_input_data);

  memset(c, 0, sizeof(*c));
}

void step1_move_processes_from_waiting_to_ready_if_io_completed(SimulationContext* c) {
  // (Métrica) acumular waiting por tick para procesos READY en colas:
  accumulate_one_tick_of_waiting_time_for_all_ready_processes_in_queue(c->high_priority_mlfq_queue.internal_dynamic_array_of_process_pointers, c->high_priority_mlfq_queue.internal_dynamic_array_size);
  accumulate_one_tick_of_waiting_time_for_all_ready_processes_in_queue(c->low_priority_mlfq_queue.internal_dynamic_array_of_process_pointers, c->low_priority_mlfq_queue.internal_dynamic_array_size);

  Process** process_array = (c->high_priority_mlfq_queue).internal_dynamic_array_of_process_pointers;
  for (size_t i = 0; i < c->high_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    if (process_array[i]->current_process_state == PROCESS_STATE_WAITING) {
      process_array[i]->remaining_time_in_current_input_output_wait -= (unsigned int) 1;
      if (process_array[i]->remaining_time_in_current_input_output_wait == 0) {
        process_array[i]->current_process_state = PROCESS_STATE_READY;
      }
    }
  }
  process_array = (c->low_priority_mlfq_queue).internal_dynamic_array_of_process_pointers;
  for (size_t i = 0; i < c->low_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    if (process_array[i]->current_process_state == PROCESS_STATE_WAITING) {
      process_array[i]->remaining_time_in_current_input_output_wait -= 1u;
      if (process_array[i]->remaining_time_in_current_input_output_wait == 0) {
        process_array[i]->current_process_state = PROCESS_STATE_READY;
      }
    }
  }
}

void step2_mark_processes_as_dead_if_deadline_reached_in_queues(SimulationContext* c) {
  Process** process_array = (c->high_priority_mlfq_queue).internal_dynamic_array_of_process_pointers;
  for (size_t i = 0; i < c->high_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    if (process_array[i]->absolute_execution_deadline == c->current_simulation_tick) {
      process_array[i]->current_process_state = PROCESS_STATE_DEAD;
      process_array[i]->turnaround_time = c->current_simulation_tick - process_array[i]->start_time;
      push_process_into_process_pool(&(c->dead_processes), process_array[i]);
      remove_process_from_queue(&(c->high_priority_mlfq_queue), i);
      i--;
    }
  }
  process_array = (c->low_priority_mlfq_queue).internal_dynamic_array_of_process_pointers;
  for (size_t i = 0; i < c->low_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    if (process_array[i]->absolute_execution_deadline == c->current_simulation_tick) {
      process_array[i]->current_process_state = PROCESS_STATE_DEAD;
      process_array[i]->turnaround_time = c->current_simulation_tick - process_array[i]->start_time;
      push_process_into_process_pool(&(c->dead_processes), process_array[i]);
      remove_process_from_queue(&(c->low_priority_mlfq_queue), i);
      i--;
    }
  }
}

void step3_update_currently_running_process_with_ordered_rules(SimulationContext* c) {
  Process* current_running_process = c->cpu_execution_unit.currently_running_process_pointer;
  if (current_running_process == NULL) return;
  current_running_process->remaining_time_in_current_cpu_burst--;
  c->cpu_execution_unit.currently_running_process_pointer->remaining_quantum--;
  if (!(current_running_process->has_ever_entered_cpu_at_least_once)) {
        current_running_process->has_ever_entered_cpu_at_least_once = true;
        current_running_process->response_time = (unsigned long long) c->current_simulation_tick - current_running_process->start_time;
      }
  // 3.1
  if (current_running_process->absolute_execution_deadline <= c->current_simulation_tick) {
    current_running_process->current_process_state = PROCESS_STATE_DEAD;
    current_running_process->turnaround_time = c->current_simulation_tick - current_running_process->start_time;
    push_process_into_process_pool(&(c->dead_processes), current_running_process);

    c->cpu_execution_unit.currently_running_process_pointer = NULL;
    printf("what the actual fuck\n");
    return;
  }
  // 3.2
  if (current_running_process->remaining_time_in_current_cpu_burst == 0) {
    current_running_process->number_of_completed_cpu_bursts += (unsigned int) 1;
    if (current_running_process->number_of_completed_cpu_bursts == current_running_process->total_number_of_cpu_bursts) {
      current_running_process->current_process_state = PROCESS_STATE_FINISHED;
      current_running_process->turnaround_time = c->current_simulation_tick - current_running_process->start_time;
      push_process_into_process_pool(&(c->finished_processes), current_running_process);
    }
    else {
      current_running_process->current_process_state = PROCESS_STATE_WAITING;
      current_running_process->remaining_time_in_current_input_output_wait = current_running_process->input_output_wait_time_between_bursts;
      current_running_process->number_of_preemption_interruptions++;
      current_running_process->last_time_tick_when_left_cpu = c->current_simulation_tick;
      current_running_process->remaining_time_in_current_cpu_burst = current_running_process->cpu_burst_duration_per_burst;
    }
    return;
  }
  // 3.3
  if (c->cpu_execution_unit.currently_running_process_pointer->remaining_quantum == 0) {
    current_running_process->current_process_state = PROCESS_STATE_READY;
    current_running_process->last_time_tick_when_left_cpu = c->current_simulation_tick;
    return;
  }
  // 3.4
  bool remaining_events = !( c->simulation_input_data.number_of_forced_cpu_events_from_input_file == c->next_forced_event_index_to_process);
  if (remaining_events) {
    unsigned int next_event_tick = c->simulation_input_data.array_of_forced_cpu_events[c->next_forced_event_index_to_process].event_time_tick;
    if (c->current_simulation_tick == next_event_tick) {
      current_running_process->current_process_state = PROCESS_STATE_READY;
      current_running_process->is_priority_forced_to_maximum_due_to_event = true;
      current_running_process->number_of_preemption_interruptions++;
    }
  }
}

static void ingress_new_arrivals_into_high_queue_when_start_time_matches_tick(
  SimulationContext* c
) {
  // parte 4.2: cuando tick == T_INICIO, ingresar a High en READY.
  for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; i++) {
    ProcessInputRecord* rec = &c->simulation_input_data.array_of_process_input_records[i];
    Process* p = rec->instantiated_process_pointer;
    if (!p) continue;

    if (rec->input_start_time_tick == (unsigned int)c->current_simulation_tick) {
      p->current_process_state = PROCESS_STATE_READY;
      p->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;
      p->start_time = c->current_simulation_tick;
      if (!push_ready_process_into_process_queue(&(c->high_priority_mlfq_queue), p)) printf("fuck\n");
      p->remaining_quantum = c->high_priority_mlfq_queue.associated_queue_quantum_in_ticks;
      
    }
  }
}

void step4_ingress_processes_into_queues_according_to_rules(SimulationContext* c) {
  // 4.1) El que salió de CPU
  Process* current_running_process = c->cpu_execution_unit.currently_running_process_pointer;
  if (current_running_process != NULL) {
    if (current_running_process->current_process_state != PROCESS_STATE_RUNNING) {
      switch (current_running_process->current_process_state)
      {
      case PROCESS_STATE_READY:
        if (current_running_process->is_priority_forced_to_maximum_due_to_event) {
          current_running_process->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;
          if (!push_ready_process_into_process_queue(&(c->high_priority_mlfq_queue), current_running_process)) printf("fuck ready");
        }
        else {
          current_running_process->current_queue_affinity = PROCESS_QUEUE_AFFINITY_LOW;
          if (!push_ready_process_into_process_queue(&(c->low_priority_mlfq_queue), current_running_process)) printf("fuck ready");
          current_running_process->remaining_quantum = c->low_priority_mlfq_queue.associated_queue_quantum_in_ticks;
        }
        c->cpu_execution_unit.currently_running_process_pointer = NULL;
        break;

      case PROCESS_STATE_WAITING:
        if (current_running_process->current_queue_affinity == PROCESS_QUEUE_AFFINITY_HIGH) {
          push_ready_process_into_process_queue(&(c->high_priority_mlfq_queue), current_running_process);
        }
        else if (current_running_process->current_queue_affinity == PROCESS_QUEUE_AFFINITY_LOW) {
          push_ready_process_into_process_queue(&(c->low_priority_mlfq_queue), current_running_process);
        }
        c->cpu_execution_unit.currently_running_process_pointer = NULL;
        break;
      
      default:
        c->cpu_execution_unit.currently_running_process_pointer = NULL;
        break;
      }
    }
  }
  // 4.2) Ingresos por T_INICIO:
  ingress_new_arrivals_into_high_queue_when_start_time_matches_tick(c);
  // 4.3) Subir Low->High por condición de urgencia: pendiente
  for (size_t i = 0; i < c->low_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    Process* low_priority_process = c->low_priority_mlfq_queue.internal_dynamic_array_of_process_pointers[i];
    if (low_priority_process->absolute_execution_deadline * 2 < c->current_simulation_tick - low_priority_process->last_time_tick_when_left_cpu) {
      low_priority_process->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;
      push_ready_process_into_process_queue(&(c->high_priority_mlfq_queue), low_priority_process);
      remove_process_from_queue(&(c->low_priority_mlfq_queue), i);
      i--;
    } 
  }
}

void step5_recompute_priorities_for_all_ready_processes(SimulationContext* c) {
  for (size_t i = 0; i < c->high_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    Process* p = c->high_priority_mlfq_queue.internal_dynamic_array_of_process_pointers[i];
    p->priority = compute_effective_priority_value_for_process(p, c->current_simulation_tick);
  }
  qsort_with_tick(c->high_priority_mlfq_queue.internal_dynamic_array_of_process_pointers,
                  c->high_priority_mlfq_queue.internal_dynamic_array_size);
  for (size_t i = 0; i < c->low_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    Process* p = c->low_priority_mlfq_queue.internal_dynamic_array_of_process_pointers[i];
    p->priority = compute_effective_priority_value_for_process(p, c->current_simulation_tick);
    }
  qsort_with_tick(c->low_priority_mlfq_queue.internal_dynamic_array_of_process_pointers,
                  c->low_priority_mlfq_queue.internal_dynamic_array_size);
}

void step6_select_next_process_for_cpu_according_to_priority_order(SimulationContext* c) {
  Process* current_cpu_process = c->cpu_execution_unit.currently_running_process_pointer;
  // if (current_cpu_process == NULL) {
  //   Process* best_process =  pop_best_ready_process_from_process_queue(&(c->high_priority_mlfq_queue));
  //   if (best_process == NULL) pop_best_ready_process_from_process_queue(&(c->low_priority_mlfq_queue));
  //   if (best_process != NULL) {
  //     c->cpu_execution_unit.currently_running_process_pointer = best_process;
  //     c->cpu_execution_unit.currently_running_process_pointer->current_process_state = PROCESS_STATE_RUNNING;
  //   }
  //   return;
  // }
  if (current_cpu_process == NULL) {
    Process* new_process_in_cpu = NULL;
    if (c->simulation_input_data.number_of_forced_cpu_events_from_input_file > c->next_forced_event_index_to_process) {
      unsigned int next_event_tick = c->simulation_input_data.array_of_forced_cpu_events[c->next_forced_event_index_to_process].event_time_tick;
      if (c->current_simulation_tick == next_event_tick) {
        unsigned int event_id = c->simulation_input_data.array_of_forced_cpu_events[c->next_forced_event_index_to_process].event_process_id;
        Process** p_array = c->high_priority_mlfq_queue.internal_dynamic_array_of_process_pointers;
        for (size_t i = 0; i < c->high_priority_mlfq_queue.internal_dynamic_array_size; i++) {
          if (p_array[i]->process_id == event_id) {
            new_process_in_cpu = p_array[i];
            remove_process_from_queue(&c->high_priority_mlfq_queue, i);
            break;
          }
        }
        p_array = c->low_priority_mlfq_queue.internal_dynamic_array_of_process_pointers;
        for (size_t i = 0; i < c->low_priority_mlfq_queue.internal_dynamic_array_size; i++) {
          if (p_array[i]->process_id == event_id) {
            new_process_in_cpu = p_array[i];
            remove_process_from_queue(&c->high_priority_mlfq_queue, i);
            break;
          }
        }
        p_array = c->dead_processes.internal_dynamic_array_of_process_pointers;
        for (size_t i = 0; i < c->dead_processes.internal_dynamic_array_size; i++) {
          if (p_array[i]->process_id == event_id) {
            new_process_in_cpu = p_array[i];
            remove_process_from_pool(&c->dead_processes, i);
          }
        }
        p_array = c->finished_processes.internal_dynamic_array_of_process_pointers;
        for (size_t i = 0; i < c->finished_processes.internal_dynamic_array_size; i++) {
          if (p_array[i]->process_id == event_id) {
            new_process_in_cpu = p_array[i];
            remove_process_from_pool(&c->finished_processes, i);
          }
        }
        c->next_forced_event_index_to_process += (size_t) 1;
        printf("next event: %zu", c->next_forced_event_index_to_process);
      }
    }
    if (c->high_priority_mlfq_queue.internal_dynamic_array_size >= 1 && new_process_in_cpu == NULL) {
      Process* best_process = pop_best_ready_process_from_process_queue(&(c->high_priority_mlfq_queue));
      if (best_process->current_process_state == PROCESS_STATE_READY) new_process_in_cpu = best_process;
      else push_ready_process_into_process_queue(&(c->high_priority_mlfq_queue), best_process);
    }
    if (c->low_priority_mlfq_queue.internal_dynamic_array_size >= 1 && new_process_in_cpu == NULL) {
      Process* best_process = pop_best_ready_process_from_process_queue(&(c->low_priority_mlfq_queue));
      if (best_process->current_process_state == PROCESS_STATE_READY) new_process_in_cpu = best_process;
      else push_ready_process_into_process_queue(&(c->low_priority_mlfq_queue), best_process);
    }
    c->cpu_execution_unit.currently_running_process_pointer = new_process_in_cpu;
    if (new_process_in_cpu != NULL) {
      new_process_in_cpu->current_process_state = PROCESS_STATE_RUNNING;
      printf("CPU PROCESS: %s\n", new_process_in_cpu->process_name);
      printf("CURRENT EVENT: %zu", c->next_forced_event_index_to_process);
    }
  }
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
  if (c->simulation_input_data.number_of_forced_cpu_events_from_input_file > c->next_forced_event_index_to_process) return false;

  for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; i++) {
    Process* p = c->simulation_input_data.array_of_process_input_records[i].instantiated_process_pointer;
    if (!p) continue;
    if (p->current_process_state != PROCESS_STATE_FINISHED &&
        p->current_process_state != PROCESS_STATE_DEAD) {
      // Si no está terminado ni muerto, pero tampoco está en cola/CPU,
      // ahora puede deberse a que aún no llega su T_INICIO.
      // Para evitar loop infinito, permitimos terminar cuando el tick ya superó todos los T_INICIO.
      // unsigned int t_inicio = c->simulation_input_data.array_of_process_input_records[i].input_start_time_tick;
      // if ((long long)t_inicio > c->current_simulation_tick) {
      //   return false;
      // }
      return false;
    }
  }
  return true;
}




void run_simulation_skeleton_main_loop(SimulationContext* c) {
  const long long maximum_safety_number_of_ticks_to_prevent_infinite_loops = 10LL * 1000LL * 1000LL;

  while (!are_all_processes_in_terminal_state_and_no_work_left(c) &&
         c->current_simulation_tick < maximum_safety_number_of_ticks_to_prevent_infinite_loops) {

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