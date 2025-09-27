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
  for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; ++i) {
    Process* p = c->simulation_input_data.array_of_process_input_records[i].instantiated_process_pointer;
    if (!p) continue;

    if (p->current_process_state == PROCESS_STATE_WAITING && p->remaining_time_in_current_input_output_wait > 0) {
      p->remaining_time_in_current_input_output_wait -= 1;

      if (p->remaining_time_in_current_input_output_wait == 0) {
        // Vuelve a READY y entra a High (regla 4.2/ingresos)
        p->current_process_state = PROCESS_STATE_READY;
        p->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;
        push_ready_process_into_process_queue(&c->high_priority_mlfq_queue, p);

        // Resetea burst para la siguiente ráfaga:
        p->remaining_time_in_current_cpu_burst = p->cpu_burst_duration_per_burst;
      }
    }
  }
}


void step2_mark_processes_as_dead_if_deadline_reached_in_queues(SimulationContext* c) {
  Process** process_array = (c->high_priority_mlfq_queue).internal_dynamic_array_of_process_pointers;
  for (size_t i = 0; i < c->high_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    if (process_array[i]->absolute_execution_deadline <= c->current_simulation_tick) {
      if (process_array[i]->current_process_state != PROCESS_STATE_DEAD &&
          process_array[i]->current_process_state != PROCESS_STATE_FINISHED) {
        process_array[i]->current_process_state = PROCESS_STATE_DEAD;
        
        process_array[i]->time_tick_when_finished_or_dead_for_sorting = c->current_simulation_tick;
        push_process_into_process_pool(&(c->dead_processes), process_array[i]);
      }
      // Sácarla de la cola
      remove_process_from_queue(&(c->high_priority_mlfq_queue), i);
      i--; // compactada la cola
    }
  }
  process_array = (c->low_priority_mlfq_queue).internal_dynamic_array_of_process_pointers;
  for (size_t i = 0; i < c->low_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    if (process_array[i]->absolute_execution_deadline <= c->current_simulation_tick) {
      if (process_array[i]->current_process_state != PROCESS_STATE_DEAD &&
          process_array[i]->current_process_state != PROCESS_STATE_FINISHED) {
        process_array[i]->current_process_state = PROCESS_STATE_DEAD;
        
        process_array[i]->time_tick_when_finished_or_dead_for_sorting = c->current_simulation_tick;
        push_process_into_process_pool(&(c->dead_processes), process_array[i]);
      }
      remove_process_from_queue(&(c->low_priority_mlfq_queue), i);
      i--; 
    }
  }
}


