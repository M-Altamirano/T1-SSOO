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
  SimulationContext* sim
) {
  FILE* f = fopen(output_file_path, "w");
  if (!f) return;

  const SimulationInputData* in = &sim->simulation_input_data;

  for (unsigned int i = 0; i < in->number_of_processes_from_input_file; ++i) {
    const ProcessInputRecord* rec = &in->array_of_process_input_records[i];
    Process* p = rec->instantiated_process_pointer;
    if (!p) continue; // por seguridad

    const char* name = p->process_name;
    unsigned int pid  = p->process_id;
    unsigned int t0   = rec->input_start_time_tick;

    // response = (first_cpu - T_INICIO + 1) clamped ≥ 0
    unsigned long long response = 0ULL;
    if (p->first_time_tick_when_entered_cpu >= 0) {
      long long r = p->first_time_tick_when_entered_cpu - (long long)t0 + 1;
      if (r < 0) r = 0;
      response = (unsigned long long) r;
    }

    // turnaround = (finish_tick - T_INICIO) clamped ≥ 0
    unsigned long long tat = 0ULL;
    if (p->time_tick_when_finished_or_dead_for_sorting >= 0) {
      long long tt = p->time_tick_when_finished_or_dead_for_sorting - (long long)t0;
      if (tt < 0) tt = 0;
      tat = (unsigned long long) tt;
    }

    const char* state = (p->current_process_state == PROCESS_STATE_DEAD) ? "DEAD" : "FINISHED";

    fprintf(f, "%s,%u,%s,%u,%llu,%llu,%llu\n",
      name,
      pid,
      state,
      p->number_of_preemption_interruptions,
      tat,
      response,
      (unsigned long long)p->accumulated_time_in_ready_or_waiting_states
    );
  }

  fclose(f);
}