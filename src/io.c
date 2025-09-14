#include "io.h"
#include "events.h"
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

bool read_simulation_input_file_into_memory(
  const char* input_file_path,
  SimulationInputData* out
) {
  if (!input_file_path || !out) return false;
  memset(out, 0, sizeof(*out));

  FILE* f = fopen(input_file_path, "r");
  if (!f) {
    fprintf(stderr, "No se pudo abrir el archivo de entrada: %s\n", input_file_path);
    return false;
  }

  unsigned int q_param = 0, K = 0, N = 0;
  if (fscanf(f, "%u", &q_param) != 1) { fclose(f); return false; }
  if (fscanf(f, "%u", &K) != 1) { fclose(f); return false; }
  if (fscanf(f, "%u", &N) != 1) { fclose(f); return false; }

  out->base_quantum_parameter_q_from_first_line = q_param;
  out->number_of_processes_from_input_file = K;
  out->number_of_forced_cpu_events_from_input_file = N;

  out->array_of_process_input_records = (ProcessInputRecord*)calloc(K, sizeof(ProcessInputRecord));
  if (!out->array_of_process_input_records) { fclose(f); return false; }

  char process_name_buffer[256];
  for (unsigned int i = 0; i < K; ++i) {
    unsigned int pid=0, t_inicio=0, t_cpu_burst=0, n_bursts=0, io_wait=0, t_deadline=0;
    if (fscanf(f, "%255s %u %u %u %u %u %u",
               process_name_buffer, &pid, &t_inicio, &t_cpu_burst, &n_bursts, &io_wait, &t_deadline) != 7) {
      fclose(f);
      return false;
    }

    ProcessInputRecord* rec = &out->array_of_process_input_records[i];
    rec->input_process_name = duplicate_string_or_null(process_name_buffer);
    rec->input_process_id = pid;
    rec->input_start_time_tick = t_inicio;
    rec->input_cpu_burst_duration_per_burst = t_cpu_burst;
    rec->input_total_number_of_cpu_bursts = n_bursts;
    rec->input_input_output_wait_time_between_bursts = io_wait;
    rec->input_absolute_execution_deadline = t_deadline;

    rec->instantiated_process_pointer = create_process_from_input_line(
      rec->input_process_name,
      rec->input_process_id,
      rec->input_start_time_tick,
      rec->input_cpu_burst_duration_per_burst,
      rec->input_total_number_of_cpu_bursts,
      rec->input_input_output_wait_time_between_bursts,
      rec->input_absolute_execution_deadline
    );
    if (!rec->instantiated_process_pointer) {
      fclose(f);
      return false;
    }
  }

  out->array_of_forced_cpu_events = (ForcedCpuEvent*)calloc(N, sizeof(ForcedCpuEvent));
  if (!out->array_of_forced_cpu_events && N > 0) { fclose(f); return false; }

  for (unsigned int j = 0; j < N; ++j) {
    unsigned int pid=0, t_evento=0;
    if (fscanf(f, "%u %u", &pid, &t_evento) != 2) { fclose(f); return false; }
    out->array_of_forced_cpu_events[j].event_process_id = pid;
    out->array_of_forced_cpu_events[j].event_time_tick = t_evento;
    out->array_of_forced_cpu_events[j].input_order_index_for_stable_sorting = j;
  }

  fclose(f);

  // Ordenar eventos por tiempo y por orden de aparición:
  sort_forced_cpu_events_by_time_then_input_order(
    out->array_of_forced_cpu_events,
    out->number_of_forced_cpu_events_from_input_file
  );

  return true;
}

void destroy_simulation_input_data(
  SimulationInputData* data
) {
  if (!data) return;

  if (data->array_of_process_input_records) {
    for (unsigned int i = 0; i < data->number_of_processes_from_input_file; ++i) {
      ProcessInputRecord* rec = &data->array_of_process_input_records[i];
      if (rec->instantiated_process_pointer) {
        destroy_process(rec->instantiated_process_pointer);
      }
      free(rec->input_process_name);
    }
    free(data->array_of_process_input_records);
  }

  if (data->array_of_forced_cpu_events) {
    free(data->array_of_forced_cpu_events);
  }

  memset(data, 0, sizeof(*data));
}