void step3_update_currently_running_process_with_ordered_rules(SimulationContext* c) {
  Process* running = c->cpu_execution_unit.currently_running_process_pointer;
  if (!running) return;

  // 3.1 Deadline alcanzado (usar <= por seguridad frente a ticks saltados)
  if (running->absolute_execution_deadline <= c->current_simulation_tick) {
    if (running->current_process_state != PROCESS_STATE_DEAD &&
        running->current_process_state != PROCESS_STATE_FINISHED) {
      running->current_process_state = PROCESS_STATE_DEAD;
      running->time_tick_when_finished_or_dead_for_sorting = c->current_simulation_tick;
      push_process_into_process_pool(&(c->dead_processes), running);
    }
    c->cpu_execution_unit.currently_running_process_pointer = NULL;
    return;
  }

  // Avanzar 1 tick de ejecución
  if (running->remaining_time_in_current_cpu_burst > 0) {
    running->remaining_time_in_current_cpu_burst -= 1;
  }
  if (running->remaining_quantum > 0) {
    running->remaining_quantum -= 1;
  }

  // 3.2) ¿Terminó la ráfaga actual?
  if (running->remaining_time_in_current_cpu_burst == 0) {
    running->number_of_completed_cpu_bursts += 1;

    // ¿Terminó TODAS las ráfagas?
    if (running->number_of_completed_cpu_bursts >= running->total_number_of_cpu_bursts) {
      running->current_process_state = PROCESS_STATE_FINISHED;
      running->time_tick_when_finished_or_dead_for_sorting = c->current_simulation_tick;
      push_process_into_process_pool(&(c->finished_processes), running);
      c->cpu_execution_unit.currently_running_process_pointer = NULL;
      return;
    }

    // Aún quedan ráfagas: pasa a WAITING por IO
    running->current_process_state = PROCESS_STATE_WAITING;
    running->remaining_time_in_current_input_output_wait = running->input_output_wait_time_between_bursts;

    // Sale de CPU
    c->cpu_execution_unit.currently_running_process_pointer = NULL;
    return;
  }

  // 3.3) ¿Se acabó el quantum sin terminar la ráfaga?
  if (running->remaining_quantum == 0) {
    running->current_process_state = PROCESS_STATE_READY;
    // Democión a Low se hará en paso 4.1
    c->cpu_execution_unit.currently_running_process_pointer = NULL;
    return;
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
  // 4.1 Si un proceso salió de CPU por fin de quantum (estado READY y burst no terminó), encolarlo en Low (Este estado lo dejó el step3 al poner remaining_quantum == 0 y state READY)
  for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; ++i) {
    Process* p = c->simulation_input_data.array_of_process_input_records[i].instantiated_process_pointer;
    if (!p) continue;
    if (p->current_process_state == PROCESS_STATE_READY &&
        p->remaining_time_in_current_cpu_burst > 0 &&
        p != c->cpu_execution_unit.currently_running_process_pointer) {

      p->current_queue_affinity = PROCESS_QUEUE_AFFINITY_LOW;
      push_ready_process_into_process_queue(&c->low_priority_mlfq_queue, p);
    }
  }

  // 4.2 Ingresos por T_INICIO
  for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; ++i) {
    ProcessInputRecord* rec = &c->simulation_input_data.array_of_process_input_records[i];
    Process* p = rec->instantiated_process_pointer;
    if (!p) continue;
    if ((unsigned int)c->current_simulation_tick == rec->input_start_time_tick) {
      p->current_process_state = PROCESS_STATE_READY;
      p->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;
      push_ready_process_into_process_queue(&c->high_priority_mlfq_queue, p);
    }
  }

  // 4.3) Promoción Low → High por urgencia
  Process* temp_array[1024];
  size_t temp_size = 0;

  while (!is_process_queue_empty(&c->low_priority_mlfq_queue)) {
    Process* p = pop_best_ready_process_from_process_queue(&c->low_priority_mlfq_queue);
    if (!p) break;

    long long lcpu = p->last_time_tick_when_left_cpu; // puede ser -1 si nunca salió
    bool must_promote = (2LL * (long long)p->absolute_execution_deadline) < (c->current_simulation_tick - lcpu);

    if (must_promote) {
      p->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;
      push_ready_process_into_process_queue(&c->high_priority_mlfq_queue, p);
    } else {
      temp_array[temp_size++] = p;
    }
  }

  // Volver a meter los que no se promovieron
  for (size_t i = 0; i < temp_size; ++i) {
    push_ready_process_into_process_queue(&c->low_priority_mlfq_queue, temp_array[i]);
  }
}


void step5_recompute_priorities_for_all_ready_processes(SimulationContext* c) {
  for (size_t i = 0; i < c->high_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    Process* p = c->high_priority_mlfq_queue.internal_dynamic_array_of_process_pointers[i];
    p->priority = compute_effective_priority_value_for_process(p, c->current_simulation_tick);
  }
  qsort_with_tick(c->high_priority_mlfq_queue.internal_dynamic_array_of_process_pointers,
                  c->high_priority_mlfq_queue.internal_dynamic_array_size);
  if (c->high_priority_mlfq_queue.internal_dynamic_array_size > 0) printf("high\n");
  for (size_t i = 0; i < c->high_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    Process* p = c->high_priority_mlfq_queue.internal_dynamic_array_of_process_pointers[i];
    printf("%s, %s, %f\n", p->process_name, p->current_process_state == PROCESS_STATE_READY? "READY": "WAIT", p->priority);
  }
  for (size_t i = 0; i < c->low_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    Process* p = c->low_priority_mlfq_queue.internal_dynamic_array_of_process_pointers[i];
    p->priority = compute_effective_priority_value_for_process(p, c->current_simulation_tick);
    }
  qsort_with_tick(c->low_priority_mlfq_queue.internal_dynamic_array_of_process_pointers,
                  c->low_priority_mlfq_queue.internal_dynamic_array_size);
  if (c->low_priority_mlfq_queue.internal_dynamic_array_size > 0) printf("low\n");
  for (size_t i = 0; i < c->low_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    Process* p = c->low_priority_mlfq_queue.internal_dynamic_array_of_process_pointers[i];
    printf("%s, %s, %f\n",p->process_name, p->current_process_state == PROCESS_STATE_READY? "READY": "WAIT", p->priority);
  }
}


