#include "Sem_Interface.h"

#include <errno.h>
#include <sys/shm.h>
#include <stdlib.h>
#include <semaphore.h>
#include <fcntl.h>

static const char *names[] = {
    "/pp_sem", "/up_sem", "/stop", "/msg", "/co_sim", "/co_sim_2",
    "/SemaphoreWrite", "/SemaphoreDone"};

int Sem_Interface(void)
{
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        sem_t *sem = sem_open(names[i], O_CREAT, 0644, 0);
        if (sem == SEM_FAILED) {
            return -1;
        }

        int rc;
        do {
            rc = sem_trywait(sem);
        } while (rc == 0 || errno == EINTR);
        int saved = errno;

        sem_close(sem);
        if (saved != EAGAIN) {
            errno = saved;
            return -1;
        }
    }
    return 0;
}

void Cleanup_Interface(void)
{
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        sem_unlink(names[i]);
    }

    const key_t keys[] = {10618, 10619, 10620, 10621};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        int id = shmget(keys[i], 1, 0600);
        if (id >= 0) {
            shmctl(id, IPC_RMID, NULL);
        }
    }
}
