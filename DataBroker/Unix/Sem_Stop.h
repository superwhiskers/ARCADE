#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <semaphore.h>

extern sem_t* stop;

void Init_Stop_Semaphore(void);
void Cleanup_Stop_Semaphore(void);
int Sem_Stop(void);
void Set_Stop(void);
