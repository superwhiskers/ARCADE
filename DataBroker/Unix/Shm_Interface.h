#pragma once

#include <pthread.h>

/// Mutex guarding `PUB_DATA` and `UP_DATA`.
extern pthread_mutex_t DATA_Mutx;

/// Initialize the shared memory thread.
void *Shm_Interface(void *);

// Data received over shared memory.
typedef struct {
    /// Name of the value.
    char Name[128];

    /// Type of the value.
    char Type[50];

    /// Value.
    double Value;

    /// Simulation time at which the value was recorded.
    double Time;
} DATA;

// Upper limit of data that may be recorded in memory at once.
#define MAX_IO 1000

extern DATA UP_DATA[MAX_IO];
extern DATA PUB_DATA[MAX_IO];

typedef struct {
    int PUB;
    int UP;
    double TimeStep;
} MSG_DATA;
