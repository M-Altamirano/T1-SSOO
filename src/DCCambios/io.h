#ifndef IO_H
#define IO_H

#include "process.h"
#include "events.h"
#include "queue.h"
#include <stddef.h>
#include <stdbool.h>


typedef struct ProcessInputRecord {
  // Copia de los datos originales del input, para referencia
  char* input_process_name;
  unsigned int input_process_id;
  unsigned int input_start_time_tick;
  unsigned int input_cpu_burst_duration_per_burst;
  unsigned int input_total_number_of_cpu_bursts;
  unsigned int input_input_output_wait_time_between_bursts;
  unsigned int input_absolute_execution_deadline;


  // Instancia viva del proceso:
  Process* instantiated_process_pointer;
} ProcessInputRecord;

typedef struct SimulationInputData {
  unsigned int base_quantum_parameter_q_from_first_line;
  unsigned int number_of_processes_from_input_file;
  unsigned int number_of_forced_cpu_events_from_input_file;

  ProcessInputRecord* array_of_process_input_records;
  ForcedCpuEvent* array_of_forced_cpu_events;

} SimulationInputData;


// Lee archivo de entrada y llena SimulationInputData (incluye ordenar eventos)
bool read_simulation_input_file_into_memory(
  const char* input_file_path,
  SimulationInputData* out_simulation_input_data
);


// Libera memoria al finalizar
void destroy_simulation_input_data(
  SimulationInputData* data
);

#endif