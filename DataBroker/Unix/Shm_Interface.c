#include "Shm_Interface.h"

#include <sys/shm.h>
#include <semaphore.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <math.h>

#include "UDP_Server.h"
#include "Sem_Stop.h"
#include "Data_Aggregator.h"
#include "utils.h"

DATA UP_DATA[MAX_IO];
DATA PUB_DATA[MAX_IO];
MSG_DATA DB_MESSAGE;

pthread_mutex_t DATA_Mutx;

void *Shm_Interface(void *_)
{
    sem_t *up = SEM_FAILED, *pub = SEM_FAILED, *msg = SEM_FAILED;
    sem_t *co_sim = SEM_FAILED, *co_sim_2 = SEM_FAILED;
    MSG_DATA *metadata = (void *)-1;
    DATA *published = (void *)-1, *updated = (void *)-1;
    int result = WAIT_ERROR;

    up = sem_open("/up_sem", 0);
    pub = sem_open("/pp_sem", 0);
    msg = sem_open("/msg", 0);
    co_sim = sem_open("/co_sim", 0);
    co_sim_2 = sem_open("/co_sim_2", 0);

    if (up == SEM_FAILED || pub == SEM_FAILED || msg == SEM_FAILED ||
        co_sim == SEM_FAILED || co_sim_2 == SEM_FAILED) {
        goto cleanup;
    }

    result = sem_wait_safe(msg, 0);
    if (result != WAIT_OK) {
        goto cleanup;
    }
    result = WAIT_ERROR;

    int id = shmget(10620, sizeof(MSG_DATA), 0600);
    if (id < 0) {
        goto cleanup;
    }

    metadata = shmat(id, NULL, 0);
    if (metadata == (void *)-1) {
        goto cleanup;
    }
    int n_pub = metadata->PUB, n_up = metadata->UP;
    double dt = metadata->TimeStep;
    if (n_pub < 0 || n_pub > MAX_IO || n_up < 0 || n_up > MAX_IO ||
        !isfinite(dt) || dt < 0) {
        errno = EINVAL;
        goto cleanup;
    }
    if (n_pub) {
        id = shmget(10618, n_pub * sizeof(DATA), 0600);
        if (id < 0) {
            goto cleanup;
        }

        published = shmat(id, NULL, 0);
        if (published == (void *)-1) {
            goto cleanup;
        }
    }
    if (n_up) {
        id = shmget(10619, n_up * sizeof(DATA), 0600);
        if (id < 0) {
            goto cleanup;
        }

        updated = shmat(id, NULL, 0);
        if (updated == (void *)-1) {
            goto cleanup;
        }
    }

    pthread_mutex_lock(&DATA_Mutx);
    for (int i = 0; i < n_pub; ++i) {
        PUB_DATA[i] = published[i];
        PUB_DATA[i].Name[sizeof(PUB_DATA[i].Name) - 1] = '\0';
        PUB_DATA[i].Type[sizeof(PUB_DATA[i].Type) - 1] = '\0';
        enqueue(PUB_DATA_QUEUE, PUB_DATA[i]);
    }
    for (int i = 0; i < n_up; ++i) {
        UP_DATA[i] = updated[i];
        UP_DATA[i].Name[sizeof(UP_DATA[i].Name) - 1] = '\0';
        UP_DATA[i].Type[sizeof(UP_DATA[i].Type) - 1] = '\0';
        enqueue(UP_DATA_QUEUE, UP_DATA[i]);
    }
    pthread_mutex_unlock(&DATA_Mutx);

    pthread_mutex_lock(&FLAG_Mutx);
    CONF.PUB_N = n_pub;
    CONF.UP_N = n_up;
    CONF.TimeStep = dt;
    CONF.config_captured = true;
    Configs config = CONF;
    pthread_mutex_unlock(&FLAG_Mutx);

    bool sync = config.Co_Sim_Enable && config.Co_Sim_Sync_Enable;
    if (config.Co_Sim_Enable) {
        sem_post(co_sim);
        if (sync && (result = sem_wait_safe(co_sim_2, 0)) != WAIT_OK) {
            goto cleanup;
        }
    }
    sem_post(up);

    char message[MAX_IO * 256];
    size_t used;

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (!Sem_Stop()) {
        result = sem_wait_safe(pub, 5);
        if (result != WAIT_OK) {
            goto cleanup;
        }

        used = 0;
        message[0] = '\0';

        pthread_mutex_lock(&DATA_Mutx);
        for (int i = 0; i < n_pub; ++i) {
            PUB_DATA[i] = published[i];
            PUB_DATA[i].Name[sizeof(PUB_DATA[i].Name) - 1] = '\0';
            PUB_DATA[i].Type[sizeof(PUB_DATA[i].Type) - 1] = '\0';
            enqueue(PUB_DATA_QUEUE, PUB_DATA[i]);
            int n = snprintf(message + used, sizeof(message) - used,
                             "%s %s %f %f sec \n", PUB_DATA[i].Name,
                             PUB_DATA[i].Type, PUB_DATA[i].Value, PUB_DATA[i].Time);
            if (n > 0) {
                size_t available = sizeof(message) - used;
                used += (size_t)n < available ? (size_t)n : available - 1;
            }
        }
        pthread_mutex_unlock(&DATA_Mutx);

        if (config.Co_Sim_Enable) {
            sem_post(co_sim);
            if (sync && (result = sem_wait_safe(co_sim_2, 0)) != WAIT_OK) {
                goto cleanup;
            }
        }

        pthread_mutex_lock(&DATA_Mutx);
        for (int i = 0; i < n_up; ++i) {
            updated[i] = UP_DATA[i];
            enqueue(UP_DATA_QUEUE, updated[i]);
        }
        pthread_mutex_unlock(&DATA_Mutx);

        UDP_Server(message);
        while (!Sem_Stop()) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = now.tv_sec - start.tv_sec + (now.tv_nsec - start.tv_nsec) / 1e9;
            if (elapsed >= dt) {
                break;
            }
            sleep_ms(1);
        }
        clock_gettime(CLOCK_MONOTONIC, &start);
        while (!Sem_Stop()) {
            pthread_mutex_lock(&FLAG_Mutx);
            bool hold = FLAGS.Hold_Time_Flag;
            pthread_mutex_unlock(&FLAG_Mutx);

            if (!hold) {
                break;
            }

            sleep_ms(10);
        }
        if (!Sem_Stop()) {
            sem_post(up);
        }
    }
    result = WAIT_STOPPED;

cleanup:
    if (result == WAIT_ERROR) {
        perror("Shared memory interface");
    }

    Set_Stop();

    if (up != SEM_FAILED) {
        sem_post(up);
    }

    UDP_Stop();

    if (metadata != (void *)-1) {
        shmdt(metadata);
    }

    if (published != (void *)-1) {
        shmdt(published);
    }

    if (updated != (void *)-1) {
        shmdt(updated);
    }

    sem_t *handles[] = {up, pub, msg, co_sim, co_sim_2};
    for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); ++i) {
        if (handles[i] != SEM_FAILED) {
            sem_close(handles[i]);
        }
    }

    printf("Shared memory interface exited.\n");

    return result == WAIT_ERROR ? (void *)-1 : NULL;
}