void step6_select_next_process_for_cpu_according_to_priority_order(SimulationContext* c) {
  // Si alguien sigue en RUNNING, no cambiar
  Process* current = c->cpu_execution_unit.currently_running_process_pointer;
  if (current && current->current_process_state == PROCESS_STATE_RUNNING) {
    return;
  }

  // 6.1) Procesar eventos del tick
  while (c->next_forced_event_index_to_process < c->simulation_input_data.number_of_forced_cpu_events_from_input_file) {
    ForcedCpuEvent* ev = &c->simulation_input_data.array_of_forced_cpu_events[c->next_forced_event_index_to_process];
    if ((long long)ev->event_time_tick != c->current_simulation_tick) break;

    // Buscar proceso objetivo por PID
    Process* target = NULL;
    for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; ++i) {
      Process* p = c->simulation_input_data.array_of_process_input_records[i].instantiated_process_pointer;
      if (p && p->process_id == ev->event_process_id) { target = p; break; }
    }

    if (target) {
      // Forzar a RUNNING en High
      target->current_process_state = PROCESS_STATE_RUNNING;
      target->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;
      target->remaining_quantum = c->high_priority_mlfq_queue.associated_queue_quantum_in_ticks;

      c->cpu_execution_unit.currently_running_process_pointer = target;

      if (!target->has_ever_entered_cpu_at_least_once) {
        target->has_ever_entered_cpu_at_least_once = true;
        target->first_time_tick_when_entered_cpu = c->current_simulation_tick; // <-- marca de primera entrada
        target->response_time = (unsigned long long)c->current_simulation_tick - target->start_time;
      }

      c->next_forced_event_index_to_process += 1;
      return;
    } else {
      c->next_forced_event_index_to_process += 1;
    }
  }

  // 6.2/6.3 Selección normal: High primero, luego Low
  Process* best_process = pop_best_ready_process_from_process_queue(&c->high_priority_mlfq_queue);
  if (best_process == NULL) {
    best_process = pop_best_ready_process_from_process_queue(&c->low_priority_mlfq_queue);
  }

  c->cpu_execution_unit.currently_running_process_pointer = best_process;

  if (best_process != NULL) {
    best_process->current_process_state = PROCESS_STATE_RUNNING;

    if (!best_process->has_ever_entered_cpu_at_least_once) {
      best_process->has_ever_entered_cpu_at_least_once = true;
      best_process->first_time_tick_when_entered_cpu = c->current_simulation_tick; // <-- marca de primera entrada
      best_process->response_time = (unsigned long long)c->current_simulation_tick - best_process->start_time;
    }

    if (best_process->current_queue_affinity == PROCESS_QUEUE_AFFINITY_HIGH) {
      best_process->remaining_quantum = c->high_priority_mlfq_queue.associated_queue_quantum_in_ticks;
    } else {
      best_process->remaining_quantum = c->low_priority_mlfq_queue.associated_queue_quantum_in_ticks;
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

  // Si existe un proceso que ya debería estar "activo" (T_INICIO <= tick)
  // y que NO está FINISHED/DEAD, aún hay trabajo.
  for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; ++i) {
    const ProcessInputRecord* rec = &c->simulation_input_data.array_of_process_input_records[i];
    const Process* p = rec->instantiated_process_pointer;
    if (!p) continue;

    if ((long long)rec->input_start_time_tick <= c->current_simulation_tick &&
        p->current_process_state != PROCESS_STATE_FINISHED &&
        p->current_process_state != PROCESS_STATE_DEAD) {
      return false;
    }
  }

  // Si aún no llega el T_INICIO de alguien, no podemos terminar (para no cortar antes).
  for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; ++i) {
    const ProcessInputRecord* rec = &c->simulation_input_data.array_of_process_input_records[i];
    if ((long long)rec->input_start_time_tick > c->current_simulation_tick) {
      return false;
    }
  }

  return true;
}


void run_simulation_skeleton_main_loop(SimulationContext* c) {
  const long long maximum_safety_number_of_ticks_to_prevent_infinite_loops = 10LL * 1000LL * 1000LL;

  while (!are_all_processes_in_terminal_state_and_no_work_left(c) &&
         c->current_simulation_tick < maximum_safety_number_of_ticks_to_prevent_infinite_loops) {

    // (Métrica) acumular waiting por tick para procesos READY en colas:
    accumulate_one_tick_of_waiting_time_for_all_ready_processes_in_queue(&(c->high_priority_mlfq_queue));
    accumulate_one_tick_of_waiting_time_for_all_ready_processes_in_queue(&(c->low_priority_mlfq_queue));

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