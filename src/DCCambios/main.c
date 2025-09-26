#include "io.h"
#include "output.h"
#include "sim.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "Uso: %s <input_file> <output_file>\n", argv[0]);
    return 1;
  }
  const char* input_file_path = argv[1];
  const char* output_file_path = argv[2];

  SimulationInputData input_data;
  if (!read_simulation_input_file_into_memory(input_file_path, &input_data)) {
    fprintf(stderr, "Error al leer el archivo de entrada.\n");
    return 1;
  }

  // Breve resumen de verificación:
  printf("[VERIFICACION] q=%u, procesos=%u, eventos=%u\n",
    input_data.base_quantum_parameter_q_from_first_line,
    input_data.number_of_processes_from_input_file,
    input_data.number_of_forced_cpu_events_from_input_file
  );

  for (unsigned int i = 0; i < input_data.number_of_processes_from_input_file; i++) {
    ProcessInputRecord* rec = &input_data.array_of_process_input_records[i];
    printf("[VERIFICACION] Proceso %u: nombre=%s pid=%u T_INICIO=%u\n",
      i, rec->input_process_name, rec->input_process_id, rec->input_start_time_tick);
  }

  for (unsigned int j = 0; j < input_data.number_of_forced_cpu_events_from_input_file; j++) {
    ForcedCpuEvent* ev = &input_data.array_of_forced_cpu_events[j];
    printf("[VERIFICACION] Evento %u: pid=%u t=%u\n",
      j, ev->event_process_id, ev->event_time_tick);
  }

  SimulationContext sim_context;
  if (!initialize_simulation_context_from_input(&sim_context, &input_data)) {
    fprintf(stderr, "No se pudo inicializar el contexto de simulación.\n");
    destroy_simulation_input_data(&input_data);
    return 1;
  }

  run_simulation_skeleton_main_loop(&sim_context);

  write_simulation_output(output_file_path, &sim_context);

  destroy_simulation_context(&sim_context);
  return 0;
}