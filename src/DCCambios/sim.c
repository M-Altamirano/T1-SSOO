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


static void accumulate_one_tick_waiting_for_waiting_processes(SimulationContext* c) {
  for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; ++i) {
    Process* p = c->simulation_input_data.array_of_process_input_records[i].instantiated_process_pointer;
    if (!p) continue;
    if (p->current_process_state == PROCESS_STATE_WAITING) {
      p->accumulated_time_in_ready_or_waiting_states += 1ull;
    }
  }
}


void step1_move_processes_from_waiting_to_ready_if_io_completed(SimulationContext* c) {
  for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; ++i) {
    Process* p = c->simulation_input_data.array_of_process_input_records[i].instantiated_process_pointer;
    if (!p) continue;

    if (p->current_process_state == PROCESS_STATE_WAITING && p->remaining_time_in_current_input_output_wait > 0) {
      p->remaining_time_in_current_input_output_wait -= 1;

      if (p->remaining_time_in_current_input_output_wait == 0) {
        p->current_process_state = PROCESS_STATE_READY;

        // Mantener cola (Regla 4)
        if (p->current_queue_affinity == PROCESS_QUEUE_AFFINITY_HIGH) {
          push_ready_unique(&c->high_priority_mlfq_queue, p);
        } else if (p->current_queue_affinity == PROCESS_QUEUE_AFFINITY_LOW) {
          push_ready_unique(&c->low_priority_mlfq_queue, p);
        } else {
          // fallback seguro
          p->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;
          push_ready_unique(&c->high_priority_mlfq_queue, p);
        }

        // Preparar siguiente ráfaga
        p->remaining_time_in_current_cpu_burst = p->cpu_burst_duration_per_burst;
        // IMPORTANTE: NO reiniciar p->remaining_quantum
      }
    }
  }
}


