#pragma once

#include <semaphore.h>

extern sem_t *stop;

/// Initialize the stop semaphore.
void Init_Stop_Semaphore(void);

/// Clean up the stop semaphore handle.
void Cleanup_Stop_Semaphore(void);

/// Check the stop flag semaphore.
int Sem_Stop(void);

/// Set the stop semaphore.
void Set_Stop(void);
