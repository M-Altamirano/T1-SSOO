#include "output.h"
#include <math.h>


static size_t partition_pool (ProcessPool* pool, size_t beginning, size_t end) {
  if (beginning < end) {
    size_t p = (size_t) floor((end - beginning) / 2) + beginning;
    Process* pivot = pool->internal_dynamic_array_of_process_pointers[p];
    pool->internal_dynamic_array_of_process_pointers[p] = pool->internal_dynamic_array_of_process_pointers[end];
    pool->internal_dynamic_array_of_process_pointers[end] = pivot;
    size_t current_position = beginning;
    for (size_t i = beginning; i < end; i++) {
        if (pool->internal_dynamic_array_of_process_pointers[i]->process_id < pivot->process_id) {
        Process* placeholder = pool->internal_dynamic_array_of_process_pointers[current_position];
        pool->internal_dynamic_array_of_process_pointers[current_position] = pool->internal_dynamic_array_of_process_pointers[i];
        pool->internal_dynamic_array_of_process_pointers[i] = placeholder;
        current_position++;
        }
    }
    pool->internal_dynamic_array_of_process_pointers[end] = pool->internal_dynamic_array_of_process_pointers[current_position];
    pool->internal_dynamic_array_of_process_pointers[current_position] = pivot;
    return current_position;
  }
  else return beginning;
}

static void reorder_pool_by_id(ProcessPool* pool, size_t beginning, size_t end) {
  if (beginning < end) {
    size_t pivot = partition_pool(pool, beginning, end);
    if (pivot > 0) reorder_pool_by_id(pool, beginning, pivot - 1);
    reorder_pool_by_id(pool, pivot + 1, end);
  }
}

static ProcessPool* merge_pools(ProcessPool* pool_a, ProcessPool* pool_b) {
  ProcessPool* pool_c = (ProcessPool*)calloc(1, sizeof(ProcessPool));
  while (pool_a->current_merge_index < pool_a->internal_dynamic_array_size && pool_b->current_merge_index < pool_b->internal_dynamic_array_size) {
    Process* pa = pool_a->internal_dynamic_array_of_process_pointers[pool_a->current_merge_index];
    Process* pb = pool_b->internal_dynamic_array_of_process_pointers[pool_b->current_merge_index];
    if (pa->process_id < pb->process_id) {
      push_process_into_process_pool(pool_c, pa);
      pool_a->current_merge_index++;
    }
    else {
      push_process_into_process_pool(pool_c, pb);
      pool_b->current_merge_index++;
    }
  }
  ProcessPool* remaining_pool;
  if (pool_a->current_merge_index == pool_a->internal_dynamic_array_size) remaining_pool = pool_b;
  else remaining_pool = pool_a;
  while (remaining_pool->current_merge_index < remaining_pool->internal_dynamic_array_size) {
    push_process_into_process_pool(pool_c, remaining_pool->internal_dynamic_array_of_process_pointers[remaining_pool->current_merge_index]);
    remaining_pool->current_merge_index++;
  }
  return pool_c;
}


void write_simulation_output(
  const char* output_file_path,
  SimulationContext* simulation_data
) {

  FILE* output_file = fopen(output_file_path, "w");

  ProcessPool* finished_pool = &(simulation_data->finished_processes);
  ProcessPool* dead_pool = &(simulation_data->dead_processes);

  printf("DEAD %zu FINISH %zu\n", dead_pool->internal_dynamic_array_size, finished_pool->internal_dynamic_array_size);

  if (finished_pool->internal_dynamic_array_size > 1) reorder_pool_by_id(finished_pool, (size_t) 0, finished_pool->internal_dynamic_array_size - (size_t)1);
  if (dead_pool->internal_dynamic_array_size > 1) reorder_pool_by_id(dead_pool, (size_t) 0, dead_pool->internal_dynamic_array_size - (size_t)1);

  ProcessPool* all_processes = merge_pools(finished_pool, dead_pool);
  for (size_t i = 0; i < all_processes->internal_dynamic_array_size; i++) {
    Process* p = all_processes->internal_dynamic_array_of_process_pointers[i];
    char state[9];
    if (p->current_process_state == PROCESS_STATE_DEAD) strcpy(state, "DEAD");
    else strcpy(state, "FINISHED");
    fprintf(output_file, 
      "%s,%u,%s,%u,%llu,%llu,%llu\n", 
      p->process_name, 
      p->process_id, 
      state,
      p->number_of_preemption_interruptions,
      p->turnaround_time,
      p->response_time,
      p->accumulated_time_in_ready_or_waiting_states
    );
    printf(
      "%s,%u,%s,%u,%llu,%llu,%llu, %d\n", 
      p->process_name, 
      p->process_id, 
      state,
      p->number_of_preemption_interruptions,
      p->turnaround_time,
      p->response_time,
      p->accumulated_time_in_ready_or_waiting_states,
      p->has_ever_entered_cpu_at_least_once
    );
  }

//   for (size_t i = 0; i < dead_pool->internal_dynamic_array_size; i++) printf("%s\n", dead_pool->internal_dynamic_array_of_process_pointers[i]->process_name);
//   printf("###########\n");
//   for (size_t i = 0; i < finished_pool->internal_dynamic_array_size; i++) printf("%s\n", finished_pool->internal_dynamic_array_of_process_pointers[i]->process_name);
  fclose(output_file);
  destroy_process_pool(all_processes);
  free(all_processes);
}