void step2_mark_processes_as_dead_if_deadline_reached_in_queues(SimulationContext* c) {
  // 1) Marcar en colas y remover
  ProcessQueue* qs[2] = { &c->high_priority_mlfq_queue, &c->low_priority_mlfq_queue };
  for (int qidx = 0; qidx < 2; ++qidx) {
    ProcessQueue* q = qs[qidx];
    for (size_t i = 0; i < q->internal_dynamic_array_size; ) {
      Process* p = q->internal_dynamic_array_of_process_pointers[i];
      if (p->absolute_execution_deadline <= c->current_simulation_tick &&
          p->current_process_state != PROCESS_STATE_DEAD &&
          p->current_process_state != PROCESS_STATE_FINISHED) {

        p->current_process_state = PROCESS_STATE_DEAD;
        p->last_time_tick_when_left_cpu = c->current_simulation_tick;
        p->time_tick_when_finished_or_dead_for_sorting = c->current_simulation_tick;
        if (p->start_time >= 0)
          p->turnaround_time = (unsigned long long)(c->current_simulation_tick - p->start_time);

        push_process_into_process_pool(&c->dead_processes, p);
        remove_process_from_queue(q, i);
        continue; // no i++
      }
      ++i;
    }
  }

  // 2) Asegurar DEAD también para procesos WAITING/READY fuera de colas (modelo permisivo)
  for (unsigned int k = 0; k < c->simulation_input_data.number_of_processes_from_input_file; ++k) {
    Process* p = c->simulation_input_data.array_of_process_input_records[k].instantiated_process_pointer;
    if (!p) continue;
    if (p->current_process_state == PROCESS_STATE_FINISHED ||
        p->current_process_state == PROCESS_STATE_DEAD) continue;

    if (p->absolute_execution_deadline <= c->current_simulation_tick) {
      p->current_process_state = PROCESS_STATE_DEAD;
      p->last_time_tick_when_left_cpu = c->current_simulation_tick;
      p->time_tick_when_finished_or_dead_for_sorting = c->current_simulation_tick;
      if (p->start_time >= 0)
        p->turnaround_time = (unsigned long long)(c->current_simulation_tick - p->start_time);

      push_process_into_process_pool(&c->dead_processes, p);
      // Si estaba en colas, ya lo removimos arriba; si no, no hacemos nada más.
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
      running->last_time_tick_when_left_cpu = c->current_simulation_tick;
      running->time_tick_when_finished_or_dead_for_sorting = c->current_simulation_tick;
      // turnaround = now - T_INICIO
      if (running->start_time >= 0) {
        running->turnaround_time = (unsigned long long)(c->current_simulation_tick - running->start_time);
      }
      push_process_into_process_pool(&(c->dead_processes), running);
      c->cpu_execution_unit.currently_running_process_pointer = NULL;
      return;
    }
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
    running->last_time_tick_when_left_cpu = c->current_simulation_tick;

    // ¿Terminó TODAS las ráfagas?
    if (running->number_of_completed_cpu_bursts >= running->total_number_of_cpu_bursts) {
      running->current_process_state = PROCESS_STATE_FINISHED;
      running->time_tick_when_finished_or_dead_for_sorting = c->current_simulation_tick;
      if (running->start_time >= 0) {
        running->turnaround_time = (unsigned long long)(c->current_simulation_tick - running->start_time);
      }
      push_process_into_process_pool(&(c->finished_processes), running);
      c->cpu_execution_unit.currently_running_process_pointer = NULL;
      return;
    }

    running->current_process_state = PROCESS_STATE_WAITING;
    running->remaining_time_in_current_input_output_wait = running->input_output_wait_time_between_bursts;
    c->cpu_execution_unit.currently_running_process_pointer = NULL;
    return;

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
    running->last_time_tick_when_left_cpu = c->current_simulation_tick;
    c->cpu_execution_unit.currently_running_process_pointer = NULL;
    return;
  }

  // 3.4) Si hay evento en este tick que involucra a otro PID, preemptar YA.
  while (c->next_forced_event_index_to_process < c->simulation_input_data.number_of_forced_cpu_events_from_input_file) {
    ForcedCpuEvent* ev = &c->simulation_input_data.array_of_forced_cpu_events[c->next_forced_event_index_to_process];
    if ((long long)ev->event_time_tick != c->current_simulation_tick) break;

    if (ev->event_process_id != running->process_id) {
      // Preemptar una sola vez; el target se despacha en step6
      running->current_process_state = PROCESS_STATE_READY;
      running->last_time_tick_when_left_cpu = c->current_simulation_tick;
      running->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;
      running->is_priority_forced_to_maximum_due_to_event = true;
      running->number_of_preemption_interruptions += 1;
      push_ready_unique(&c->high_priority_mlfq_queue, running);
      c->cpu_execution_unit.currently_running_process_pointer = NULL;
    }
    break; // no consumir el evento aquí; se consume step6
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

      if (p->remaining_quantum == 0) {
        p->current_queue_affinity = PROCESS_QUEUE_AFFINITY_LOW;
        push_ready_unique(&c->low_priority_mlfq_queue, p);
      } else {
        // Preemptado o cedió por ráfaga → mantiene cola
        if (p->current_queue_affinity == PROCESS_QUEUE_AFFINITY_HIGH)
          push_ready_unique(&c->high_priority_mlfq_queue, p);
        else if (p->current_queue_affinity == PROCESS_QUEUE_AFFINITY_LOW)
          push_ready_unique(&c->low_priority_mlfq_queue, p);
        else {
          p->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;
          push_ready_unique(&c->high_priority_mlfq_queue, p);
        }
      }
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
      if (p->start_time == LL_SENTINEL) p->start_time = c->current_simulation_tick;
      p->remaining_quantum = c->high_priority_mlfq_queue.associated_queue_quantum_in_ticks;
      push_ready_unique(&c->high_priority_mlfq_queue, p);
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
  // if (c->high_priority_mlfq_queue.internal_dynamic_array_size > 0) printf("high\n");
  for (size_t i = 0; i < c->high_priority_mlfq_queue.internal_dynamic_array_size; i++) {
    Process* p = c->high_priority_mlfq_queue.internal_dynamic_array_of_process_pointers[i];
    // printf("%s, %s, %f\n", p->process_name, p->current_process_state == PROCESS_STATE_READY? "READY": "WAIT", p->priority);
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
    // printf("%s, %s, %f\n",p->process_name, p->current_process_state == PROCESS_STATE_READY? "READY": "WAIT", p->priority);
  }
}


void step6_select_next_process_for_cpu_according_to_priority_order(SimulationContext* c) {
  // 1) Procesar TODOS los eventos del tick (el último "gana")
  Process* event_target_to_run = NULL;
  while (c->next_forced_event_index_to_process < c->simulation_input_data.number_of_forced_cpu_events_from_input_file) {
    ForcedCpuEvent* ev = &c->simulation_input_data.array_of_forced_cpu_events[c->next_forced_event_index_to_process];
    if ((long long)ev->event_time_tick != c->current_simulation_tick) break;

    // Buscar target por PID
    Process* target = NULL;
    for (unsigned int i = 0; i < c->simulation_input_data.number_of_processes_from_input_file; ++i) {
      Process* p = c->simulation_input_data.array_of_process_input_records[i].instantiated_process_pointer;
      if (p && p->process_id == ev->event_process_id) { target = p; break; }
    }

    if (target) {
      // Asegurar que NO quede en colas dos veces
      remove_if_present_from_queue(&c->high_priority_mlfq_queue, target);
      remove_if_present_from_queue(&c->low_priority_mlfq_queue, target);

      // Si estaba en WAITING, el evento lo trae igual a CPU
      target->remaining_time_in_current_input_output_wait = 0;
      target->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;

      event_target_to_run = target;  // el último en el mismo tick prevalece
    }
    c->next_forced_event_index_to_process += 1;
  }

  // 2) Si hubo evento, despacharlo inmediatamente
  if (event_target_to_run != NULL) {
    Process* cur = c->cpu_execution_unit.currently_running_process_pointer;
    if (cur && cur != event_target_to_run) {
      // Por si step3 no alcanzó a preemptar (seguro redundante pero inocuo)
      cur->current_process_state = PROCESS_STATE_READY;
      cur->last_time_tick_when_left_cpu = c->current_simulation_tick;
      cur->current_queue_affinity = PROCESS_QUEUE_AFFINITY_HIGH;
      cur->is_priority_forced_to_maximum_due_to_event = true;
      cur->number_of_preemption_interruptions += 1;
      push_ready_unique(&c->high_priority_mlfq_queue, cur);
    }

    c->cpu_execution_unit.currently_running_process_pointer = event_target_to_run;
    event_target_to_run->current_process_state = PROCESS_STATE_RUNNING;

    if (!event_target_to_run->has_ever_entered_cpu_at_least_once) {
      event_target_to_run->has_ever_entered_cpu_at_least_once = true;
      event_target_to_run->first_time_tick_when_entered_cpu = c->current_simulation_tick;
      if (event_target_to_run->start_time >= 0)
        event_target_to_run->response_time = (unsigned long long)(c->current_simulation_tick - event_target_to_run->start_time);
    }

    if (event_target_to_run->remaining_quantum == 0) {
      event_target_to_run->remaining_quantum = c->high_priority_mlfq_queue.associated_queue_quantum_in_ticks;
    }
    // Si venía con prioridad forzada por evento anterior y por fin entró, no es necesario limpiarla aquí;
    // la prioridad forzada afecta solo al orden en colas, y ahora no está en cola.

    return;
  }

  // 3) Selección normal: High luego Low (READY)
  if (c->cpu_execution_unit.currently_running_process_pointer &&
      c->cpu_execution_unit.currently_running_process_pointer->current_process_state == PROCESS_STATE_RUNNING) {
    return; // sigue corriendo
  }

  Process* best = pop_best_ready_process_from_process_queue(&c->high_priority_mlfq_queue);
  if (!best) best = pop_best_ready_process_from_process_queue(&c->low_priority_mlfq_queue);

  c->cpu_execution_unit.currently_running_process_pointer = best;

  if (best) {
    best->current_process_state = PROCESS_STATE_RUNNING;

    if (!best->has_ever_entered_cpu_at_least_once) {
      best->has_ever_entered_cpu_at_least_once = true;
      best->first_time_tick_when_entered_cpu = c->current_simulation_tick;
      if (best->start_time >= 0)
        best->response_time = (unsigned long long)(c->current_simulation_tick - best->start_time);
    }

    if (best->remaining_quantum == 0) {
      best->remaining_quantum = (best->current_queue_affinity == PROCESS_QUEUE_AFFINITY_HIGH)
        ? c->high_priority_mlfq_queue.associated_queue_quantum_in_ticks
        : c->low_priority_mlfq_queue.associated_queue_quantum_in_ticks;
    }

    // Ya ingresó a CPU: limpia bandera de prioridad forzada para su próximo ciclo
    best->is_priority_forced_to_maximum_due_to_event = false;
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
    
    accumulate_one_tick_waiting_for_waiting_processes(c); // Métrica waiting para procesos en WAITING

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