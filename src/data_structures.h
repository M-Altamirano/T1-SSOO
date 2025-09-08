#pragma once
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>


struct process_data;
typedef struct procces_data ProcessData;
struct process_data {
    volatile pid_t pid;
    char *process_name;
    unsigned int init_time;
    unsigned int cpu_burst_time;
    unsigned int io_burst_time;
    int cpu_bursts;
    unsigned int deadline;
    float priority;              
    char state[10];
    ProcessData* next_process;
    ProcessData* prev_process;
};

struct process_queue {
    unsigned int quantum;
    ProcessData* first_process;
};
typedef struct process_queue Queue;
