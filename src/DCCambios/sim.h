#ifndef SIM_H
#define SIM_H

#include "io.h"
#include "queue.h"
#include "events.h"
#include "process.h"
#include <stdbool.h>

typedef struct CpuExecutionUnit {
  Process* currently_running_process_pointer;
} CpuExecutionUnit;

typedef struct SimulationContext {
  // Datos base:
  SimulationInputData simulation_input_data;

  // Colas MLFQ:
  ProcessQueue high_priority_mlfq_queue;
  ProcessQueue low_priority_mlfq_queue;

  // Pools de FINISHED y RUNNING
  ProcessPool finished_processes;
  ProcessPool dead_processes;

  // CPU:
  CpuExecutionUnit cpu_execution_unit;

  // Tiempo y control:
  long long current_simulation_tick;
  size_t next_forced_event_index_to_process;

} SimulationContext;

// Inicializa colas, CPU y variables a partir del input ya leído:
bool initialize_simulation_context_from_input(
  SimulationContext* simulation_context_pointer,
  const SimulationInputData* source_input_data
);

// Libera recursos del contexto (y de simulation_input_data):
void destroy_simulation_context(SimulationContext* simulation_context_pointer);

// Corre el ESQUELETO del scheduler: orden 1→6 invocando funciones "stub" (sin lógica completa aún)
void run_simulation_skeleton_main_loop(SimulationContext* simulation_context_pointer);

// ---- Stubs (pendiente): Paso 1→6. Por ahora sin implementación de reglas finas.
// Se dejan claramente separadas para implementar despues.

void step1_move_processes_from_waiting_to_ready_if_io_completed(SimulationContext* c);
void step2_mark_processes_as_dead_if_deadline_reached_in_queues(SimulationContext* c);
void step3_update_currently_running_process_with_ordered_rules(SimulationContext* c);
void step4_ingress_processes_into_queues_according_to_rules(SimulationContext* c);
void step5_recompute_priorities_for_all_ready_processes(SimulationContext* c);
void step6_select_next_process_for_cpu_according_to_priority_order(SimulationContext* c);

// Funciones auxiliares:
bool are_all_processes_in_terminal_state_and_no_work_left(const SimulationContext* c);

#endif // SIM_H