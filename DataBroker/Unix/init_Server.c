#include "init_Server.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <zmq.h>
#include "cJSON.h"

#include "utils.h"

static cJSON *read_config(void)
{
    size_t len;

    char *data = OpenFile("input.json", &len);
    if (!data) {
        perror("Opening input.json");
        return NULL;
    }

    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        perror("Invalid input.json");
    }

    munmap(data, len);
    return root;
}

static int send_config(void *socket, const char *text, bool allow_skip)
{
    for (int attempt = 0; attempt < 5; ++attempt) {
        zmq_msg_t reply;
        zmq_msg_init(&reply);

        int rc = zmq_exchange(socket, text, &reply);
        if (rc != WAIT_OK) {
            return -1;
        }

        // remove the terminating null from the message if one is present
        size_t n = zmq_msg_size(&reply);
        if (n && ((char *)zmq_msg_data(&reply))[n - 1] == '\0') {
            --n;
        }

        bool valid = n == 5 && memcmp(zmq_msg_data(&reply), "VALID", 5) == 0;
        bool skip = allow_skip && n == 4 && memcmp(zmq_msg_data(&reply), "SKIP", 4) == 0;

        zmq_msg_close(&reply);

        if (valid || skip) {
            return 0;
        }
    }
    errno = EPROTO;
    return -1;
}

int init_Server(void)
{
    cJSON *root = read_config();
    if (!root) {
        return -1;
    }

    int result = -1;
    void *context = zmq_ctx_new(), *requester = NULL;
    if (!context) {
        goto cleanup;
    }

    FLAGS.Hold_Time_Flag = Read_flags("simulator", "hold_for_dante", false);
    CONF.Co_Sim_Enable = Read_flags("simulator", "co_sim_enable", false);
    CONF.Co_Sim_Sync_Enable = Read_flags("cosim", "sync_enable", false);
    CONF.Realtime_Timestep = Read_flags("simulator", "realtime_timestep", false);

    cJSON *endpoint;
    cJSON_ArrayForEach(endpoint, cJSON_GetObjectItem(root, "endpoints"))
    {
        cJSON *host = cJSON_GetObjectItem(endpoint, "IP_Host");
        if (!cJSON_IsString(host)) {
            errno = EINVAL;
            goto cleanup;
        }
        char address[256];
        int n = snprintf(address, sizeof(address), "tcp://%s:6666", host->valuestring);
        if (n < 0 || (size_t)n >= sizeof(address)) {
            errno = EINVAL;
            goto cleanup;
        }
        requester = zmq_socket(context, ZMQ_REQ);
        if (!requester || zmq_configure(requester) || zmq_connect(requester, address)) {
            goto cleanup;
        }
        const char *groups[] = {"PLCS", "Server"};
        const char *types[] = {"plc", "server"};
        for (int i = 0; i < 2; ++i) {
            cJSON *item;
            cJSON_ArrayForEach(item, cJSON_GetObjectItem(endpoint, groups[i]))
            {
                if (!cJSON_GetObjectItem(item, "Type")) {
                    cJSON_AddStringToObject(item, "Type", types[i]);
                }
                char *text = cJSON_PrintUnformatted(item);
                if (!text)
                    goto cleanup;
                int rc = send_config(requester, text, true);
                free(text);
                if (rc != 0) {
                    goto cleanup;
                }
            }
        }
        if (send_config(requester, "END_MESSAGE", false)) {
            goto cleanup;
        }
        zmq_close(requester);
        requester = NULL;
    }
    result = 0;
    printf("Endpoint Initialization Complete\n");
cleanup:
    if (requester)
        zmq_close(requester);
    if (context)
        zmq_ctx_destroy(context);
    cJSON_Delete(root);
    return result;
}

char *Read_Vars(char *category, char *varname, char *fallback)
{
    cJSON *root = read_config();
    if (!root) {
        return NULL;
    }

    const char *value = fallback;
    cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItem(root, category))
    {
        cJSON *field = cJSON_GetObjectItem(item, varname);
        if (cJSON_IsString(field))
            value = field->valuestring;
        break;
    }
    char *copy = value ? strdup(value) : NULL;
    cJSON_Delete(root);
    return copy;
}

char *SimName(void)
{
    return Read_Vars("simulator", "executableName", NULL);
}

bool Read_flags(char *category, char *flagname, bool fallback)
{
    char *value = Read_Vars(category, flagname, fallback ? "true" : "false");
    bool result = value ? strtobool(value) : fallback;
    free(value);
    return result;
}

char *OpenFile(const char *filename, size_t *len)
{
    char *data = NULL;
    int file;

    RETRY_EINTR(file, open(filename, O_RDONLY | O_CLOEXEC));
    if (file == -1) {
        return NULL;
    }

    struct stat info = {0};

    if (fstat(file, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size == 0) {
        goto cleanup;
    }

    *len = info.st_size;

    data = mmap(NULL, *len, PROT_READ, MAP_PRIVATE, file, 0);
    if (data == MAP_FAILED) {
        data = NULL;
        goto cleanup;
    }

cleanup:
    close(file);
    return data;
}
