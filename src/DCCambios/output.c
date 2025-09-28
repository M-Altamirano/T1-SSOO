#include "output.h"
#include "sim.h"       // SimulationContext + ProcessInputRecord (via io.h)
#include "process.h"   // Process, ProcessPool

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>    // ULLONG_MAX


typedef struct ProcessPrintableRow {
  Process* process_pointer;
  unsigned int start_time_tick_for_this_process; // T_INICIO
  long long finish_tick_for_sort;                // clave primaria de orden
} ProcessPrintableRow;

// Busca T_INICIO por PID
static unsigned int find_start_time_for_pid(const SimulationContext* sim, unsigned int pid) {
  for (unsigned int k = 0; k < sim->simulation_input_data.number_of_processes_from_input_file; ++k) {
    const ProcessInputRecord* rec = &sim->simulation_input_data.array_of_process_input_records[k];
    if (rec->input_process_id == pid) return rec->input_start_time_tick;
  }
  return 0;
}

// Orden: primero por tiempo de término, luego por PID
static int compare_rows_by_finish_time_then_pid(const void* a, const void* b) {
  const ProcessPrintableRow* ra = (const ProcessPrintableRow*)a;
  const ProcessPrintableRow* rb = (const ProcessPrintableRow*)b;

  if (ra->finish_tick_for_sort < rb->finish_tick_for_sort) return -1;
  if (ra->finish_tick_for_sort > rb->finish_tick_for_sort) return 1;

  unsigned int pa = ra->process_pointer->process_id;
  unsigned int pb = rb->process_pointer->process_id;

  if (pa < pb) return -1;
  if (pa > pb) return 1;
  return 0;
}

// Inserta o actualiza la "mejor" fila por PID (evita duplicados)
static void update_or_insert_row(ProcessPrintableRow* rows,
                                 size_t* m,
                                 Process* p,
                                 const SimulationContext* sim) {
  if (!p) return;

  unsigned int t_inicio = find_start_time_for_pid(sim, p->process_id);

  // Reconstruir "finish tick" para ordenar si no está seteado
  long long finish_tick = p->time_tick_when_finished_or_dead_for_sorting; // puede ser -1
  if (finish_tick < 0) {
    // Fallback: si turnaround es válido, finish = T_INICIO + turnaround
    if (p->turnaround_time != ULLONG_MAX) {
      finish_tick = (long long)t_inicio + (long long)p->turnaround_time;
    } else {
      // último fallback conservador: usar el tick actual
      finish_tick = (long long)sim->current_simulation_tick;
    }
  }

  // Si ya existe una fila con este PID, conserva la que tenga finish_tick mayor (más reciente)
  for (size_t i = 0; i < *m; ++i) {
    if (rows[i].process_pointer->process_id == p->process_id) {
      if (finish_tick > rows[i].finish_tick_for_sort) {
        rows[i].process_pointer = p;
        rows[i].start_time_tick_for_this_process = t_inicio;
        rows[i].finish_tick_for_sort = finish_tick;
      }
      return;
    }
  }

  // Nueva entrada
  rows[*m].process_pointer = p;
  rows[*m].start_time_tick_for_this_process = t_inicio;
  rows[*m].finish_tick_for_sort = finish_tick;
  (*m)++;
}

void write_simulation_output(
  const char* output_file_path,
  SimulationContext* simulation_data
) {
  FILE* output_file = fopen(output_file_path, "w");
  if (!output_file) return;

  ProcessPool* finished_pool = &(simulation_data->finished_processes);
  ProcessPool* dead_pool     = &(simulation_data->dead_processes);

  // Construimos candidatos (pueden venir duplicados desde pools)
  size_t total_candidates = finished_pool->internal_dynamic_array_size
                          + dead_pool->internal_dynamic_array_size;

  ProcessPrintableRow* rows = (ProcessPrintableRow*)calloc(total_candidates, sizeof(ProcessPrintableRow));
  if (!rows) { fclose(output_file); return; }

  size_t m = 0;
  for (size_t i = 0; i < finished_pool->internal_dynamic_array_size; ++i) {
    update_or_insert_row(rows, &m, finished_pool->internal_dynamic_array_of_process_pointers[i], simulation_data);
  }
  for (size_t i = 0; i < dead_pool->internal_dynamic_array_size; ++i) {
    update_or_insert_row(rows, &m, dead_pool->internal_dynamic_array_of_process_pointers[i], simulation_data);
  }

  // Orden por tiempo de término, desempata por PID
  qsort(rows, m, sizeof(ProcessPrintableRow), compare_rows_by_finish_time_then_pid);

  // Escribir CSV (una sola fila por PID)
  for (size_t i = 0; i < m; ++i) {
    Process* p = rows[i].process_pointer;
    unsigned int t_inicio = rows[i].start_time_tick_for_this_process;

    // Métricas: usa primero las del proceso; si están en sentinela, calcula/clampa a 0.
    unsigned long long response_out = 0ULL;
    if (p->response_time != LL_SENTINEL) {
      response_out = (unsigned long long)p->response_time;
    } else if (p->first_time_tick_when_entered_cpu >= 0) {
      long long calc = p->first_time_tick_when_entered_cpu - (long long)t_inicio;
      response_out = (calc < 0) ? 0ULL : (unsigned long long)calc;
    }

    // turnaround_out: finish - T_INICIO
    unsigned long long turnaround_out = 0ULL;
    if (p->turnaround_time != ULL_SENTINEL) {
      turnaround_out = p->turnaround_time;
    } else {
      long long calc = rows[i].finish_tick_for_sort - (long long)t_inicio;
      turnaround_out = (calc < 0) ? 0ULL : (unsigned long long)calc;
    }

    const char* state =
      (p->current_process_state == PROCESS_STATE_DEAD) ? "DEAD" : "FINISHED";

    fprintf(output_file,
      "%s,%u,%s,%u,%llu,%llu,%llu\n",
      p->process_name,
      p->process_id,
      state,
      p->number_of_preemption_interruptions,
      (unsigned long long)turnaround_out,
      (unsigned long long)response_out,
      (unsigned long long)p->accumulated_time_in_ready_or_waiting_states
    );
  }

  free(rows);
  fclose(output_file);
}