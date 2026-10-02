#include "UDP_Server.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "Sem_Stop.h"
#include "atomicSet.h"

void UDP_Server(char *msg)
{
    int sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        perror("Could not create a socket");
        setErrorFlag();
        Set_Stop();
        return;
    }

    int broadcastEnable = 1;
    int ret = setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable,
                         sizeof(broadcastEnable));
    if (ret) {
        perror("Could not set socket options");
        close(sockfd);
        setErrorFlag();
        Set_Stop();
        return;
    }

    struct sockaddr_in servaddr = {0};
    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET, "255.255.255.255", &servaddr.sin_addr);
    servaddr.sin_port = htons(UDP_PORT);

    sendto(sockfd, msg, strlen(msg), 0, (const struct sockaddr *)(&servaddr),
           sizeof(servaddr));

    close(sockfd);
}

void UDP_Stop(void)
{
    char msg[256];
    memset(msg, 0, sizeof(msg));
    sprintf(msg, "STOP\nSTOP\n");
    UDP_Server(msg);
    for (int i = 0; i < 5; i++) {
        UDP_Server(msg);
    }
}
