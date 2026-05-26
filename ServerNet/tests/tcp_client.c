#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include "../../utils/logger.h"
#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 8080

int main(int argc, char **argv)
{
    char *server_ip = DEFAULT_SERVER_IP;
    uint16_t server_port = DEFAULT_SERVER_PORT;
    if (argc == 3)
    {
        server_ip = argv[1];
        server_port = atoi(argv[2]);
    }


    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        LOG_ERROR("socket creation failed: %s", strerror(errno));
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        LOG_ERROR("connection failed: %s", strerror(errno));
        return 1;
    }

    LOG_INFO("Client: Connected to server");

    char buf[256];
    while (1)
    {
        LOG_INFO("Enter message (or 'quit' to exit): ");
        if (fgets(buf, sizeof(buf), stdin) == NULL)
            break;

        if (strncmp(buf, "quit", 4) == 0)
            break;

        if (send(sockfd, buf, strlen(buf), 0) < 0)
        {
            LOG_ERROR("send failed: %s", strerror(errno));
            break;
        }
    }

    close(sockfd);
    return 0;
}