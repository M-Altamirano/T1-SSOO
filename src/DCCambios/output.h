#ifndef OUTPUT_H
#define OUTPUT_H

#include "sim.h"
#include "queue.h"
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void write_simulation_output(
  const char* output_file_path,
  SimulationContext* simulation_data
);


#endif