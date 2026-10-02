#include "ZMQ_Client.h"

#include <pthread.h>
#include <stdbool.h>
#include <zmq.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

#include "Shm_Interface.h"
#include "Sem_Stop.h"
#include "init_Server.h"
#include "utils.h"

void *ZMQ_Client(void *_)
{
    int result = WAIT_ERROR;
    void *context = NULL, *socket = NULL;
    sem_t *trigger = SEM_FAILED, *done = SEM_FAILED;
    ZMQ_TagMap outputs[MAX_ZMQ_VARS];
    int count = 0;

    char *tags = Read_Vars("cosim", "outputs", "");
    if (!tags) {
        goto cleanup;
    }

    char *save;
    for (char *tag = strtok_r(tags, ",", &save); tag; tag = strtok_r(NULL, ",", &save)) {
        if (count == MAX_ZMQ_VARS || strlen(tag) >= sizeof(outputs[count].tag)) {
            errno = EINVAL;
            goto cleanup;
        }
        strcpy(outputs[count++].tag, tag);
    }

    trigger = sem_open("/co_sim", 0);
    done = sem_open("/co_sim_2", 0);
    if (trigger == SEM_FAILED || done == SEM_FAILED) {
        goto cleanup;
    }

    result = sem_wait_safe(trigger, 0);
    if (result != WAIT_OK) {
        goto cleanup;
    }

    pthread_mutex_lock(&FLAG_Mutx);
    Configs config = CONF;
    pthread_mutex_unlock(&FLAG_Mutx);

    result = WAIT_ERROR;
    bool missing = false;

    pthread_mutex_lock(&DATA_Mutx);
    for (int i = 0; i < count; ++i) {
        outputs[i].index = -1;
        for (int j = 0; j < config.PUB_N; ++j) {
            if (strcmp(PUB_DATA[j].Name, outputs[i].tag) == 0) {
                outputs[i].index = j;
                break;
            }
        }

        if (outputs[i].index < 0) {
            missing = true;
        }
    }
    pthread_mutex_unlock(&DATA_Mutx);

    if (missing) {
        errno = EINVAL;
        goto cleanup;
    }

    context = zmq_ctx_new();
    if (!context) {
        goto cleanup;
    }

    socket = zmq_socket(context, ZMQ_REQ);
    if (!socket || zmq_configure(socket) || zmq_connect(socket, "tcp://localhost:5556")) {
        goto cleanup;
    }

    if (config.Co_Sim_Sync_Enable) {
        sem_post(done);
    }

    while (!Sem_Stop()) {
        result = sem_wait_safe(trigger, 0);
        if (result != WAIT_OK) {
            goto cleanup;
        }

        if (!config.Co_Sim_Sync_Enable) {
            while (sem_trywait(trigger) == 0 || errno == EINTR) {
            }
        }

        result = WAIT_ERROR;

        cJSON *root = cJSON_CreateObject();
        if (!root) {
            goto cleanup;
        }

        bool allocated = true;
        pthread_mutex_lock(&DATA_Mutx);
        for (int i = 0; i < count; ++i) {
            if (!cJSON_AddNumberToObject(root, outputs[i].tag,
                                         PUB_DATA[outputs[i].index].Value)) {
                allocated = false;
            }
        }
        pthread_mutex_unlock(&DATA_Mutx);

        char *request = allocated ? cJSON_PrintUnformatted(root) : NULL;
        cJSON_Delete(root);
        if (!request) {
            goto cleanup;
        }

        zmq_msg_t response;
        zmq_msg_init(&response);
        result = zmq_exchange(socket, request, &response);
        free(request);
        cJSON *reply = NULL;
        if (result == WAIT_OK) {
            reply = cJSON_ParseWithLength(zmq_msg_data(&response), zmq_msg_size(&response));
        }

        zmq_msg_close(&response);
        if (result != WAIT_OK) {
            goto cleanup;
        }

        result = WAIT_ERROR;
        if (!cJSON_IsObject(reply)) {
            cJSON_Delete(reply);
            errno = EPROTO;
            goto cleanup;
        }

        cJSON *item;
        pthread_mutex_lock(&DATA_Mutx);
        cJSON_ArrayForEach(item, reply)
        {
            if (!cJSON_IsNumber(item)) {
                continue;
            }
            for (int j = 0; j < config.UP_N; ++j)
                if (strcmp(UP_DATA[j].Name, item->string) == 0) {
                    UP_DATA[j].Value = item->valuedouble;
                    break;
                }
        }
        pthread_mutex_unlock(&DATA_Mutx);
        cJSON_Delete(reply);

        if (config.Co_Sim_Sync_Enable) {
            sem_post(done);
        }
    }
    result = WAIT_STOPPED;
cleanup:
    if (result == WAIT_ERROR) {
        perror("Co-simulation interface");
    }

    Set_Stop();
    free(tags);

    if (trigger != SEM_FAILED) {
        sem_close(trigger);
    }

    if (done != SEM_FAILED) {
        sem_close(done);
    }

    if (socket) {
        zmq_close(socket);
    }

    if (context) {
        zmq_ctx_destroy(context);
    }

    return result == WAIT_ERROR ? (void *)-1 : NULL;
}
